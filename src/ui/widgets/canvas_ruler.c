#include "ui/widgets/canvas_ruler.h"
#include "document.h"
#include "ui/ruler_units.h"
#include <cairo.h>
#include <gdk/gdk.h>
#include <math.h>

#define RULER_SIZE_PX 24
/** Unit label text size in points */
#define RULER_LABEL_FONT_SIZE 11.0
/** Minimum viewport pixels between major ticks (avoids label overlap) */
#define MIN_MAJOR_SPACING_VIEWPORT 48.0
/** Tick line lengths (from ruler edge inward); major ticks use full ruler height/width */
#define TICK_MEDIUM_LEN 6.0
#define TICK_MINOR_LEN 3.0
/** Default colors (RGB 0–255) */
#define RULER_BG_R (237.0 / 255.0)
#define RULER_BG_G (237.0 / 255.0)
#define RULER_BG_B (242.0 / 255.0)
#define RULER_FG_R (146.0 / 255.0)
#define RULER_FG_G (146.0 / 255.0)
#define RULER_FG_B (146.0 / 255.0)
#define RULER_TICK_LINE_WIDTH 1.0
/** for invalid mouse position */
#define RULER_MOUSE_INVALID (-1e9)

G_DEFINE_TYPE(CanvasRuler, canvas_ruler, GTK_TYPE_DRAWING_AREA)

static gboolean canvas_ruler_draw(GtkWidget* widget, cairo_t* cr);
static void on_adjustment_value_changed(GtkAdjustment* adj, gpointer user_data);

static void canvas_ruler_init(CanvasRuler* self) {
    self->orientation = CANVAS_RULER_HORIZONTAL;
    self->document = NULL;

    gtk_widget_set_size_request(GTK_WIDGET(self),
                                self->orientation == CANVAS_RULER_HORIZONTAL ? -1 : RULER_SIZE_PX,
                                self->orientation == CANVAS_RULER_HORIZONTAL ? RULER_SIZE_PX : -1);
}

static void canvas_ruler_class_init(CanvasRulerClass* klass) {
    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->draw = canvas_ruler_draw;
}

/**
 * Round to a "nice" step for ruler ticks (1, 2, 5, 10, 20, 50, 100, ...).
 * Keeps tick density readable across zoom levels.
 */
