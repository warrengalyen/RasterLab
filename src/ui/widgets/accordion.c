#include "ui/widgets/accordion.h"
#include <stdlib.h>
#include <string.h>

/**
 * Accordion section structure
 */
typedef struct {
    GtkWidget* header_button; /* Header/title button */
    GtkWidget* content;       /* Content widget */
    GtkWidget* content_box;   /* Box containing content */
    gboolean expanded;        /* Current state */
    gchar* title;             /* Section title */
} AccordionSectionImpl;

/**
 * Accordion structure
 */
typedef struct _Accordion {
    GtkWidget* main_box; /* Main vertical box */
    GList* sections;     /* List of AccordionSectionImpl* */
    gint section_count;  /* Number of sections */
} AccordionImpl;

/**
 * Event box realize callback - set pointer cursor
 */
static void on_event_box_realize(GtkWidget* widget, gpointer user_data) {
    (void)user_data; /* Unused */

    GdkWindow* window = gtk_widget_get_window(widget);
    if (window) {
        GdkDisplay* display = gdk_window_get_display(window);
        if (display) {
            GdkCursor* cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
            gdk_window_set_cursor(window, cursor);
            g_object_unref(cursor);
        }
    }
}

/**
 * Header event box click callback
 */
static gboolean on_accordion_header_clicked(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    AccordionSectionImpl* section = (AccordionSectionImpl*)user_data;

    if (!section || event->type != GDK_BUTTON_PRESS) {
        return FALSE;
    }

    /* Toggle expanded state */
    section->expanded = !section->expanded;

    /* Update caret icon rotation */
    GtkWidget* caret_image = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "caret_image"));
    GdkPixbuf* chevron_pixbuf = GDK_PIXBUF(g_object_get_data(G_OBJECT(widget), "chevron_pixbuf"));

    if (caret_image && GTK_IS_IMAGE(caret_image) && chevron_pixbuf) {
        /* Rotate pixbuf 90 degrees clockwise when collapsed (from down to right) */
        GdkPixbuf* rotated = section->expanded ? gdk_pixbuf_copy(chevron_pixbuf) : gdk_pixbuf_rotate_simple(chevron_pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);

        gtk_image_set_from_pixbuf(GTK_IMAGE(caret_image), rotated);

        if (!section->expanded) {
            g_object_unref(rotated);
        }
    }

    /* Show/hide content */
    if (section->expanded) {
        gtk_widget_show(section->content_box);
    } else {
        gtk_widget_hide(section->content_box);
    }

    return TRUE;
}

/**
 * Create a new accordion widget
 */
Accordion* accordion_new(void) {
    AccordionImpl* accordion = (AccordionImpl*)g_malloc(sizeof(AccordionImpl));

    /* Create main vertical box */
    accordion->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(accordion->main_box, 0);
    gtk_widget_set_margin_start(accordion->main_box, 0);
    gtk_widget_set_margin_end(accordion->main_box, 0);
    gtk_widget_set_margin_bottom(accordion->main_box, 0);

    /* Make accordion expand to fill available space */
    gtk_widget_set_vexpand(accordion->main_box, TRUE);
    gtk_widget_set_hexpand(accordion->main_box, TRUE);

    accordion->sections = NULL;
    accordion->section_count = 0;

    return (Accordion*)accordion;
}

/**
 * Add a new section to the accordion
 */
