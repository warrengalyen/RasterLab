#ifndef ACCORDION_H
#define ACCORDION_H

#include <gtk/gtk.h>

/**
 * Accordion widget for collapsible sections
 * Allows multiple sections to be expanded/collapsed independently
 */

typedef struct _Accordion Accordion;
typedef struct _AccordionSection AccordionSection;

/**
 * Create a new accordion widget
 * @return The accordion widget (GtkWidget*)
 */
Accordion* accordion_new(void);

/**
 * Add a new section to the accordion
 * @param accordion The accordion widget
 * @param title The section title/header text
 * @param content The content widget to display when expanded
 * @return The accordion section handle
 */
AccordionSection* accordion_add_section(Accordion *accordion,
                                        const gchar *title,
                                        GtkWidget *content);

/**
 * Get the main widget for the accordion
 * @param accordion The accordion widget
 * @return The GTK widget
 */
GtkWidget* accordion_get_widget(Accordion *accordion);

/**
 * Set section expanded state
 * @param accordion The accordion widget
 * @param section The section to update
 * @param expanded TRUE to expand, FALSE to collapse
 */
void accordion_set_section_expanded(Accordion *accordion,
                                    AccordionSection *section,
                                    gboolean expanded);

/**
 * Get section expanded state
 * @param accordion The accordion widget
 * @param section The section to query
 * @return TRUE if expanded, FALSE if collapsed
 */
gboolean accordion_get_section_expanded(Accordion *accordion,
                                        AccordionSection *section);

/**
 * Collapse all sections
 * @param accordion The accordion widget
 */
void accordion_collapse_all(Accordion *accordion);

/**
 * Expand all sections
 * @param accordion The accordion widget
 */
void accordion_expand_all(Accordion *accordion);

/**
 * Free accordion and all its sections
 * @param accordion The accordion to free
 */
void accordion_free(Accordion *accordion);

#endif /* ACCORDION_H */