static int nice_step_canvas(gdouble desired_step_canvas) {
    const int nice[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
    const int n = (int)(sizeof(nice) / sizeof(nice[0]));
    int i;
    if (desired_step_canvas <= 0)
        return 1;
    if (desired_step_canvas >= nice[n - 1])
        return nice[n - 1];
    for (i = 0; i < n - 1; i++) {
        if (desired_step_canvas <= (gdouble)(nice[i] + nice[i + 1]) / 2.0)
            return nice[i];
    }
    return nice[n - 1];
}

/**
 * Get canvas origin in viewport/ruler pixels.
 * Ruler 0 is at the left/top edge of the canvas; positions left of or above
 * that edge are negative. We account for scroll (hadj/vadj) and for the
 * drawing area's position within the viewport (e.g. when centered).
 */
static void get_canvas_origin(ImageDocument* doc, gdouble* out_origin_x, gdouble* out_origin_y) {
    gdouble ox = 0.0, oy = 0.0;
    if (doc && doc->scrolled_window && GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        if (hadj)
            ox = -gtk_adjustment_get_value(hadj);
        if (vadj)
            oy = -gtk_adjustment_get_value(vadj);
        /* Add drawing area offset within viewport so 0 aligns with canvas edge (e.g. when canvas is centered). */
        if (doc->drawing_area && gtk_widget_get_visible(doc->drawing_area)) {
            GtkAllocation alloc;
            gtk_widget_get_allocation(doc->drawing_area, &alloc);
            ox += (gdouble)alloc.x;
            oy += (gdouble)alloc.y;
        }
    }
    if (out_origin_x)
        *out_origin_x = ox;
    if (out_origin_y)
        *out_origin_y = oy;
}

static gboolean canvas_ruler_draw(GtkWidget* widget, cairo_t* cr) {
    CanvasRuler* ruler = CANVAS_RULER(widget);
    gint w = gtk_widget_get_allocated_width(widget);
    gint h = gtk_widget_get_allocated_height(widget);
    ImageDocument* doc = (ImageDocument*)ruler->document;
    gdouble canvas_origin_x, canvas_origin_y;
    gdouble zoom;

    /* Ruler background: rgb(237, 237, 242) */
    cairo_set_source_rgb(cr, RULER_BG_R, RULER_BG_G, RULER_BG_B);
    cairo_rectangle(cr, 0, 0, (gdouble)w, (gdouble)h);
    cairo_fill(cr);

    if (!doc) {
        return FALSE;
    }

    get_canvas_origin(doc, &canvas_origin_x, &canvas_origin_y);
    zoom = doc->zoom_factor;
    if (zoom <= 0.0)
        zoom = 1.0;

    /* Horizontal ruler spans full width including vertical ruler; offset so 0 aligns with canvas left edge */
    if (ruler->orientation == CANVAS_RULER_HORIZONTAL)
        canvas_origin_x += (gdouble)RULER_SIZE_PX;

    /* Scale spacing by zoom: at small zoom use larger steps to avoid clutter */
    {
        int major_step_canvas = nice_step_canvas(MIN_MAJOR_SPACING_VIEWPORT / zoom);
        if (major_step_canvas < 1)
            major_step_canvas = 1;
        int medium_step_canvas = (major_step_canvas >= 2) ? (major_step_canvas / 2) : 1;
        int minor_step_canvas = (major_step_canvas >= 10) ? (major_step_canvas / 10) : ((major_step_canvas >= 5) ? 1 : 1);
        if (minor_step_canvas < 1)
            minor_step_canvas = 1;

        RulerUnit unit = document_get_ruler_unit(doc);
        gdouble dpi = document_get_ruler_dpi(doc);
        guint extent_h = (guint)(doc->width > 0 ? doc->width : 1);
        guint extent_v = (guint)(doc->height > 0 ? doc->height : 1);
        cairo_set_line_width(cr, RULER_TICK_LINE_WIDTH);
        cairo_set_source_rgb(cr, RULER_FG_R, RULER_FG_G, RULER_FG_B);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE); /* sharp 1px tick marks */
        cairo_set_font_size(cr, RULER_LABEL_FONT_SIZE);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

        if (ruler->orientation == CANVAS_RULER_HORIZONTAL) {
            gdouble x;
            int canvas_val;
            int first_minor = (int)floor((-1.0 - canvas_origin_x) / zoom / (gdouble)minor_step_canvas) * minor_step_canvas;
            int first_medium = (int)floor((-1.0 - canvas_origin_x) / zoom / (gdouble)medium_step_canvas) * medium_step_canvas;
            int first_major = (int)floor((-1.0 - canvas_origin_x) / zoom / (gdouble)major_step_canvas) * major_step_canvas;

            /* Minor ticks: short lines */
            for (canvas_val = first_minor;; canvas_val += minor_step_canvas) {
                x = canvas_origin_x + canvas_val * zoom;
                if (x > (gdouble)w + 1.0)
                    break;
                if (x < -1.0)
                    continue;
                cairo_move_to(cr, x, (gdouble)h);
                cairo_line_to(cr, x, (gdouble)h - TICK_MINOR_LEN);
                cairo_stroke(cr);
            }
            /* Medium ticks: medium lines */
            for (canvas_val = first_medium;; canvas_val += medium_step_canvas) {
                x = canvas_origin_x + canvas_val * zoom;
                if (x > (gdouble)w + 1.0)
                    break;
                if (x < -1.0)
                    continue;
                cairo_move_to(cr, x, (gdouble)h);
                cairo_line_to(cr, x, (gdouble)h - TICK_MEDIUM_LEN);
                cairo_stroke(cr);
            }
            /* Major ticks: full height */
            for (canvas_val = first_major;; canvas_val += major_step_canvas) {
                x = canvas_origin_x + canvas_val * zoom;
                if (x > (gdouble)w + 1.0)
                    break;
                if (x < -1.0)
                    continue;
                cairo_move_to(cr, x, 0);
                cairo_line_to(cr, x, (gdouble)h);
                cairo_stroke(cr);
            }
            /* Unit labels in black (antialias on for text) */
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            for (canvas_val = first_major;; canvas_val += major_step_canvas) {
                gchar* label;
                x = canvas_origin_x + canvas_val * zoom;
                if (x > (gdouble)w + 1.0)
                    break;
                if (x < -1.0)
                    continue;
                label = ruler_units_format_value(
                    ruler_units_pixel_to_value((gdouble)canvas_val, unit, dpi, extent_h), unit);
                cairo_move_to(cr, x + 2.0, (gdouble)h - 12.0);
                cairo_show_text(cr, label);
                g_free(label);
            }
        } else {
            gdouble y;
            int canvas_val;
            int first_minor = (int)floor((-1.0 - canvas_origin_y) / zoom / (gdouble)minor_step_canvas) * minor_step_canvas;
            int first_medium = (int)floor((-1.0 - canvas_origin_y) / zoom / (gdouble)medium_step_canvas) * medium_step_canvas;
            int first_major = (int)floor((-1.0 - canvas_origin_y) / zoom / (gdouble)major_step_canvas) * major_step_canvas;

            /* Minor ticks */
            for (canvas_val = first_minor;; canvas_val += minor_step_canvas) {
                y = canvas_origin_y + canvas_val * zoom;
                if (y > (gdouble)h + 1.0)
                    break;
                if (y < -1.0)
                    continue;
                cairo_move_to(cr, (gdouble)w, y);
                cairo_line_to(cr, (gdouble)w - TICK_MINOR_LEN, y);
                cairo_stroke(cr);
            }
            /* Medium ticks */
            for (canvas_val = first_medium;; canvas_val += medium_step_canvas) {
                y = canvas_origin_y + canvas_val * zoom;
                if (y > (gdouble)h + 1.0)
                    break;
                if (y < -1.0)
                    continue;
                cairo_move_to(cr, (gdouble)w, y);
                cairo_line_to(cr, (gdouble)w - TICK_MEDIUM_LEN, y);
                cairo_stroke(cr);
            }
            /* Major ticks: full width */
            for (canvas_val = first_major;; canvas_val += major_step_canvas) {
                y = canvas_origin_y + canvas_val * zoom;
                if (y > (gdouble)h + 1.0)
                    break;
                if (y < -1.0)
                    continue;
                cairo_move_to(cr, 0, y);
                cairo_line_to(cr, (gdouble)w, y);
                cairo_stroke(cr);
            }
            /* Unit labels in black, rotated -90° (match horizontal: 2px from tick, 12px from edge) */
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            for (canvas_val = first_major;; canvas_val += major_step_canvas) {
                gchar* label;
                y = canvas_origin_y + canvas_val * zoom;
                if (y > (gdouble)h + 1.0)
                    break;
                if (y < -1.0)
                    continue;
                label = ruler_units_format_value(
                    ruler_units_pixel_to_value((gdouble)canvas_val, unit, dpi, extent_v), unit);
                /* Place at right edge (w-8) to avoid left clipping; 2px above tick to avoid overlap */
                cairo_save(cr);
                cairo_translate(cr, (gdouble)w - 8.0, y - 2.0);
                cairo_rotate(cr, -90.0 * 3.14159265358979323846 / 180.0);
                cairo_move_to(cr, 0, 0);
                cairo_show_text(cr, label);
                cairo_restore(cr);
                g_free(label);
            }
        }

        /* 1px line at bottom of ruler (tick mark color) */
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, RULER_FG_R, RULER_FG_G, RULER_FG_B);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, 0, (gdouble)h - 0.5);
        cairo_line_to(cr, (gdouble)w, (gdouble)h - 0.5);
        cairo_stroke(cr);

        /* Mouse position crosshair marker (thin line, tick color) */
        if (doc->mouse_canvas_x >= RULER_MOUSE_INVALID + 1e8 &&
            doc->mouse_canvas_y >= RULER_MOUSE_INVALID + 1e8) {
            gdouble mx, my;
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_source_rgb(cr, RULER_FG_R, RULER_FG_G, RULER_FG_B);
            cairo_set_line_width(cr, 1.0);
            if (ruler->orientation == CANVAS_RULER_HORIZONTAL) {
                mx = canvas_origin_x + doc->mouse_canvas_x * zoom;
                if (mx >= -1.0 && mx <= (gdouble)w + 1.0) {
                    cairo_move_to(cr, mx, 0);
                    cairo_line_to(cr, mx, (gdouble)h);
                    cairo_stroke(cr);
                }
            } else {
                my = canvas_origin_y + doc->mouse_canvas_y * zoom;
                if (my >= -1.0 && my <= (gdouble)h + 1.0) {
                    cairo_move_to(cr, 0, my);
                    cairo_line_to(cr, (gdouble)w, my);
                    cairo_stroke(cr);
                }
            }
        }
    }

    return FALSE;
}

