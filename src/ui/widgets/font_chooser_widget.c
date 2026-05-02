/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "ui/widgets/font_chooser_widget.h"
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include <gobject/gclosure.h>
#include FT_FREETYPE_H
#include FT_TYPE1_TABLES_H
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>

#define FONT_PREVIEW_SIZE 14
#define FONT_ICON_SIZE 16
#define POPOVER_MIN_WIDTH     600
#define POPOVER_MAX_WIDTH     1200
#define ICON_COL_PADDING      16 /* icon column + gap before scrollbar */
#define POPOVER_HEIGHT        500
#define SCROLLBAR_MIN_RESERVE 48 /* pre-realize / hidden fallback */

enum {
    COL_DISPLAY_NAME = 0,
    COL_FONT_DESC_STR,
    COL_ICON_PIXBUF,
    COL_FAMILY_NAME,
    COL_WEIGHT,
    COL_STYLE,
    COL_IS_FAMILY_ROOT,
    NUM_COLUMNS
};

enum {
    SIGNAL_FONT_CHANGED = 0,
    NUM_SIGNALS
};

static guint font_chooser_signals[NUM_SIGNALS] = {0};

/* Fontconfig synthetic / probe faces trigger harmless Pango warnings during font enumeration. */
static gboolean font_chooser_suppress_pango_font_probe_warning(const gchar* message) {
    if (!message)
        return FALSE;
    /* Typical CLI line: couldn't load font "Wide Latin Not-Rotated 14", falling back to ... */
    if (strstr(message, "couldn't load font") != NULL)
        return TRUE;
    if (strstr(message, "could not load font") != NULL)
        return TRUE;
    if (strstr(message, "Could not load font") != NULL)
        return TRUE;
    return FALSE;
}

#if GLIB_CHECK_VERSION(2, 50, 0)

static gboolean font_chooser_suppress_pango_font_probe_warning_sized(const gchar* message,
                                                                     gssize msg_len) {
    if (!message)
        return FALSE;
    if (msg_len < 0)
        return font_chooser_suppress_pango_font_probe_warning(message);
    gchar* tmp = g_strndup(message, (gsize)msg_len);
    gboolean hit = font_chooser_suppress_pango_font_probe_warning(tmp);
    g_free(tmp);
    return hit;
}

static GLogWriterOutput font_chooser_font_probe_log_writer(GLogLevelFlags log_level,
                                                         const GLogField* fields,
                                                         gsize n_fields,
                                                         gpointer user_data) {
    (void)user_data;

    if ((log_level & G_LOG_LEVEL_MASK) != G_LOG_LEVEL_WARNING)
        return g_log_writer_default(log_level, fields, n_fields, user_data);

    const gchar* domain = NULL;
    const gchar* message = NULL;
    gssize msg_len = -1;

    for (gsize i = 0; i < n_fields; i++) {
        if (strcmp(fields[i].key, "GLIB_DOMAIN") == 0 && fields[i].value)
            domain = fields[i].value;
        else if (strcmp(fields[i].key, "MESSAGE") == 0 && fields[i].value) {
            message = fields[i].value;
            msg_len = fields[i].length;
        }
    }

    if (domain && g_str_equal(domain, "Pango") &&
        font_chooser_suppress_pango_font_probe_warning_sized(message, msg_len))
        return G_LOG_WRITER_HANDLED;

    return g_log_writer_default(log_level, fields, n_fields, user_data);
}

void font_chooser_install_font_probe_log_suppression(void) {
    static gboolean installed;
    if (installed)
        return;
    installed = TRUE;
    g_log_set_writer_func(font_chooser_font_probe_log_writer, NULL, NULL);
}

#else /* GLib < 2.50: structured writer unavailable */

static GLogFunc font_chooser_prev_default_log_handler;

static void font_chooser_default_log_filter(const gchar* log_domain, GLogLevelFlags log_level,
                                            const gchar* message, gpointer user_data) {
    (void)user_data;
    if (log_domain && g_str_equal(log_domain, "Pango") && message &&
        (log_level & G_LOG_LEVEL_MASK) == G_LOG_LEVEL_WARNING &&
        font_chooser_suppress_pango_font_probe_warning(message))
        return;
    if (font_chooser_prev_default_log_handler)
        font_chooser_prev_default_log_handler(log_domain, log_level, message, NULL);
    else
        g_log_default_handler(log_domain, log_level, message, NULL);
}

void font_chooser_install_font_probe_log_suppression(void) {
    static gboolean installed;
    if (installed)
        return;
    installed = TRUE;
    font_chooser_prev_default_log_handler =
        g_log_set_default_handler(font_chooser_default_log_filter, NULL);
}

#endif /* GLIB_CHECK_VERSION(2, 50, 0) */

struct _FontChooserWidget {
    GtkBox parent;
    GtkWidget* button;
    GtkWidget* button_label;
    GtkWidget* popover;
    GtkWidget* font_list_scrolled;
    GtkWidget* search_entry;
    GtkWidget* tree_view;
    GtkTreeStore* store;
    GtkTreeModel* filter_model;
    gchar* current_family;
    GdkPixbuf* truetype_icon;
    GdkPixbuf* opentype_icon;
    FT_Library ft_library;
};

G_DEFINE_TYPE(FontChooserWidget, font_chooser_widget, GTK_TYPE_BOX)

/* Forward declarations */
static void font_chooser_widget_populate(FontChooserWidget* self);
static void font_chooser_widget_finalize(GObject* object);
static void on_button_clicked(GtkButton* button, gpointer user_data);
static void on_row_activated(GtkTreeView* tree_view, GtkTreePath* path,
                             GtkTreeViewColumn* column, gpointer user_data);
static void on_search_changed(GtkSearchEntry* entry, gpointer user_data);
static gboolean filter_visible_func(GtkTreeModel* model, GtkTreeIter* iter,
                                    gpointer data);
