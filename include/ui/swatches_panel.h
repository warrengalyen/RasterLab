#ifndef SWATCHES_PANEL_H
#define SWATCHES_PANEL_H

#include <gtk/gtk.h>

/* Forward declaration - AppContext is defined in ui.h */
struct _AppContext;
typedef struct _AppContext AppContext;

/**
 * Create the swatches panel widget
 * @param ctx Application context (can be NULL, but needed for swatches sync)
 * @return GtkWidget* The swatches panel widget, or NULL on error
 */
GtkWidget* swatches_panel_create(AppContext* ctx);

/**
 * Cleanup swatches panel static references
 * Call this when the panel is being destroyed
 */
void swatches_panel_cleanup(void);

#endif /* SWATCHES_PANEL_H */