static void on_adjustment_value_changed(GtkAdjustment* adj, gpointer user_data) {
    GtkWidget* ruler_widget = (GtkWidget*)user_data;
    (void)adj;
    if (ruler_widget && gtk_widget_get_visible(ruler_widget)) {
        gtk_widget_queue_draw(ruler_widget);
    }
}

GtkWidget* canvas_ruler_new(CanvasRulerOrientation orientation) {
    CanvasRuler* ruler = g_object_new(CANVAS_RULER_TYPE, NULL);
    ruler->orientation = orientation;

    if (orientation == CANVAS_RULER_HORIZONTAL) {
        gtk_widget_set_size_request(GTK_WIDGET(ruler), -1, RULER_SIZE_PX);
    } else {
        gtk_widget_set_size_request(GTK_WIDGET(ruler), RULER_SIZE_PX, -1);
    }

    return GTK_WIDGET(ruler);
}

void canvas_ruler_set_document(CanvasRuler* ruler, gpointer document) {
    ImageDocument* doc = (ImageDocument*)document;
    if (!CANVAS_IS_RULER(ruler)) {
        return;
    }
    ruler->document = document;
    /* Connect to scroll adjustments so ruler redraws when viewport scrolls (zero stays aligned) */
    if (doc && doc->scrolled_window && GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
        GtkScrolledWindow* sw = GTK_SCROLLED_WINDOW(doc->scrolled_window);
        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(sw);
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(sw);
        GtkWidget* widget = GTK_WIDGET(ruler);
        if (ruler->orientation == CANVAS_RULER_HORIZONTAL && hadj) {
            g_signal_connect(hadj, "value-changed", G_CALLBACK(on_adjustment_value_changed), widget);
        }
        if (ruler->orientation == CANVAS_RULER_VERTICAL && vadj) {
            g_signal_connect(vadj, "value-changed", G_CALLBACK(on_adjustment_value_changed), widget);
        }
    }
    gtk_widget_queue_draw(GTK_WIDGET(ruler));
}

CanvasRulerOrientation canvas_ruler_get_orientation(CanvasRuler* ruler) {
    return ruler ? ruler->orientation : CANVAS_RULER_HORIZONTAL;
}