static void preview_cell_data_func(GtkTreeViewColumn* col, GtkCellRenderer* cell,
                                   GtkTreeModel* model, GtkTreeIter* iter,
                                   gpointer data);
static void font_chooser_sync_popover_width(FontChooserWidget* self);
static gboolean idle_font_chooser_sync_popover_width(gpointer user_data);
static void on_font_list_vscrollbar_size_allocate(GtkWidget* widget, GdkRectangle* alloc,
                                                  gpointer user_data);

/* -----------------------------------------------------------------------
 * Font preview cell renderer (custom — avoids GtkCellRendererText font-desc merge
 * that produces spurious "Family Not-Rotated 14" loads and Pango warnings).
 * --------------------------------------------------------------------- */

typedef struct _FontPreviewCellRenderer      FontPreviewCellRenderer;
typedef struct _FontPreviewCellRendererClass FontPreviewCellRendererClass;

struct _FontPreviewCellRenderer {
    GtkCellRenderer parent_instance;
    gchar*          family;
    gchar*          preview_text;
    gint            weight;
    gint            style;
};

struct _FontPreviewCellRendererClass {
    GtkCellRendererClass parent_class;
};

#define FONT_PREVIEW_TYPE_CELL_RENDERER (font_preview_cell_renderer_get_type())
#define FONT_PREVIEW_CELL_RENDERER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), FONT_PREVIEW_TYPE_CELL_RENDERER, FontPreviewCellRenderer))

G_DEFINE_TYPE(FontPreviewCellRenderer, font_preview_cell_renderer, GTK_TYPE_CELL_RENDERER)

enum {
    FONT_PREVIEW_PROP_0,
    FONT_PREVIEW_PROP_FAMILY,
    FONT_PREVIEW_PROP_PREVIEW_TEXT,
    FONT_PREVIEW_PROP_WEIGHT,
    FONT_PREVIEW_PROP_STYLE,
};

static PangoLayout* font_preview_layout_new(GtkWidget* widget) {
    /* Avoid gtk_widget_create_pango_layout(): some GTK builds merge widget/CSS fonts first. */
    return pango_layout_new(gtk_widget_get_pango_context(widget));
}

static void font_preview_set_layout_font(PangoLayout* layout, const gchar* fam, gint weight,
                                         gint style) {
    /* Replace any widget/CSS-derived font on the layout (avoids FC synthetic names). */
    pango_layout_set_attributes(layout, NULL);

    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, fam);
    pango_font_description_set_weight(desc, weight > 0 ? weight : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_style(desc, (PangoStyle)style);
    pango_font_description_set_size(desc, FONT_PREVIEW_SIZE * PANGO_SCALE);
    pango_font_description_unset_fields(
        desc, (PangoFontMask)(PANGO_FONT_MASK_GRAVITY | PANGO_FONT_MASK_VARIATIONS |
                              PANGO_FONT_MASK_VARIANT | PANGO_FONT_MASK_STRETCH));
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
}

static void font_preview_cell_renderer_finalize(GObject* object) {
    FontPreviewCellRenderer* self = FONT_PREVIEW_CELL_RENDERER(object);
    g_free(self->family);
    g_free(self->preview_text);
    G_OBJECT_CLASS(font_preview_cell_renderer_parent_class)->finalize(object);
}

