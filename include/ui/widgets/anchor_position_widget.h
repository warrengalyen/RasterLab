#ifndef ANCHOR_POSITION_WIDGET_H
#define ANCHOR_POSITION_WIDGET_H

#include "document.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

/**
 * Anchor position widget structure
 */
typedef struct _AnchorPositionWidget AnchorPositionWidget;

/**
 * Create a new anchor position widget
 * @return Newly created AnchorPositionWidget
 */
AnchorPositionWidget* anchor_position_widget_new(void);

/**
 * Get the widget container
 * @param widget The anchor position widget
 * @return GtkWidget container
 */
GtkWidget* anchor_position_widget_get_widget(AnchorPositionWidget* widget);

/**
 * Get the current anchor position
 * @param widget The anchor position widget
 * @return Current CanvasAnchorPosition
 */
CanvasAnchorPosition anchor_position_widget_get_position(AnchorPositionWidget* widget);

/**
 * Set the anchor position
 * @param widget The anchor position widget
 * @param position The anchor position to set
 */
void anchor_position_widget_set_position(AnchorPositionWidget* widget, CanvasAnchorPosition position);

/**
 * Reset to default position (center)
 * @param widget The anchor position widget
 */
void anchor_position_widget_reset(AnchorPositionWidget* widget);

/**
 * Free the anchor position widget
 * @param widget The anchor position widget to free
 */
void anchor_position_widget_free(AnchorPositionWidget* widget);

G_END_DECLS

#endif /* ANCHOR_POSITION_WIDGET_H */