AccordionSection* accordion_add_section(Accordion* accordion,
                                        const gchar* title,
                                        GtkWidget* content) {
    AccordionImpl* acc = (AccordionImpl*)accordion;

    if (!acc || !title || !content) {
        return NULL;
    }

    /* Create section structure */
    AccordionSectionImpl* section = (AccordionSectionImpl*)g_malloc(sizeof(AccordionSectionImpl));
    section->title = g_strdup(title);
    section->expanded = TRUE; /* Default to expanded */
    section->content = content;

    /* Create header box with full width */
    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(header_box, 8);
    gtk_widget_set_margin_bottom(header_box, 8);
    gtk_widget_set_margin_start(header_box, 8);
    gtk_widget_set_margin_end(header_box, 8);

    /* Create title label */
    GtkWidget* title_label = gtk_label_new(title);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_label_set_markup(GTK_LABEL(title_label), g_strdup_printf("<b>%s</b>", title));
    gtk_box_pack_start(GTK_BOX(header_box), title_label, TRUE, TRUE, 0);

    /* Create caret icon image (chevron.png from resources, scaled to 16x16) */
    GdkPixbuf* chevron_pixbuf = gdk_pixbuf_new_from_resource("/icons/chevron.png", NULL);
    if (chevron_pixbuf) {
        GdkPixbuf* scaled_pixbuf = gdk_pixbuf_scale_simple(chevron_pixbuf, 16, 16,
                                                           GDK_INTERP_BILINEAR);
        GtkWidget* caret_image = gtk_image_new_from_pixbuf(scaled_pixbuf);
        gtk_widget_set_halign(caret_image, GTK_ALIGN_END);
        gtk_box_pack_end(GTK_BOX(header_box), caret_image, FALSE, FALSE, 0);

        /* Store pixbuf and image references for later rotation */
        g_object_set_data(G_OBJECT(header_box), "caret_image", caret_image);
        g_object_set_data_full(G_OBJECT(header_box), "chevron_pixbuf", scaled_pixbuf,
                               (GDestroyNotify)g_object_unref);
        g_object_unref(chevron_pixbuf);
    } else {
        g_warning("Failed to load chevron.png from resources");
        GtkWidget* caret_image = gtk_label_new("▼");
        gtk_widget_set_halign(caret_image, GTK_ALIGN_END);
        gtk_box_pack_end(GTK_BOX(header_box), caret_image, FALSE, FALSE, 0);
        g_object_set_data(G_OBJECT(header_box), "caret_image", caret_image);
    }

    /* Create event box to make entire header clickable */
    GtkWidget* event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(event_box), header_box);
    gtk_widget_set_can_focus(event_box, TRUE);

    /* Add background styling to header */
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
                                    "eventbox { background-color: #f0f0f0; border-bottom: 1px solid #d0d0d0; }",
                                    -1, NULL);
    GtkStyleContext* context = gtk_widget_get_style_context(event_box);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* Set pointer cursor on event box when realized */
    g_signal_connect(event_box, "realize", G_CALLBACK(on_event_box_realize), NULL);

    /* Store data on event_box (which receives the click events) */
    g_object_set_data(G_OBJECT(event_box), "caret_image",
                      g_object_get_data(G_OBJECT(header_box), "caret_image"));
    g_object_set_data(G_OBJECT(event_box), "chevron_pixbuf",
                      g_object_get_data(G_OBJECT(header_box), "chevron_pixbuf"));
    g_object_set_data(G_OBJECT(event_box), "section", section);

    /* Store header widgets in section */
    section->header_button = event_box;

    /* Connect event box click */
    g_signal_connect(event_box, "button-press-event",
                     G_CALLBACK(on_accordion_header_clicked), section);

    /* Create content box */
    section->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(section->content_box, 0);
    gtk_widget_set_margin_start(section->content_box, 10);
    gtk_widget_set_margin_end(section->content_box, 0);
    gtk_widget_set_margin_bottom(section->content_box, 5);

    /* Check if content widget wants to expand vertically */
    gboolean content_vexpand = gtk_widget_get_vexpand(content);

    /* Set content box expansion based on content widget */
    gtk_widget_set_vexpand(section->content_box, content_vexpand);
    gtk_widget_set_hexpand(section->content_box, TRUE);

    /* Pack content with appropriate expansion settings */
    gtk_box_pack_start(GTK_BOX(section->content_box), content, content_vexpand, content_vexpand, 0);

    /* Pack header and content into main box */
    gtk_box_pack_start(GTK_BOX(acc->main_box), section->header_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(acc->main_box), section->content_box, content_vexpand, content_vexpand, 0);

    /* Add to sections list */
    acc->sections = g_list_append(acc->sections, section);
    acc->section_count++;

    /* Show all widgets */
    gtk_widget_show_all(acc->main_box);

    return (AccordionSection*)section;
}

/**
 * Get the main widget for the accordion
 */
GtkWidget* accordion_get_widget(Accordion* accordion) {
    AccordionImpl* acc = (AccordionImpl*)accordion;

    if (!acc) {
        return NULL;
    }

    return acc->main_box;
}

/**
 * Set section expanded state
 */
void accordion_set_section_expanded(Accordion* accordion,
                                    AccordionSection* section,
                                    gboolean expanded) {
    AccordionSectionImpl* sec = (AccordionSectionImpl*)section;

    if (!sec) {
        return;
    }

    sec->expanded = expanded;

    if (expanded) {
        gtk_widget_show(sec->content_box);
    } else {
        gtk_widget_hide(sec->content_box);
    }
}

/**
 * Get section expanded state
 */
gboolean accordion_get_section_expanded(Accordion* accordion,
                                        AccordionSection* section) {
    (void)accordion; /* Unused */
    AccordionSectionImpl* sec = (AccordionSectionImpl*)section;

    if (!sec) {
        return FALSE;
    }

    return sec->expanded;
}

/**
 * Collapse all sections
 */
void accordion_collapse_all(Accordion* accordion) {
    AccordionImpl* acc = (AccordionImpl*)accordion;

    if (!acc) {
        return;
    }

    for (GList* iter = acc->sections; iter; iter = iter->next) {
        AccordionSectionImpl* section = (AccordionSectionImpl*)iter->data;
        if (section) {
            section->expanded = FALSE;
            gtk_widget_hide(section->content_box);
        }
    }
}

/**
 * Expand all sections
 */
void accordion_expand_all(Accordion* accordion) {
    AccordionImpl* acc = (AccordionImpl*)accordion;

    if (!acc) {
        return;
    }

    for (GList* iter = acc->sections; iter; iter = iter->next) {
        AccordionSectionImpl* section = (AccordionSectionImpl*)iter->data;
        if (section) {
            section->expanded = TRUE;
            gtk_widget_show(section->content_box);
        }
    }
}

/**
 * Free accordion and all its sections
 */
void accordion_free(Accordion* accordion) {
    AccordionImpl* acc = (AccordionImpl*)accordion;

    if (!acc) {
        return;
    }

    /* Free all sections */
    for (GList* iter = acc->sections; iter; iter = iter->next) {
        AccordionSectionImpl* section = (AccordionSectionImpl*)iter->data;
        if (section) {
            if (section->title) {
                g_free(section->title);
            }
            g_free(section);
        }
    }

    g_list_free(acc->sections);
    g_free(acc);
}
