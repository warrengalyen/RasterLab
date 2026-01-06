#ifndef VERTICAL_SPIN_BUTTON_H
#define VERTICAL_SPIN_BUTTON_H

#include <gtk/gtk.h>

/**
 * Vertical spin button widget
 * A custom spin button with up/down buttons stacked vertically on the right
 */
typedef struct _VerticalSpinButton VerticalSpinButton;
typedef struct _VerticalSpinButtonClass VerticalSpinButtonClass;

#define VERTICAL_SPIN_BUTTON(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), vertical_spin_button_get_type(), VerticalSpinButton))
#define VERTICAL_SPIN_BUTTON_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), vertical_spin_button_get_type(), VerticalSpinButtonClass))
#define IS_VERTICAL_SPIN_BUTTON(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), vertical_spin_button_get_type()))
#define IS_VERTICAL_SPIN_BUTTON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), vertical_spin_button_get_type()))

GType vertical_spin_button_get_type(void);

/**
 * Create a new vertical spin button
 * @param adjustment The GtkAdjustment to use (can be NULL, will create one)
 * @param climb_rate The climb rate (step increment)
 * @param digits Number of decimal places to display
 * @return New VerticalSpinButton widget
 */
GtkWidget* vertical_spin_button_new(GtkAdjustment* adjustment, gdouble climb_rate, guint digits);

/**
 * Get the value from the spin button
 * @param spin The vertical spin button
 * @return Current value
 */
gdouble vertical_spin_button_get_value(VerticalSpinButton* spin);

/**
 * Set the value of the spin button
 * @param spin The vertical spin button
 * @param value The value to set
 */
void vertical_spin_button_set_value(VerticalSpinButton* spin, gdouble value);

/**
 * Get the adjustment used by the spin button
 * @param spin The vertical spin button
 * @return The GtkAdjustment
 */
GtkAdjustment* vertical_spin_button_get_adjustment(VerticalSpinButton* spin);

/**
 * Set the adjustment for the spin button
 * @param spin The vertical spin button
 * @param adjustment The adjustment to use
 */
void vertical_spin_button_set_adjustment(VerticalSpinButton* spin, GtkAdjustment* adjustment);

/**
 * Get the entry widget (for direct access if needed)
 * @param spin The vertical spin button
 * @return The GtkEntry widget
 */
GtkWidget* vertical_spin_button_get_entry(VerticalSpinButton* spin);

#endif /* VERTICAL_SPIN_BUTTON_H */
