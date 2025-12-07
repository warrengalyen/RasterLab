#include "accordion.h"
#include <stdlib.h>
#include <string.h>

/**
 * Accordion section structure
 */
typedef struct {
    GtkWidget *header_button;      /* Header/title button */
    GtkWidget *content;            /* Content widget */
    GtkWidget *content_box;        /* Box containing content */
    gboolean expanded;             /* Current state */
    gchar *title;                  /* Section title */
} AccordionSectionImpl;

/**
 * Accordion structure
 */
typedef struct _Accordion {
    GtkWidget *main_box;           /* Main vertical box */
    GList *sections;               /* List of AccordionSectionImpl* */
    gint section_count;            /* Number of sections */
} AccordionImpl;

/**
 * Header button click callback
 */
static void on_accordion_header_clicked(GtkButton *button, gpointer user_data)
{
    AccordionSectionImpl *section = (AccordionSectionImpl *)user_data;
    
    if (!section) {
        return;
    }
    
    /* Toggle expanded state */
    section->expanded = !section->expanded;
    
    /* Show/hide content */
    if (section->expanded) {
        gtk_widget_show(section->content_box);
    } else {
        gtk_widget_hide(section->content_box);
    }
}

/**
 * Create a new accordion widget
 */
Accordion* accordion_new(void)
{
    AccordionImpl *accordion = (AccordionImpl *)g_malloc(sizeof(AccordionImpl));
    
    /* Create main vertical box */
    accordion->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(accordion->main_box, 0);
    gtk_widget_set_margin_start(accordion->main_box, 0);
    gtk_widget_set_margin_end(accordion->main_box, 0);
    gtk_widget_set_margin_bottom(accordion->main_box, 0);
    
    accordion->sections = NULL;
    accordion->section_count = 0;
    
    return (Accordion *)accordion;
}

/**
 * Add a new section to the accordion
 */
AccordionSection* accordion_add_section(Accordion *accordion,
                                        const gchar *title,
                                        GtkWidget *content)
{
    AccordionImpl *acc = (AccordionImpl *)accordion;
    
    if (!acc || !title || !content) {
        return NULL;
    }
    
    /* Create section structure */
    AccordionSectionImpl *section = (AccordionSectionImpl *)g_malloc(sizeof(AccordionSectionImpl));
    section->title = g_strdup(title);
    section->expanded = TRUE;  /* Default to expanded */
    section->content = content;
    
    /* Create header button */
    section->header_button = gtk_button_new_with_label(title);
    gtk_widget_set_halign(section->header_button, GTK_ALIGN_START);
    gtk_button_set_relief(GTK_BUTTON(section->header_button), GTK_RELIEF_NONE);
    gtk_widget_set_margin_top(section->header_button, 2);
    gtk_widget_set_margin_bottom(section->header_button, 2);
    gtk_widget_set_margin_start(section->header_button, 5);
    gtk_widget_set_margin_end(section->header_button, 5);
    
    /* Connect button click */
    g_signal_connect(section->header_button, "clicked",
                    G_CALLBACK(on_accordion_header_clicked), section);
    
    /* Create content box */
    section->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(section->content_box, 0);
    gtk_widget_set_margin_start(section->content_box, 10);
    gtk_widget_set_margin_end(section->content_box, 0);
    gtk_widget_set_margin_bottom(section->content_box, 5);
    gtk_box_pack_start(GTK_BOX(section->content_box), content, TRUE, TRUE, 0);
    
    /* Pack header and content into main box */
    gtk_box_pack_start(GTK_BOX(acc->main_box), section->header_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(acc->main_box), section->content_box, FALSE, FALSE, 0);
    
    /* Add to sections list */
    acc->sections = g_list_append(acc->sections, section);
    acc->section_count++;
    
    /* Show all widgets */
    gtk_widget_show_all(acc->main_box);
    
    return (AccordionSection *)section;
}

/**
 * Get the main widget for the accordion
 */
GtkWidget* accordion_get_widget(Accordion *accordion)
{
    AccordionImpl *acc = (AccordionImpl *)accordion;
    
    if (!acc) {
        return NULL;
    }
    
    return acc->main_box;
}

/**
 * Set section expanded state
 */
void accordion_set_section_expanded(Accordion *accordion,
                                    AccordionSection *section,
                                    gboolean expanded)
{
    AccordionSectionImpl *sec = (AccordionSectionImpl *)section;
    
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
gboolean accordion_get_section_expanded(Accordion *accordion,
                                        AccordionSection *section)
{
    (void)accordion;  /* Unused */
    AccordionSectionImpl *sec = (AccordionSectionImpl *)section;
    
    if (!sec) {
        return FALSE;
    }
    
    return sec->expanded;
}

/**
 * Collapse all sections
 */
void accordion_collapse_all(Accordion *accordion)
{
    AccordionImpl *acc = (AccordionImpl *)accordion;
    
    if (!acc) {
        return;
    }
    
    for (GList *iter = acc->sections; iter; iter = iter->next) {
        AccordionSectionImpl *section = (AccordionSectionImpl *)iter->data;
        if (section) {
            section->expanded = FALSE;
            gtk_widget_hide(section->content_box);
        }
    }
}

/**
 * Expand all sections
 */
void accordion_expand_all(Accordion *accordion)
{
    AccordionImpl *acc = (AccordionImpl *)accordion;
    
    if (!acc) {
        return;
    }
    
    for (GList *iter = acc->sections; iter; iter = iter->next) {
        AccordionSectionImpl *section = (AccordionSectionImpl *)iter->data;
        if (section) {
            section->expanded = TRUE;
            gtk_widget_show(section->content_box);
        }
    }
}

/**
 * Free accordion and all its sections
 */
void accordion_free(Accordion *accordion)
{
    AccordionImpl *acc = (AccordionImpl *)accordion;
    
    if (!acc) {
        return;
    }
    
    /* Free all sections */
    for (GList *iter = acc->sections; iter; iter = iter->next) {
        AccordionSectionImpl *section = (AccordionSectionImpl *)iter->data;
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