static void font_preview_cell_renderer_set_property(GObject* object, guint prop_id,
                                                      const GValue* value, GParamSpec* pspec) {
    FontPreviewCellRenderer* self = FONT_PREVIEW_CELL_RENDERER(object);
    switch (prop_id) {
    case FONT_PREVIEW_PROP_FAMILY:
        g_free(self->family);
        self->family = g_value_dup_string(value);
        break;
    case FONT_PREVIEW_PROP_PREVIEW_TEXT:
        g_free(self->preview_text);
        self->preview_text = g_value_dup_string(value);
        break;
    case FONT_PREVIEW_PROP_WEIGHT:
        self->weight = g_value_get_int(value);
        break;
    case FONT_PREVIEW_PROP_STYLE:
        self->style = g_value_get_int(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void font_preview_cell_renderer_get_property(GObject* object, guint prop_id,
                                                    GValue* value, GParamSpec* pspec) {
    FontPreviewCellRenderer* self = FONT_PREVIEW_CELL_RENDERER(object);
    switch (prop_id) {
    case FONT_PREVIEW_PROP_FAMILY:
        g_value_set_string(value, self->family);
        break;
    case FONT_PREVIEW_PROP_PREVIEW_TEXT:
        g_value_set_string(value, self->preview_text);
        break;
    case FONT_PREVIEW_PROP_WEIGHT:
        g_value_set_int(value, self->weight);
        break;
    case FONT_PREVIEW_PROP_STYLE:
        g_value_set_int(value, self->style);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void font_preview_cell_renderer_get_preferred_width(GtkCellRenderer* cell,
                                                           GtkWidget* widget, gint* min_width,
                                                           gint* natural_width) {
    FontPreviewCellRenderer* self = FONT_PREVIEW_CELL_RENDERER(cell);
    gint xpad, ypad;
    gtk_cell_renderer_get_padding(cell, &xpad, &ypad);

    const gchar* fam =
        (self->family && self->family[0]) ? self->family : "Sans";
    const gchar* txt = (self->preview_text && self->preview_text[0]) ? self->preview_text : fam;

    PangoLayout* layout = font_preview_layout_new(widget);
    if (!layout) {
        if (min_width)
            *min_width = 0;
        if (natural_width)
            *natural_width = 2 * xpad;
        return;
    }

    font_preview_set_layout_font(layout, fam, self->weight, self->style);
    pango_layout_set_text(layout, txt, -1);
    pango_layout_set_width(layout, -1);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);

    gint w_px = 0;
    gint h_px = 0;
    pango_layout_get_pixel_size(layout, &w_px, &h_px);
    g_object_unref(layout);

    gint natural = w_px + 2 * xpad;
    if (min_width)
        *min_width = MIN(natural, 24);
    if (natural_width)
        *natural_width = natural;
}

static void font_preview_cell_renderer_get_preferred_height(GtkCellRenderer* cell,
                                                            GtkWidget* widget,
                                                            gint* min_height,
                                                            gint* natural_height) {
    FontPreviewCellRenderer* self = FONT_PREVIEW_CELL_RENDERER(cell);
    gint xpad, ypad;
    gtk_cell_renderer_get_padding(cell, &xpad, &ypad);

    const gchar* fam =
        (self->family && self->family[0]) ? self->family : "Sans";
    const gchar* txt = (self->preview_text && self->preview_text[0]) ? self->preview_text : fam;

    PangoLayout* layout = font_preview_layout_new(widget);
    if (!layout) {
        if (min_height)
            *min_height = 0;
        if (natural_height)
            *natural_height = 2 * ypad;
        return;
    }

    font_preview_set_layout_font(layout, fam, self->weight, self->style);
    pango_layout_set_text(layout, txt, -1);
    pango_layout_set_width(layout, -1);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);

    gint w_px = 0;
    gint h_px = 0;
    pango_layout_get_pixel_size(layout, &w_px, &h_px);
    g_object_unref(layout);

    gint h = h_px + 2 * ypad;
    if (min_height)
        *min_height = h;
    if (natural_height)
        *natural_height = h;
}

static void font_preview_cell_renderer_render(GtkCellRenderer* cell, cairo_t* cr,
                                              GtkWidget* widget, const GdkRectangle* background_area,
                                              const GdkRectangle* cell_area,
                                              GtkCellRendererState flags) {
    (void)background_area;

    FontPreviewCellRenderer* self = FONT_PREVIEW_CELL_RENDERER(cell);
    gint xpad, ypad;
    gtk_cell_renderer_get_padding(cell, &xpad, &ypad);

    GdkRectangle aligned;
    gtk_cell_renderer_get_aligned_area(cell, widget, flags, cell_area, &aligned);

    const gchar* fam =
        (self->family && self->family[0]) ? self->family : "Sans";
    const gchar* txt = (self->preview_text && self->preview_text[0]) ? self->preview_text : fam;

    PangoLayout* layout = font_preview_layout_new(widget);
    if (!layout)
        return;

    font_preview_set_layout_font(layout, fam, self->weight, self->style);
    pango_layout_set_text(layout, txt, -1);

    gint lay_w = aligned.width - 2 * xpad;
    if (lay_w > 0)
        pango_layout_set_width(layout, lay_w * PANGO_SCALE);
    else
        pango_layout_set_width(layout, -1);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

    GtkStyleContext* ctx = gtk_widget_get_style_context(widget);
    GdkRGBA fg;
    gtk_style_context_get_color(ctx, gtk_widget_get_state_flags(widget), &fg);

    cairo_save(cr);
    gdk_cairo_rectangle(cr, &aligned);
    cairo_clip(cr);

    cairo_move_to(cr, (gdouble)(aligned.x + xpad), (gdouble)(aligned.y + ypad));
    gdk_cairo_set_source_rgba(cr, &fg);
    pango_cairo_update_layout(cr, layout);
    pango_cairo_show_layout(cr, layout);

    cairo_restore(cr);
    g_object_unref(layout);
}

static void font_preview_cell_renderer_init(FontPreviewCellRenderer* self) {
    self->family = g_strdup("Sans");
    self->preview_text = g_strdup("");
    self->weight = PANGO_WEIGHT_NORMAL;
    self->style = PANGO_STYLE_NORMAL;
    /* GtkCellRenderer defaults xalign=0.5 — preview text must hug the left edge */
    gtk_cell_renderer_set_alignment(GTK_CELL_RENDERER(self), 0.0f, 0.5f);
}

static void font_preview_cell_renderer_class_init(FontPreviewCellRendererClass* klass) {
    GObjectClass* obj_class = G_OBJECT_CLASS(klass);
    GtkCellRendererClass* cell_class = GTK_CELL_RENDERER_CLASS(klass);

    obj_class->finalize = font_preview_cell_renderer_finalize;
    obj_class->set_property = font_preview_cell_renderer_set_property;
    obj_class->get_property = font_preview_cell_renderer_get_property;

    cell_class->render = font_preview_cell_renderer_render;
    cell_class->get_preferred_width = font_preview_cell_renderer_get_preferred_width;
    cell_class->get_preferred_height = font_preview_cell_renderer_get_preferred_height;

    g_object_class_install_property(
        obj_class, FONT_PREVIEW_PROP_FAMILY,
        g_param_spec_string("family", NULL, NULL, "Sans",
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        obj_class, FONT_PREVIEW_PROP_PREVIEW_TEXT,
        g_param_spec_string("preview-text", NULL, NULL, "",
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        obj_class, FONT_PREVIEW_PROP_WEIGHT,
        g_param_spec_int("weight", NULL, NULL, 0, G_MAXINT, PANGO_WEIGHT_NORMAL,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        obj_class, FONT_PREVIEW_PROP_STYLE,
        g_param_spec_int("style", NULL, NULL, (gint)PANGO_STYLE_NORMAL,
                         (gint)PANGO_STYLE_ITALIC, (gint)PANGO_STYLE_NORMAL,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static GtkCellRenderer* font_preview_cell_renderer_new(void) {
    return GTK_CELL_RENDERER(g_object_new(FONT_PREVIEW_TYPE_CELL_RENDERER, NULL));
}

/* -----------------------------------------------------------------------
 * Font type detection via FreeType
 * --------------------------------------------------------------------- */

typedef enum {
    FONT_TYPE_TRUETYPE = 0,
    FONT_TYPE_OPENTYPE
} FontOutlineType;

/**
 * Detect whether a font file contains TrueType or OpenType (CFF) outlines.
 * Returns FONT_TYPE_OPENTYPE if CFF, FONT_TYPE_TRUETYPE otherwise.
 */
static FontOutlineType detect_font_type(FT_Library ft_lib, const char* filepath) {
    if (!ft_lib || !filepath || filepath[0] == '\0')
        return FONT_TYPE_TRUETYPE;

    FT_Face face = NULL;
    FT_Error err = FT_New_Face(ft_lib, filepath, 0, &face);
    if (err || !face)
        return FONT_TYPE_TRUETYPE;

    FontOutlineType type = FONT_TYPE_TRUETYPE;

    if (face->face_flags & FT_FACE_FLAG_SFNT) {
        PS_FontInfoRec info;
        if (FT_Get_PS_Font_Info(face, &info) == 0) {
            type = FONT_TYPE_OPENTYPE;
        }
    }

    FT_Done_Face(face);
    return type;
}

/**
 * Load scaled icon from GResource; on failure log and return a solid placeholder
 * so the type column still occupies space.
 */
static GdkPixbuf* font_chooser_load_type_pixbuf(const gchar* resource_path,
                                                guint32 gdk_pixbuf_fill_pixel) {
    GError* err = NULL;
    /*
     * Use new_from_resource + scale: new_from_resource_at_scale can report
     * "Unrecognized image file format" for resources built with to-pixdata,
     * and matches how other icons are loaded elsewhere in the app.
     */
    GdkPixbuf* loaded = gdk_pixbuf_new_from_resource(resource_path, &err);
    GdkPixbuf* pb = NULL;
    if (loaded) {
        pb = gdk_pixbuf_scale_simple(loaded, (int)FONT_ICON_SIZE, (int)FONT_ICON_SIZE,
                                     GDK_INTERP_BILINEAR);
        g_object_unref(loaded);
        if (pb)
            return pb;
    }
    if (err) {
        g_warning("font chooser: failed to load %s: %s", resource_path, err->message);
        g_error_free(err);
    }
    pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, FONT_ICON_SIZE, FONT_ICON_SIZE);
    if (pb)
        gdk_pixbuf_fill(pb, gdk_pixbuf_fill_pixel);
    return pb;
}

/**
 * Resolve a font family name to a file path using fontconfig.
 * Returns a newly allocated string or NULL.
 */
static gchar* resolve_font_path(const gchar* family, int weight, int slant) {
    FcPattern* pattern = FcPatternCreate();
    if (!pattern)
        return NULL;

    FcPatternAddString(pattern, FC_FAMILY, (const FcChar8*)family);
    FcPatternAddInteger(pattern, FC_WEIGHT, weight);
    FcPatternAddInteger(pattern, FC_SLANT, slant);

    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result;
    FcPattern* match = FcFontMatch(NULL, pattern, &result);
    FcPatternDestroy(pattern);

    if (!match)
        return NULL;

    FcChar8* file = NULL;
    if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch || !file) {
        FcPatternDestroy(match);
        return NULL;
    }

    gchar* path = g_strdup((const gchar*)file);
    FcPatternDestroy(match);
    return path;
}

/* -----------------------------------------------------------------------
 * GObject boilerplate
 * --------------------------------------------------------------------- */

static void font_chooser_widget_class_init(FontChooserWidgetClass* klass) {
    GObjectClass* obj_class = G_OBJECT_CLASS(klass);
    obj_class->finalize = font_chooser_widget_finalize;

    /*
     * Marshaller must match parameters; NULL marshaller breaks INTEGER args on
     * many platforms so weight/style arrive wrong and Bold/Italic never sync.
     */
    font_chooser_signals[SIGNAL_FONT_CHANGED] = g_signal_new(
        "font-changed",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        0, NULL, NULL,
        g_cclosure_marshal_generic,
        G_TYPE_NONE, 4,
        G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_BOOLEAN);
}

static void font_chooser_widget_init(FontChooserWidget* self) {
    self->current_family = g_strdup("Sans");
    self->ft_library = NULL;
    self->truetype_icon = NULL;
    self->opentype_icon = NULL;
    self->store = NULL;
    self->filter_model = NULL;
    self->font_list_scrolled = NULL;

    FT_Init_FreeType(&self->ft_library);

    /* Load icons from GResource (solid fallback if missing / failed compile embed) */
    self->truetype_icon =
        font_chooser_load_type_pixbuf("/icons/truetype.png", 0xff6080b0u);
    self->opentype_icon =
        font_chooser_load_type_pixbuf("/icons/opentype.png", 0xff50a060u);

    /* Button showing current font */
    gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_HORIZONTAL);

    self->button = gtk_button_new();
    gtk_widget_set_size_request(self->button, 140, -1);
    self->button_label = gtk_label_new("Sans");
    gtk_label_set_xalign(GTK_LABEL(self->button_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(self->button_label), PANGO_ELLIPSIZE_END);
    gtk_container_add(GTK_CONTAINER(self->button), self->button_label);
    gtk_box_pack_start(GTK_BOX(self), self->button, FALSE, TRUE, 0);
    gtk_widget_set_tooltip_text(self->button, "Font family");

    g_signal_connect(self->button, "clicked", G_CALLBACK(on_button_clicked), self);

    /* Popover */
    self->popover = gtk_popover_new(self->button);
    gtk_popover_set_position(GTK_POPOVER(self->popover), GTK_POS_BOTTOM);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(vbox, POPOVER_MIN_WIDTH, POPOVER_HEIGHT);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
    gtk_container_add(GTK_CONTAINER(self->popover), vbox);

    /* Search entry */
    self->search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(self->search_entry), "Search fonts...");
    gtk_box_pack_start(GTK_BOX(vbox), self->search_entry, FALSE, FALSE, 0);

    /* Scrolled window with tree view */
    self->font_list_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->font_list_scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
#if GTK_CHECK_VERSION(3, 16, 0)
    /* Overlay scrollbars can widen on hover without reliable layout reserve */
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(self->font_list_scrolled),
                                              FALSE);
#endif
#if GTK_CHECK_VERSION(3, 22, 0)
    gtk_scrolled_window_set_propagate_natural_width(
        GTK_SCROLLED_WINDOW(self->font_list_scrolled), TRUE);
#endif
    gtk_widget_set_vexpand(self->font_list_scrolled, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), self->font_list_scrolled, TRUE, TRUE, 0);

    /* Tree store */
    self->store = gtk_tree_store_new(NUM_COLUMNS,
                                     G_TYPE_STRING,   /* COL_DISPLAY_NAME */
                                     G_TYPE_STRING,   /* COL_FONT_DESC_STR */
                                     GDK_TYPE_PIXBUF, /* COL_ICON_PIXBUF */
                                     G_TYPE_STRING,   /* COL_FAMILY_NAME */
                                     G_TYPE_INT,      /* COL_WEIGHT */
                                     G_TYPE_INT,      /* COL_STYLE */
                                     G_TYPE_BOOLEAN); /* COL_IS_FAMILY_ROOT */

    /* Filter model */
    self->filter_model = gtk_tree_model_filter_new(GTK_TREE_MODEL(self->store), NULL);
    gtk_tree_model_filter_set_visible_func(
        GTK_TREE_MODEL_FILTER(self->filter_model), filter_visible_func, self, NULL);

    /* Tree view */
    self->tree_view = gtk_tree_view_new_with_model(self->filter_model);
    gtk_widget_set_size_request(self->tree_view, 380, -1);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(self->tree_view), FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(self->tree_view), FALSE);
#if GTK_CHECK_VERSION(3, 14, 0)
    gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(self->tree_view), TRUE);
#endif

    /* Column 1: Family / face name */
    GtkCellRenderer* name_renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* name_col = gtk_tree_view_column_new_with_attributes(
        "", name_renderer, "text", COL_DISPLAY_NAME, NULL);
    gtk_tree_view_column_set_expand(name_col, FALSE);
    gtk_tree_view_column_set_min_width(name_col, 120);
    gtk_tree_view_append_column(GTK_TREE_VIEW(self->tree_view), name_col);

    /* Column 2: Font preview (custom renderer — no GtkCellRendererText font-desc merge) */
    GtkCellRenderer* preview_renderer = font_preview_cell_renderer_new();
    GtkTreeViewColumn* preview_col = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(preview_col, preview_renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(preview_col, preview_renderer,
                                            preview_cell_data_func, NULL, NULL);
    gtk_tree_view_column_set_expand(preview_col, TRUE);
    gtk_tree_view_column_set_min_width(preview_col, 80);
    gtk_tree_view_append_column(GTK_TREE_VIEW(self->tree_view), preview_col);

    /* Column 3: Type icon */
    GtkCellRenderer* icon_renderer = gtk_cell_renderer_pixbuf_new();
    g_object_set(icon_renderer, "xalign", 1.0, NULL);
    GtkTreeViewColumn* icon_col = gtk_tree_view_column_new_with_attributes(
        "", icon_renderer, "pixbuf", COL_ICON_PIXBUF, NULL);
    gtk_tree_view_column_set_expand(icon_col, FALSE);
    gtk_tree_view_column_set_fixed_width(icon_col, FONT_ICON_SIZE + ICON_COL_PADDING);
    gtk_tree_view_column_set_min_width(icon_col, FONT_ICON_SIZE + ICON_COL_PADDING);
    gtk_tree_view_column_set_resizable(icon_col, FALSE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(self->tree_view), icon_col);

    /* Small inset so the last column does not sit under the vscrollbar */
#if GTK_CHECK_VERSION(3, 12, 0)
    gtk_widget_set_margin_end(self->tree_view, 10);
#else
    gtk_widget_set_margin_right(self->tree_view, 10);
#endif

    gtk_container_add(GTK_CONTAINER(self->font_list_scrolled), self->tree_view);

    {
        GtkWidget* vsb = gtk_scrolled_window_get_vscrollbar(
            GTK_SCROLLED_WINDOW(self->font_list_scrolled));
        if (vsb)
            g_signal_connect(vsb, "size-allocate",
                             G_CALLBACK(on_font_list_vscrollbar_size_allocate), self);
    }

    g_signal_connect(self->tree_view, "row-activated",
                     G_CALLBACK(on_row_activated), self);
    g_signal_connect(self->search_entry, "search-changed",
                     G_CALLBACK(on_search_changed), self);

    gtk_widget_show_all(vbox);

    /* Populate font list */
    font_chooser_widget_populate(self);
    font_chooser_sync_popover_width(self);
}

static void font_chooser_widget_finalize(GObject* object) {
    FontChooserWidget* self = (FontChooserWidget*)object;

    g_free(self->current_family);
    self->current_family = NULL;

    if (self->truetype_icon)
        g_object_unref(self->truetype_icon);
    if (self->opentype_icon)
        g_object_unref(self->opentype_icon);
    if (self->filter_model)
        g_object_unref(self->filter_model);
    if (self->store)
        g_object_unref(self->store);
    if (self->ft_library)
        FT_Done_FreeType(self->ft_library);

    G_OBJECT_CLASS(font_chooser_widget_parent_class)->finalize(object);
}

/* -----------------------------------------------------------------------
 * Font enumeration and tree population
 * --------------------------------------------------------------------- */

static void on_font_list_vscrollbar_size_allocate(GtkWidget* widget, GdkRectangle* alloc,
                                                  gpointer user_data) {
    (void)widget;
    (void)alloc;
    FontChooserWidget* self = FONT_CHOOSER_WIDGET(user_data);
    g_idle_add(idle_font_chooser_sync_popover_width, self);
}

static void font_chooser_sync_popover_width(FontChooserWidget* self) {
    GtkWidget* vbox;
    gint tv_min = 0;
    gint tv_nat = 0;
    gint smin = 0;
    gint snat = 0;
    gint w;

    if (!self->popover || !self->tree_view)
        return;

    vbox = gtk_bin_get_child(GTK_BIN(self->popover));
    if (!vbox)
        return;

    /* Do not call gtk_tree_view_columns_autosize here: it fixes column widths and
     * prevents the preview column from expanding, which clips markup text. */

    gtk_widget_get_preferred_width(self->tree_view, &tv_min, &tv_nat);

    if (self->search_entry)
        gtk_widget_get_preferred_width(self->search_entry, &smin, &snat);

    w = MAX(tv_nat, snat);
    w += 2 * (gint)gtk_container_get_border_width(GTK_CONTAINER(vbox));
    /* Tree margin_end + vertical scrollbar width so column 3 stays visible */
#if GTK_CHECK_VERSION(3, 12, 0)
    w += gtk_widget_get_margin_end(self->tree_view);
#else
    w += gtk_widget_get_margin_right(self->tree_view);
#endif
    {
        gint sb_reserve = SCROLLBAR_MIN_RESERVE;
        if (self->font_list_scrolled) {
            GtkWidget* sb = gtk_scrolled_window_get_vscrollbar(
                GTK_SCROLLED_WINDOW(self->font_list_scrolled));
            if (sb) {
                gint sbmin = 0, sbnat = 0;
                gtk_widget_get_preferred_width(sb, &sbmin, &sbnat);
                sb_reserve = MAX(sb_reserve, sbnat);
                /* Hovered scrollbars often grow allocation without updating preferred width */
                if (gtk_widget_get_visible(sb) && gtk_widget_get_realized(sb)) {
                    gint aw = gtk_widget_get_allocated_width(sb);
                    sb_reserve = MAX(sb_reserve, aw);
                }
            }
        }
        w += sb_reserve;
    }

    if (w < POPOVER_MIN_WIDTH)
        w = POPOVER_MIN_WIDTH;
    if (w > POPOVER_MAX_WIDTH)
        w = POPOVER_MAX_WIDTH;

    gtk_widget_set_size_request(vbox, w, POPOVER_HEIGHT);
}

static gboolean idle_font_chooser_sync_popover_width(gpointer user_data) {
    font_chooser_sync_popover_width(FONT_CHOOSER_WIDGET(user_data));
    return G_SOURCE_REMOVE;
}

static int compare_family_names(const void* a, const void* b) {
    PangoFontFamily* fa = *(PangoFontFamily**)a;
    PangoFontFamily* fb = *(PangoFontFamily**)b;
    return g_ascii_strcasecmp(pango_font_family_get_name(fa),
                              pango_font_family_get_name(fb));
}

static void font_chooser_widget_populate(FontChooserWidget* self) {
    PangoFontMap* font_map = pango_cairo_font_map_get_default();
    PangoFontFamily** families = NULL;
    int n_families = 0;

    pango_font_map_list_families(font_map, &families, &n_families);

    /* Sort families alphabetically */
    qsort(families, n_families, sizeof(PangoFontFamily*), compare_family_names);

    for (int fi = 0; fi < n_families; fi++) {
        PangoFontFamily* family = families[fi];
        const gchar* family_name = pango_font_family_get_name(family);

        PangoFontFace** faces = NULL;
        int n_faces = 0;
        pango_font_family_list_faces(family, &faces, &n_faces);

        /* Detect font type for this family (use first face) */
        FontOutlineType font_type = FONT_TYPE_TRUETYPE;
        gchar* font_path = resolve_font_path(family_name, FC_WEIGHT_REGULAR, FC_SLANT_ROMAN);
        if (font_path) {
            font_type = detect_font_type(self->ft_library, font_path);
            g_free(font_path);
        }

        GdkPixbuf* icon = (font_type == FONT_TYPE_OPENTYPE)
                              ? self->opentype_icon
                              : self->truetype_icon;

        /* Display name with face count */
        gchar* display_name = g_strdup_printf("%s (%d)", family_name, n_faces);

        /* Family root row */
        GtkTreeIter parent_iter;
        gtk_tree_store_append(self->store, &parent_iter, NULL);
        gtk_tree_store_set(self->store, &parent_iter,
                           COL_DISPLAY_NAME, display_name,
                           COL_FONT_DESC_STR, family_name,
                           COL_ICON_PIXBUF, icon,
                           COL_FAMILY_NAME, family_name,
                           COL_WEIGHT, 0,
                           COL_STYLE, 0,
                           COL_IS_FAMILY_ROOT, TRUE,
                           -1);
        g_free(display_name);

        /* Face children */
        for (int fj = 0; fj < n_faces; fj++) {
            PangoFontFace* face = faces[fj];
            const gchar* face_name = pango_font_face_get_face_name(face);

            PangoFontDescription* desc = pango_font_face_describe(face);
            PangoWeight weight = pango_font_description_get_weight(desc);
            PangoStyle style = pango_font_description_get_style(desc);

            gchar* desc_str = pango_font_description_to_string(desc);
            pango_font_description_free(desc);

            GtkTreeIter child_iter;
            gtk_tree_store_append(self->store, &child_iter, &parent_iter);
            gtk_tree_store_set(self->store, &child_iter,
                               COL_DISPLAY_NAME, face_name,
                               COL_FONT_DESC_STR, desc_str,
                               COL_ICON_PIXBUF, icon,
                               COL_FAMILY_NAME, family_name,
                               COL_WEIGHT, (gint)weight,
                               COL_STYLE, (gint)style,
                               COL_IS_FAMILY_ROOT, FALSE,
                               -1);
            g_free(desc_str);
        }

        g_free(faces);
    }

    g_free(families);
}

/* -----------------------------------------------------------------------
 * Cell data function for font preview column
 * --------------------------------------------------------------------- */

static void preview_cell_data_func(GtkTreeViewColumn* col, GtkCellRenderer* cell,
                                   GtkTreeModel* model, GtkTreeIter* iter,
                                   gpointer data) {
    (void)col;
    (void)data;

    gchar* display_name = NULL;
    gchar* family_name = NULL;
    gint weight = 0;
    gint style = 0;
    gboolean is_root = FALSE;

    gtk_tree_model_get(model, iter,
                       COL_DISPLAY_NAME, &display_name,
                       COL_FAMILY_NAME, &family_name,
                       COL_WEIGHT, &weight,
                       COL_STYLE, &style,
                       COL_IS_FAMILY_ROOT, &is_root,
                       -1);

    const gchar* fam =
        (family_name && family_name[0]) ? family_name : "Sans";
    const gchar* preview_plain =
        is_root
            ? fam
            : ((display_name && display_name[0]) ? display_name : fam);

    /*
     * FontPreviewCellRenderer: set GObject properties (see font_preview_cell_renderer_new).
     */
    PangoWeight pw = PANGO_WEIGHT_NORMAL;
    PangoStyle pstyle = PANGO_STYLE_NORMAL;
    if (!is_root) {
        pw = (weight > 0) ? (PangoWeight)weight : PANGO_WEIGHT_NORMAL;
        pstyle = (PangoStyle)style;
    }

    g_object_set(cell,
                 "family", fam,
                 "preview-text", preview_plain,
                 "weight", (gint)pw,
                 "style", (gint)pstyle,
                 NULL);

    g_free(display_name);
    g_free(family_name);
}

/* -----------------------------------------------------------------------
 * Filter function for search
 * --------------------------------------------------------------------- */

static gboolean filter_visible_func(GtkTreeModel* model, GtkTreeIter* iter,
                                    gpointer data) {
    FontChooserWidget* self = (FontChooserWidget*)data;
    const gchar* search_text = gtk_entry_get_text(GTK_ENTRY(self->search_entry));

    if (!search_text || search_text[0] == '\0')
        return TRUE;

    gchar* family_name = NULL;
    gboolean is_root = FALSE;
    gtk_tree_model_get(model, iter,
                       COL_FAMILY_NAME, &family_name,
                       COL_IS_FAMILY_ROOT, &is_root,
                       -1);

    gboolean visible = FALSE;

    if (family_name) {
        gchar* lower_family = g_utf8_strdown(family_name, -1);
        gchar* lower_search = g_utf8_strdown(search_text, -1);
        visible = (strstr(lower_family, lower_search) != NULL);
        g_free(lower_family);
        g_free(lower_search);
    }

    /* If this is a child row and its family matches, show it */
    if (!is_root && !visible) {
        GtkTreeIter parent;
        if (gtk_tree_model_iter_parent(model, &parent, iter)) {
            gchar* parent_family = NULL;
            gtk_tree_model_get(model, &parent, COL_FAMILY_NAME, &parent_family, -1);
            if (parent_family) {
                gchar* lower_pf = g_utf8_strdown(parent_family, -1);
                gchar* lower_s = g_utf8_strdown(search_text, -1);
                visible = (strstr(lower_pf, lower_s) != NULL);
                g_free(lower_pf);
                g_free(lower_s);
                g_free(parent_family);
            }
        }
    }

    /* If this is a root and a child matches, show the root */
    if (is_root && !visible) {
        GtkTreeIter child;
        if (gtk_tree_model_iter_children(model, &child, iter)) {
            do {
                gchar* child_name = NULL;
                gtk_tree_model_get(model, &child, COL_DISPLAY_NAME, &child_name, -1);
                if (child_name) {
                    gchar* lower_cn = g_utf8_strdown(child_name, -1);
                    gchar* lower_s = g_utf8_strdown(search_text, -1);
                    if (strstr(lower_cn, lower_s))
                        visible = TRUE;
                    g_free(lower_cn);
                    g_free(lower_s);
                    g_free(child_name);
                }
                if (visible)
                    break;
            } while (gtk_tree_model_iter_next(model, &child));
        }
    }

    g_free(family_name);
    return visible;
}

/* -----------------------------------------------------------------------
 * Signal handlers
 * --------------------------------------------------------------------- */

static void on_button_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    FontChooserWidget* self = (FontChooserWidget*)user_data;
    font_chooser_sync_popover_width(self);
    gtk_widget_show_all(self->popover);
    gtk_popover_popup(GTK_POPOVER(self->popover));
    gtk_widget_grab_focus(self->search_entry);
}

static void on_row_activated(GtkTreeView* tree_view, GtkTreePath* path,
                             GtkTreeViewColumn* column, gpointer user_data) {
    (void)column;
    FontChooserWidget* self = (FontChooserWidget*)user_data;
    GtkTreeModel* model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;

    if (!gtk_tree_model_get_iter(model, &iter, path))
        return;

    gboolean is_root = FALSE;
    gchar* family_name = NULL;
    gint weight = 0;
    gint style = 0;

    gtk_tree_model_get(model, &iter,
                       COL_FAMILY_NAME, &family_name,
                       COL_WEIGHT, &weight,
                       COL_STYLE, &style,
                       COL_IS_FAMILY_ROOT, &is_root,
                       -1);

    g_free(self->current_family);
    self->current_family = g_strdup(family_name);
    gtk_label_set_text(GTK_LABEL(self->button_label), family_name);

    if (is_root)
        g_signal_emit(self, font_chooser_signals[SIGNAL_FONT_CHANGED], 0, family_name,
                      (gint)PANGO_WEIGHT_NORMAL, (gint)PANGO_STYLE_NORMAL, TRUE);
    else
        g_signal_emit(self, font_chooser_signals[SIGNAL_FONT_CHANGED], 0, family_name,
                      weight, style, TRUE);

    gtk_popover_popdown(GTK_POPOVER(self->popover));

    g_free(family_name);
}

static void on_search_changed(GtkSearchEntry* entry, gpointer user_data) {
    (void)entry;
    FontChooserWidget* self = (FontChooserWidget*)user_data;
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(self->filter_model));

    /* Expand all visible rows when searching */
    const gchar* text = gtk_entry_get_text(GTK_ENTRY(self->search_entry));
    if (text && text[0] != '\0')
        gtk_tree_view_expand_all(GTK_TREE_VIEW(self->tree_view));
    else
        gtk_tree_view_collapse_all(GTK_TREE_VIEW(self->tree_view));

    /* Refilter/layout runs after this handler; sync width on next main-loop tick */
    g_idle_add(idle_font_chooser_sync_popover_width, self);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

GtkWidget* font_chooser_widget_new(void) {
    return g_object_new(font_chooser_widget_get_type(), NULL);
}

void font_chooser_widget_set_family(GtkWidget* widget, const gchar* family) {
    g_return_if_fail(widget != NULL);
    g_return_if_fail(IS_FONT_CHOOSER_WIDGET(widget));

    FontChooserWidget* self = FONT_CHOOSER_WIDGET(widget);
    if (!family)
        return;

    g_free(self->current_family);
    self->current_family = g_strdup(family);
    gtk_label_set_text(GTK_LABEL(self->button_label), family);
}

const gchar* font_chooser_widget_get_family(GtkWidget* widget) {
    g_return_val_if_fail(widget != NULL, NULL);
    g_return_val_if_fail(IS_FONT_CHOOSER_WIDGET(widget), NULL);

    FontChooserWidget* self = FONT_CHOOSER_WIDGET(widget);
    return self->current_family;
}

typedef struct {
    const gchar* name;
    gint         weight;
    gint         style;
} FontFaceSortRow;

static int compare_font_face_sort_row(const void* a, const void* b) {
    const FontFaceSortRow* ra = (const FontFaceSortRow*)a;
    const FontFaceSortRow* rb = (const FontFaceSortRow*)b;
    return g_utf8_collate(ra->name, rb->name);
}

GtkWidget* font_chooser_face_combo_new(void) {
    GtkListStore* store = gtk_list_store_new(
        3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
    GtkWidget* combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    GtkCellRenderer* cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, TRUE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(combo), cell, "text",
                                  FONT_FACE_COMBO_COL_LABEL);
    gtk_widget_set_valign(combo, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(combo, 140, -1);
    return combo;
}

void font_chooser_face_combo_fill(GtkComboBox* combo, const gchar* family) {
    g_return_if_fail(GTK_IS_COMBO_BOX(combo));
    GtkTreeModel* model = gtk_combo_box_get_model(combo);
    g_return_if_fail(GTK_IS_LIST_STORE(model));

    GtkListStore* store = GTK_LIST_STORE(model);
    gtk_list_store_clear(store);

    if (!family || !family[0])
        return;

    PangoFontMap* font_map = pango_cairo_font_map_get_default();
    PangoFontFamily** families = NULL;
    int n_families = 0;
    pango_font_map_list_families(font_map, &families, &n_families);

    PangoFontFamily* match = NULL;
    for (int i = 0; i < n_families; i++) {
        if (!g_ascii_strcasecmp(pango_font_family_get_name(families[i]), family)) {
            match = families[i];
            break;
        }
    }

    if (!match) {
        g_free(families);
        return;
    }

    PangoFontFace** faces = NULL;
    int n_faces = 0;
    pango_font_family_list_faces(match, &faces, &n_faces);

    FontFaceSortRow* rows = NULL;
    if (n_faces > 0)
        rows = g_new(FontFaceSortRow, n_faces);

    for (int j = 0; j < n_faces; j++) {
        PangoFontDescription* desc = pango_font_face_describe(faces[j]);
        rows[j].name = pango_font_face_get_face_name(faces[j]);
        rows[j].weight = (gint)pango_font_description_get_weight(desc);
        rows[j].style = (gint)pango_font_description_get_style(desc);
        pango_font_description_free(desc);
    }

    if (n_faces > 1)
        qsort(rows, (size_t)n_faces, sizeof(FontFaceSortRow), compare_font_face_sort_row);

    for (int j = 0; j < n_faces; j++) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           FONT_FACE_COMBO_COL_LABEL, rows[j].name,
                           FONT_FACE_COMBO_COL_WEIGHT, rows[j].weight,
                           FONT_FACE_COMBO_COL_STYLE, rows[j].style,
                           -1);
    }

    g_free(rows);
    g_free(faces);
    g_free(families);
}

gboolean font_chooser_face_combo_select(GtkComboBox* combo, gint weight, gint style) {
    g_return_val_if_fail(GTK_IS_COMBO_BOX(combo), FALSE);
    GtkTreeModel* model = gtk_combo_box_get_model(combo);
    if (!model)
        return FALSE;

    if (gtk_tree_model_iter_n_children(model, NULL) == 0) {
        gtk_combo_box_set_active(combo, -1);
        return FALSE;
    }

    GtkTreeIter iter;
    gint idx = 0;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        gint w, s;
        gtk_tree_model_get(model, &iter,
                           FONT_FACE_COMBO_COL_WEIGHT, &w,
                           FONT_FACE_COMBO_COL_STYLE, &s,
                           -1);
        if (w == weight && s == style) {
            gtk_combo_box_set_active(combo, idx);
            return TRUE;
        }
        valid = gtk_tree_model_iter_next(model, &iter);
        idx++;
    }

    gtk_combo_box_set_active(combo, 0);
    return TRUE;
}
