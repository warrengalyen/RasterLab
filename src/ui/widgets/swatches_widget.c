#include "ui/widgets/swatches_widget.h"
#include <cairo.h>
#include <gdk/gdk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "i18n.h"

/* Default values */
#define DEFAULT_MIN_SWATCH_SIZE 20.0
#define DEFAULT_MAX_SWATCH_SIZE 20.0
#define DEFAULT_SPACING 1.0
#define DEFAULT_PADDING 0.0

/* Signal enum */
enum {
    SWATCH_SELECTED_SIGNAL,
    LAST_SIGNAL
};

static guint swatches_widget_signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE(SwatchesWidget, swatches_widget, GTK_TYPE_DRAWING_AREA)

/* Forward declarations */
static gboolean swatches_widget_draw(GtkWidget* widget, cairo_t* cr);
static void swatches_widget_size_allocate(GtkWidget* widget, GtkAllocation* allocation);
static void swatches_widget_get_preferred_width(GtkWidget* widget, gint* minimum_width, gint* natural_width);
static void swatches_widget_get_preferred_height(GtkWidget* widget, gint* minimum_height, gint* natural_height);
static gboolean swatches_widget_motion_notify(GtkWidget* widget, GdkEventMotion* event);
static gboolean swatches_widget_leave_notify(GtkWidget* widget, GdkEventCrossing* event);
static gboolean swatches_widget_button_press(GtkWidget* widget, GdkEventButton* event);
static void swatches_widget_finalize(GObject* object);
static void update_tooltip(SwatchesWidget* self);
static void hide_tooltip(SwatchesWidget* self);
static gint get_swatch_at_position(SwatchesWidget* self, gdouble x, gdouble y);
static void calculate_swatch_size(SwatchesWidget* self);

/* Initialize widget */
static void swatches_widget_init(SwatchesWidget* self) {
    self->swatches = NULL;
    self->swatch_count = 0;
    self->columns = 0;
    self->rows = 0;
    self->swatch_size = DEFAULT_MIN_SWATCH_SIZE;
    self->max_swatch_size = DEFAULT_MAX_SWATCH_SIZE;
    self->spacing = DEFAULT_SPACING;
    self->padding = DEFAULT_PADDING;
    self->hovered_swatch = -1;
    self->show_outline = TRUE;
    self->selected_swatch = -1;
    self->tooltip_window = NULL;
    self->tooltip_visible = FALSE;

    /* Enable events for mouse tracking */
    gtk_widget_set_events(GTK_WIDGET(self),
                          GDK_POINTER_MOTION_MASK |
                              GDK_LEAVE_NOTIFY_MASK |
                              GDK_BUTTON_PRESS_MASK);
}

/* Finalize widget */
static void swatches_widget_finalize(GObject* object) {
    SwatchesWidget* self = SWATCHES_WIDGET(object);

    /* Free swatch data */
    if (self->swatches) {
        for (gint i = 0; i < self->swatch_count; i++) {
            g_free(self->swatches[i].name);
        }
        g_free(self->swatches);
        self->swatches = NULL;
    }

    /* Hide and destroy tooltip */
    hide_tooltip(self);

    G_OBJECT_CLASS(swatches_widget_parent_class)->finalize(object);
}

/* Class initialization */
static void swatches_widget_class_init(SwatchesWidgetClass* klass) {
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);

    object_class->finalize = swatches_widget_finalize;

    widget_class->draw = swatches_widget_draw;
    widget_class->size_allocate = swatches_widget_size_allocate;
    widget_class->get_preferred_width = swatches_widget_get_preferred_width;
    widget_class->get_preferred_height = swatches_widget_get_preferred_height;
    widget_class->motion_notify_event = swatches_widget_motion_notify;
    widget_class->leave_notify_event = swatches_widget_leave_notify;
    widget_class->button_press_event = swatches_widget_button_press;

    /* Register signals */
    swatches_widget_signals[SWATCH_SELECTED_SIGNAL] =
        g_signal_new("swatch-selected",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE, 1, G_TYPE_INT);
}

/* Calculate swatch size and columns based on available space */
static void calculate_swatch_size(SwatchesWidget* self) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(GTK_WIDGET(self), &allocation);

    if (self->swatch_count == 0) {
        self->swatch_size = DEFAULT_MIN_SWATCH_SIZE;
        self->columns = 0;
        self->rows = 0;
        return;
    }

    /* Calculate available width for swatches */
    gdouble available_width = allocation.width - (2.0 * self->padding);

    if (available_width <= 0) {
        self->swatch_size = DEFAULT_MIN_SWATCH_SIZE;
        self->columns = 1;
        self->rows = self->swatch_count;
        return;
    }

    /* Calculate maximum possible columns based on minimum swatch size */
    /* Formula: max_columns = floor((available_width + spacing) / (min_swatch_size + spacing)) */
    gint max_columns = (gint)((available_width + self->spacing) / (DEFAULT_MIN_SWATCH_SIZE + self->spacing));
    if (max_columns < 1) {
        max_columns = 1;
    }
    if (max_columns > self->swatch_count) {
        max_columns = self->swatch_count;
    }

    /* Calculate minimum columns based on maximum swatch size */
    gint min_columns = (gint)ceil((available_width + self->spacing) / (self->max_swatch_size + self->spacing));
    if (min_columns < 1) {
        min_columns = 1;
    }
    if (min_columns > self->swatch_count) {
        min_columns = self->swatch_count;
    }

    /* Calculate rows for different column counts and find optimal layout */
    gint best_columns = max_columns;
    gdouble best_swatch_size = 0.0;
    gdouble best_score = 0.0;

    /* Try columns from min to max to find best fit */
    for (gint cols = min_columns; cols <= max_columns; cols++) {
        gint rows = (self->swatch_count + cols - 1) / cols; /* Ceiling division */

        /* Calculate swatch size based on width - this should fill the width exactly */
        gdouble width_based_size = (available_width - (cols - 1) * self->spacing) / cols;

        /* Ensure minimum size */
        if (width_based_size < 4.0) {
            continue;
        }

        /* Cap at maximum size - if capped, we might not fill width exactly, but that's okay */
        gdouble actual_size = width_based_size;
        if (actual_size > self->max_swatch_size) {
            actual_size = self->max_swatch_size;
        }

        /* Calculate score - prefer layouts that:
         * 1. Use swatches close to preferred size
         * 2. Fill the width efficiently (prefer not capped)
         * 3. Have more columns (wider layout) when size is similar
         */
        gdouble size_ratio = fmin(actual_size / DEFAULT_MIN_SWATCH_SIZE, DEFAULT_MIN_SWATCH_SIZE / actual_size);
        gdouble fill_ratio = (cols * actual_size + (cols - 1) * self->spacing) / available_width;
        gdouble capped_penalty = (actual_size < width_based_size) ? 0.8 : 1.0; /* Penalize if we had to cap */
        gdouble score = size_ratio * fill_ratio * capped_penalty * (1.0 + cols * 0.01);

        if (score > best_score) {
            best_score = score;
            best_swatch_size = actual_size;
            best_columns = cols;
        }
    }

    self->columns = best_columns;
    self->rows = (self->swatch_count + self->columns - 1) / self->columns;

    /* Calculate exact size to fill width perfectly */
    gdouble exact_size = (available_width - (self->columns - 1) * self->spacing) / self->columns;

    /* If exact size exceeds max, we need more columns to use max size and still fill width */
    if (exact_size > self->max_swatch_size) {
        /* Calculate how many columns we need to use max size and fill width */
        gint optimal_cols = (gint)floor((available_width + self->spacing) / (self->max_swatch_size + self->spacing));
        if (optimal_cols < 1)
            optimal_cols = 1;
        if (optimal_cols > self->swatch_count)
            optimal_cols = self->swatch_count;

        if (optimal_cols != self->columns) {
            /* Use max size with optimal columns */
            self->columns = optimal_cols;
            self->rows = (self->swatch_count + self->columns - 1) / self->columns;
            /* Recalculate exact size with new column count */
            exact_size = (available_width - (self->columns - 1) * self->spacing) / self->columns;
        }

        /* Use max size if exact size still exceeds it, otherwise use exact size */
        if (exact_size > self->max_swatch_size) {
            self->swatch_size = self->max_swatch_size;
        } else {
            self->swatch_size = exact_size;
        }
    } else {
        /* Use exact size to fill width perfectly */
        self->swatch_size = exact_size;
    }

    /* Ensure minimum size */
    if (self->swatch_size < 4.0) {
        self->swatch_size = 4.0;
    }
}

/* Get swatch index at position */
static gint get_swatch_at_position(SwatchesWidget* self, gdouble x, gdouble y) {
    if (self->swatch_count == 0 || self->columns <= 0) {
        return -1;
    }

    /* Account for padding */
    x -= self->padding;
    y -= self->padding;

    if (x < 0 || y < 0) {
        return -1;
    }

    /* Calculate column and row */
    gint col = (gint)(x / (self->swatch_size + self->spacing));
    gint row = (gint)(y / (self->swatch_size + self->spacing));

    /* Check if within swatch bounds */
    gdouble swatch_x = col * (self->swatch_size + self->spacing);
    gdouble swatch_y = row * (self->swatch_size + self->spacing);

    if (x >= swatch_x && x < swatch_x + self->swatch_size &&
        y >= swatch_y && y < swatch_y + self->swatch_size) {
        gint index = row * self->columns + col;
        if (index >= 0 && index < self->swatch_count) {
            return index;
        }
    }

    return -1;
}

/* Get preferred width */
static void swatches_widget_get_preferred_width(GtkWidget* widget, gint* minimum_width, gint* natural_width) {
    SwatchesWidget* self = SWATCHES_WIDGET(widget);
    GtkAllocation allocation;

    /* Get current allocation or use a reasonable default */
    gtk_widget_get_allocation(widget, &allocation);
    gint width = allocation.width > 0 ? allocation.width : 300;

    if (minimum_width)
        *minimum_width = width;
    if (natural_width)
        *natural_width = width;
}

/* Get preferred height based on content */
static void swatches_widget_get_preferred_height(GtkWidget* widget, gint* minimum_height, gint* natural_height) {
    SwatchesWidget* self = SWATCHES_WIDGET(widget);

    /* Check if widget has a size request for height */
    gint requested_height = -1;
    gtk_widget_get_size_request(widget, NULL, &requested_height);

    if (self->swatch_count == 0) {
        /* Return minimum height based on padding even when empty */
        gint empty_height = (gint)ceil(2.0 * self->padding);
        if (empty_height < 1) {
            empty_height = 1; /* At least 1px to ensure widget is visible */
        }
        /* If size request is set, use the larger of requested height or empty height */
        if (requested_height > 0 && requested_height > empty_height) {
            empty_height = requested_height;
        }
        if (minimum_height)
            *minimum_height = empty_height;
        if (natural_height)
            *natural_height = empty_height;
        return;
    }

    /* Get widget width for calculation */
    gint width = -1;
    gtk_widget_get_preferred_width(widget, NULL, &width);
    if (width <= 0) {
        /* Fallback: use a reasonable default width */
        width = 300;
    }

    /* Calculate available width */
    gdouble available_width = width - (2.0 * self->padding);
    if (available_width <= 0) {
        if (minimum_height)
            *minimum_height = 0;
        if (natural_height)
            *natural_height = 0;
        return;
    }

    /* Calculate columns and swatch size for this width */
    gint max_columns = (gint)((available_width + self->spacing) / (DEFAULT_MIN_SWATCH_SIZE + self->spacing));
    if (max_columns < 1)
        max_columns = 1;
    if (max_columns > self->swatch_count)
        max_columns = self->swatch_count;

    gint min_columns = (gint)ceil((available_width + self->spacing) / (self->max_swatch_size + self->spacing));
    if (min_columns < 1)
        min_columns = 1;
    if (min_columns > self->swatch_count)
        min_columns = self->swatch_count;

    /* Find best columns */
    gint best_columns = max_columns;
    gdouble best_swatch_size = 0.0;
    gdouble best_score = 0.0;

    for (gint cols = min_columns; cols <= max_columns; cols++) {
        gdouble width_based_size = (available_width - (cols - 1) * self->spacing) / cols;
        if (width_based_size < 4.0)
            continue;

        gdouble actual_size = (width_based_size > self->max_swatch_size) ? self->max_swatch_size : width_based_size;
        gdouble size_ratio = fmin(actual_size / DEFAULT_MIN_SWATCH_SIZE, DEFAULT_MIN_SWATCH_SIZE / actual_size);
        gdouble fill_ratio = (cols * actual_size + (cols - 1) * self->spacing) / available_width;
        gdouble capped_penalty = (actual_size < width_based_size) ? 0.8 : 1.0;
        gdouble score = size_ratio * fill_ratio * capped_penalty * (1.0 + cols * 0.01);

        if (score > best_score) {
            best_score = score;
            best_swatch_size = actual_size;
            best_columns = cols;
        }
    }

    /* Calculate exact size to fill width */
    gdouble exact_size = (available_width - (best_columns - 1) * self->spacing) / best_columns;
    if (exact_size > self->max_swatch_size) {
        gint optimal_cols = (gint)floor((available_width + self->spacing) / (self->max_swatch_size + self->spacing));
        if (optimal_cols >= 1 && optimal_cols <= self->swatch_count) {
            best_columns = optimal_cols;
            exact_size = (available_width - (best_columns - 1) * self->spacing) / best_columns;
            if (exact_size <= self->max_swatch_size) {
                best_swatch_size = exact_size;
            } else {
                best_swatch_size = self->max_swatch_size;
            }
        } else {
            best_swatch_size = self->max_swatch_size;
        }
    } else {
        best_swatch_size = exact_size;
    }

    /* Calculate rows */
    gint rows = (self->swatch_count + best_columns - 1) / best_columns;

    /* Calculate required height */
    gdouble required_height = (2.0 * self->padding) +
                              (rows * best_swatch_size) +
                              ((rows - 1) * self->spacing);

    gint height = (gint)ceil(required_height);

    if (minimum_height)
        *minimum_height = height;
    if (natural_height)
        *natural_height = height;
}

/* Size allocate handler - recalculate layout when widget size changes */
static void swatches_widget_size_allocate(GtkWidget* widget, GtkAllocation* allocation) {
    SwatchesWidget* self = SWATCHES_WIDGET(widget);

    /* Call parent implementation first */
    GTK_WIDGET_CLASS(swatches_widget_parent_class)->size_allocate(widget, allocation);

    /* Recalculate layout for new size */
    calculate_swatch_size(self);

    /* Calculate natural height based on current layout */
    /* Guard against division by zero */
    if (self->columns <= 0 || self->swatch_count == 0) {
        gtk_widget_queue_draw(widget);
        return;
    }

    gint rows = (self->swatch_count + self->columns - 1) / self->columns;
    gdouble required_height = (2.0 * self->padding) +
                              (rows * self->swatch_size) +
                              ((rows > 0 ? rows - 1 : 0) * self->spacing);
    gint natural_height = (gint)ceil(required_height);

    /* If natural height differs from allocated height, we need to request the natural height */
    /* This ensures the scrolled window knows the widget needs more space and shows scrollbar */
    if (natural_height > 0 && natural_height > allocation->height) {
        /* Queue a resize request so the widget gets its natural height */
        gtk_widget_queue_resize(widget);
    }

    gtk_widget_queue_draw(widget);
}

/* Draw widget */
static gboolean swatches_widget_draw(GtkWidget* widget, cairo_t* cr) {
    SwatchesWidget* self = SWATCHES_WIDGET(widget);
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    /* Calculate swatch size and columns */
    calculate_swatch_size(self);

    /* Let GTK draw the default background (inherits parent background) */
    if (GTK_WIDGET_CLASS(swatches_widget_parent_class)->draw) {
        GTK_WIDGET_CLASS(swatches_widget_parent_class)->draw(widget, cr);
    }

    /* Even when empty, the widget should be visible due to size_request */
    /* The parent draw call above will draw the background */
    if (self->swatch_count == 0 || self->columns <= 0) {
        return FALSE;
    }

    /* First pass: Draw all swatches (color and borders) */
    for (gint i = 0; i < self->swatch_count; i++) {
        gint row = i / self->columns;
        gint col = i % self->columns;

        gdouble x = self->padding + col * (self->swatch_size + self->spacing);
        gdouble y = self->padding + row * (self->swatch_size + self->spacing);

        /* Draw swatch color */
        cairo_rectangle(cr, x, y, self->swatch_size, self->swatch_size);
        cairo_set_source_rgba(cr,
                              self->swatches[i].color.red,
                              self->swatches[i].color.green,
                              self->swatches[i].color.blue,
                              self->swatches[i].color.alpha);
        cairo_fill(cr);

        /* Draw black border for all swatches - extend into spacing area to overlap */
        cairo_set_line_width(cr, 1.0);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); /* Black border */
        /* Extend border by half spacing on each side so borders overlap */
        cairo_rectangle(cr, x - self->spacing * 0.5, y - self->spacing * 0.5,
                        self->swatch_size + self->spacing, self->swatch_size + self->spacing);
        cairo_stroke(cr);
    }

    /* Second pass: Draw selection and hover outlines on top of everything */

    /* Draw selected swatch outline */
    if (self->selected_swatch >= 0 && self->selected_swatch < self->swatch_count) {
        gint row = self->selected_swatch / self->columns;
        gint col = self->selected_swatch % self->columns;

        gdouble x = self->padding + col * (self->swatch_size + self->spacing);
        gdouble y = self->padding + row * (self->swatch_size + self->spacing);

        /* Calculate border position */
        gdouble border_x = x - self->spacing * 0.5;
        gdouble border_y = y - self->spacing * 0.5;
        gdouble border_size = self->swatch_size + self->spacing;

        /* Draw selection outline 1px outside the black border on all sides */
        cairo_set_line_width(cr, 2.0);
        /* Yellow/orange color for selection outline */
        cairo_set_source_rgb(cr, 255.0 / 255.0, 200.0 / 255.0, 0.0 / 255.0);
        /* Outline rectangle: 1px outside on all sides */
        cairo_rectangle(cr, border_x - 1.0, border_y - 1.0,
                        border_size + 2.0, border_size + 2.0);
        cairo_stroke(cr);
    }

    /* Draw hover outline (only if not the selected swatch) */
    if (self->hovered_swatch >= 0 && self->show_outline &&
        self->hovered_swatch < self->swatch_count &&
        self->hovered_swatch != self->selected_swatch) {
        gint row = self->hovered_swatch / self->columns;
        gint col = self->hovered_swatch % self->columns;

        gdouble x = self->padding + col * (self->swatch_size + self->spacing);
        gdouble y = self->padding + row * (self->swatch_size + self->spacing);

        /* Calculate border position */
        gdouble border_x = x - self->spacing * 0.5;
        gdouble border_y = y - self->spacing * 0.5;
        gdouble border_size = self->swatch_size + self->spacing;

        /* Draw hover outline 1px outside the black border on all sides */
        cairo_set_line_width(cr, 2.0);
        /* RGB(35, 135, 215) for hover outline */
        cairo_set_source_rgb(cr, 35.0 / 255.0, 135.0 / 255.0, 215.0 / 255.0);
        /* Outline rectangle: 1px outside on all sides */
        /* With 2px line width, the center of the line is at the rectangle edge,
           so we need to offset by 1px to get 1px outside the border */
        cairo_rectangle(cr, border_x - 1.0, border_y - 1.0,
                        border_size + 2.0, border_size + 2.0);
        cairo_stroke(cr);
    }

    return FALSE;
}

/* Motion notify event */
static gboolean swatches_widget_motion_notify(GtkWidget* widget, GdkEventMotion* event) {
    SwatchesWidget* self = SWATCHES_WIDGET(widget);
    gint new_hovered = get_swatch_at_position(self, event->x, event->y);

    if (new_hovered != self->hovered_swatch) {
        self->hovered_swatch = new_hovered;
        gtk_widget_queue_draw(widget);

        /* Update cursor - show hand cursor when hovering over a swatch */
        GdkWindow* window = gtk_widget_get_window(widget);
        if (window) {
            GdkDisplay* display = gdk_window_get_display(window);
            GdkCursor* cursor = NULL;
            if (self->hovered_swatch >= 0) {
                /* Hand cursor when hovering over swatch */
                cursor = gdk_cursor_new_from_name(display, "pointer");
            } else {
                /* Default cursor when not hovering */
                cursor = gdk_cursor_new_from_name(display, "default");
            }
            if (cursor) {
                gdk_window_set_cursor(window, cursor);
                g_object_unref(cursor);
            }
        }

        /* Update tooltip - store cursor position for tooltip positioning */
        if (self->hovered_swatch >= 0) {
            /* Store cursor position for tooltip positioning */
            GdkWindow* widget_window = gtk_widget_get_window(widget);
            if (widget_window) {
                gint wx, wy;
                gdk_window_get_origin(widget_window, &wx, &wy);
                /* Store cursor position in root coordinates */
                g_object_set_data(G_OBJECT(self), "tooltip_cursor_x", GINT_TO_POINTER(wx + (gint)event->x));
                g_object_set_data(G_OBJECT(self), "tooltip_cursor_y", GINT_TO_POINTER(wy + (gint)event->y));
                g_object_set_data(G_OBJECT(self), "tooltip_cursor_valid", GINT_TO_POINTER(1));
            }
            update_tooltip(self);
        } else {
            hide_tooltip(self);
            g_object_set_data(G_OBJECT(self), "tooltip_cursor_valid", GINT_TO_POINTER(0));
        }
    } else if (self->hovered_swatch >= 0) {
        /* Update cursor position for tooltip even if hovered swatch hasn't changed */
        GdkWindow* widget_window = gtk_widget_get_window(widget);
        if (widget_window) {
            gint wx, wy;
            gdk_window_get_origin(widget_window, &wx, &wy);
            g_object_set_data(G_OBJECT(self), "tooltip_cursor_x", GINT_TO_POINTER(wx + (gint)event->x));
            g_object_set_data(G_OBJECT(self), "tooltip_cursor_y", GINT_TO_POINTER(wy + (gint)event->y));
            g_object_set_data(G_OBJECT(self), "tooltip_cursor_valid", GINT_TO_POINTER(1));
            /* Update tooltip position if it's visible */
            if (self->tooltip_visible) {
                update_tooltip(self);
            }
        }
    }

    return FALSE;
}

/* Leave notify event */
static gboolean swatches_widget_leave_notify(GtkWidget* widget, GdkEventCrossing* event) {
    SwatchesWidget* self = SWATCHES_WIDGET(widget);

    if (self->hovered_swatch >= 0) {
        self->hovered_swatch = -1;
        gtk_widget_queue_draw(widget);
        hide_tooltip(self);

        /* Reset cursor to default */
        GdkWindow* window = gtk_widget_get_window(widget);
        if (window) {
            GdkDisplay* display = gdk_window_get_display(window);
            GdkCursor* cursor = gdk_cursor_new_from_name(display, "default");
            if (cursor) {
                gdk_window_set_cursor(window, cursor);
                g_object_unref(cursor);
            }
        }
    }

    return FALSE;
}

/* Button press event */
static gboolean swatches_widget_button_press(GtkWidget* widget, GdkEventButton* event) {
    SwatchesWidget* self = SWATCHES_WIDGET(widget);

    if (event->button == 1) {
        gint swatch_index = get_swatch_at_position(self, event->x, event->y);

        if (swatch_index >= 0) {
            if (event->type == GDK_2BUTTON_PRESS || event->type == GDK_DOUBLE_BUTTON_PRESS) {
                /* Double-click: toggle selection */
                if (self->selected_swatch == swatch_index) {
                    /* Deselect if already selected */
                    self->selected_swatch = -1;
                } else {
                    /* Select this swatch */
                    self->selected_swatch = swatch_index;
                }
                gtk_widget_queue_draw(widget);
                /* Emit selection signal */
                g_signal_emit(self, swatches_widget_signals[SWATCH_SELECTED_SIGNAL], 0, self->selected_swatch);
            } else if (event->type == GDK_BUTTON_PRESS) {
                /* Single-click: deselect if clicking a different swatch */
                if (self->selected_swatch >= 0 && self->selected_swatch != swatch_index) {
                    self->selected_swatch = -1;
                    gtk_widget_queue_draw(widget);
                }
                /* Always emit selection signal for single-click */
                g_signal_emit(self, swatches_widget_signals[SWATCH_SELECTED_SIGNAL], 0, swatch_index);
            }
        } else {
            /* Clicked outside any swatch: deselect */
            if (self->selected_swatch >= 0) {
                self->selected_swatch = -1;
                gtk_widget_queue_draw(widget);
            }
        }
    }

    return FALSE;
}

/* Update tooltip */
static void update_tooltip(SwatchesWidget* self) {
    if (self->hovered_swatch < 0 || self->hovered_swatch >= self->swatch_count) {
        hide_tooltip(self);
        return;
    }

    SwatchData* swatch = &self->swatches[self->hovered_swatch];
    GtkAllocation allocation;
    gtk_widget_get_allocation(GTK_WIDGET(self), &allocation);

    /* Calculate swatch position */
    calculate_swatch_size(self);
    gint row = self->hovered_swatch / self->columns;
    gint col = self->hovered_swatch % self->columns;
    gdouble x = self->padding + col * (self->swatch_size + self->spacing);
    gdouble y = self->padding + row * (self->swatch_size + self->spacing);

    /* Get widget window and convert to root coordinates */
    GdkWindow* widget_window = gtk_widget_get_window(GTK_WIDGET(self));
    if (!widget_window) {
        return;
    }

    gint wx, wy;
    gdk_window_get_origin(widget_window, &wx, &wy);
    wx += (gint)x;
    wy += (gint)y;

    /* Build tooltip text */
    GString* tooltip_text = g_string_new(NULL);

    if (swatch->name && swatch->name[0] != '\0') {
        g_string_append_printf(tooltip_text, "%s\n", swatch->name);
    }

    /* Hex color */
    g_string_append_printf(tooltip_text, "#%02X%02X%02X",
                           (guint)(swatch->color.red * 255.0),
                           (guint)(swatch->color.green * 255.0),
                           (guint)(swatch->color.blue * 255.0));

    /* RGB color */
    g_string_append_printf(tooltip_text, "\nRGB(%d, %d, %d)",
                           (gint)(swatch->color.red * 255.0),
                           (gint)(swatch->color.green * 255.0),
                           (gint)(swatch->color.blue * 255.0));

    /* Create or update tooltip window */
    if (!self->tooltip_window) {
        GtkWindow* toplevel = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(self)));
        self->tooltip_window = gtk_window_new(GTK_WINDOW_POPUP);
        if (toplevel && GTK_IS_WINDOW(toplevel)) {
            gtk_window_set_transient_for(GTK_WINDOW(self->tooltip_window), toplevel);
        }
        gtk_window_set_type_hint(GTK_WINDOW(self->tooltip_window), GDK_WINDOW_TYPE_HINT_TOOLTIP);
        gtk_window_set_decorated(GTK_WINDOW(self->tooltip_window), FALSE);
        gtk_window_set_resizable(GTK_WINDOW(self->tooltip_window), FALSE);

        /* Make tooltip window not receive pointer events to prevent flickering */
        gtk_widget_set_events(GTK_WIDGET(self->tooltip_window), 0);
        gtk_widget_set_can_focus(GTK_WIDGET(self->tooltip_window), FALSE);

        GtkWidget* label = gtk_label_new(NULL);
        gtk_label_set_line_wrap(GTK_LABEL(label), FALSE);
        gtk_label_set_selectable(GTK_LABEL(label), FALSE);
        gtk_container_add(GTK_CONTAINER(self->tooltip_window), label);
        gtk_widget_show(label);

        /* Store label reference */
        g_object_set_data(G_OBJECT(self->tooltip_window), "label", label);
    }

    GtkWidget* label = GTK_WIDGET(g_object_get_data(G_OBJECT(self->tooltip_window), "label"));
    gtk_label_set_text(GTK_LABEL(label), tooltip_text->str);

    /* Position tooltip relative to cursor to avoid overlap */
    /* Get cursor position if available, otherwise use swatch position */
    gint cursor_valid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(self), "tooltip_cursor_valid"));
    gint tooltip_x, tooltip_y;

    if (cursor_valid) {
        gint cursor_x = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(self), "tooltip_cursor_x"));
        gint cursor_y = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(self), "tooltip_cursor_y"));
        /* Position tooltip below and to the right of cursor to avoid overlap */
        tooltip_x = cursor_x + 15;
        tooltip_y = cursor_y + 20;
    } else {
        /* Fallback: position relative to swatch */
        tooltip_x = wx + 10;
        tooltip_y = wy - 30;
    }

    /* Get monitor geometry to prevent tooltip from going off-screen */
    GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(self));
    GdkMonitor* monitor = NULL;
    GdkWindow* window = gtk_widget_get_window(GTK_WIDGET(self));
    if (window) {
        monitor = gdk_display_get_monitor_at_window(display, window);
    }
    if (!monitor) {
        monitor = gdk_display_get_primary_monitor(display);
    }
    if (monitor) {
        GdkRectangle mon_geom;
        gdk_monitor_get_geometry(monitor, &mon_geom);
        gint r = mon_geom.x + mon_geom.width;
        gint b = mon_geom.y + mon_geom.height;

        /* Get tooltip preferred size before showing */
        gint min_width, min_height, nat_width, nat_height;
        gtk_widget_get_preferred_width(self->tooltip_window, &min_width, &nat_width);
        gtk_widget_get_preferred_height(self->tooltip_window, &min_height, &nat_height);
        gint tooltip_width = nat_width > 0 ? nat_width : min_width;
        gint tooltip_height = nat_height > 0 ? nat_height : min_height;

        /* If we still don't have a size, use reasonable defaults */
        if (tooltip_width <= 0)
            tooltip_width = 150;
        if (tooltip_height <= 0)
            tooltip_height = 60;

        /* Adjust horizontal position if tooltip would go off right edge */
        if (tooltip_width > 0 && tooltip_x + tooltip_width > r) {
            if (cursor_valid) {
                gint cursor_x = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(self), "tooltip_cursor_x"));
                tooltip_x = cursor_x - tooltip_width - 15;
            } else {
                tooltip_x = wx - tooltip_width - 10;
            }
        }

        /* Adjust horizontal position if tooltip would go off left edge */
        if (tooltip_x < mon_geom.x) {
            tooltip_x = mon_geom.x + 10;
        }

        /* Adjust vertical position if tooltip would go off bottom edge */
        if (tooltip_height > 0 && tooltip_y + tooltip_height > b) {
            if (cursor_valid) {
                gint cursor_y = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(self), "tooltip_cursor_y"));
                tooltip_y = cursor_y - tooltip_height - 20;
            } else {
                tooltip_y = wy - tooltip_height - 10;
            }
        }

        /* Adjust vertical position if tooltip would go off top edge */
        if (tooltip_y < mon_geom.y) {
            tooltip_y = mon_geom.y + 10;
        }
    }

    gtk_window_move(GTK_WINDOW(self->tooltip_window), tooltip_x, tooltip_y);
    gtk_widget_show(self->tooltip_window);

    self->tooltip_visible = TRUE;
    g_string_free(tooltip_text, TRUE);
}

/* Hide tooltip */
static void hide_tooltip(SwatchesWidget* self) {
    if (self->tooltip_window && self->tooltip_visible) {
        gtk_widget_hide(self->tooltip_window);
        self->tooltip_visible = FALSE;
    }
}

/* Public API */

GtkWidget* swatches_widget_new(void) {
    return GTK_WIDGET(g_object_new(SWATCHES_TYPE_WIDGET, NULL));
}

void swatches_widget_add_swatch(SwatchesWidget* self, const GdkRGBA* color, const gchar* name) {
    if (!self || !color) {
        return;
    }

    self->swatches = g_realloc(self->swatches, sizeof(SwatchData) * (self->swatch_count + 1));
    self->swatches[self->swatch_count].color = *color;
    self->swatches[self->swatch_count].name = name ? g_strdup(name) : NULL;
    self->swatch_count++;

    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void swatches_widget_clear(SwatchesWidget* self) {
    if (!self) {
        return;
    }

    if (self->swatches) {
        for (gint i = 0; i < self->swatch_count; i++) {
            g_free(self->swatches[i].name);
        }
        g_free(self->swatches);
        self->swatches = NULL;
    }

    self->swatch_count = 0;
    self->hovered_swatch = -1;
    self->selected_swatch = -1; /* Clear selection */
    hide_tooltip(self);
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void swatches_widget_set_swatch(SwatchesWidget* self, gint index, const GdkRGBA* color, const gchar* name) {
    if (!self || !color || index < 0 || index >= self->swatch_count) {
        return;
    }

    g_free(self->swatches[index].name);
    self->swatches[index].color = *color;
    self->swatches[index].name = name ? g_strdup(name) : NULL;

    gtk_widget_queue_draw(GTK_WIDGET(self));
}

gint swatches_widget_get_swatch_count(SwatchesWidget* self) {
    if (!self) {
        return 0;
    }
    return self->swatch_count;
}

gboolean swatches_widget_get_swatch(SwatchesWidget* self, gint index, GdkRGBA* color, gchar** name) {
    if (!self || !color || index < 0 || index >= self->swatch_count) {
        return FALSE;
    }

    *color = self->swatches[index].color;
    if (name) {
        *name = self->swatches[index].name ? g_strdup(self->swatches[index].name) : NULL;
    }

    return TRUE;
}

void swatches_widget_set_columns(SwatchesWidget* self, gint columns) {
    /* This function is deprecated - columns are now calculated automatically */
    /* Keeping for API compatibility but it has no effect */
    (void)self;
    (void)columns;
}

void swatches_widget_set_spacing(SwatchesWidget* self, gdouble spacing) {
    if (!self || spacing < 0) {
        return;
    }

    self->spacing = spacing;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void swatches_widget_set_padding(SwatchesWidget* self, gdouble padding) {
    if (!self || padding < 0) {
        return;
    }

    self->padding = padding;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void swatches_widget_set_max_swatch_size(SwatchesWidget* self, gdouble max_size) {
    if (!self || max_size < DEFAULT_MIN_SWATCH_SIZE) {
        return;
    }

    self->max_swatch_size = max_size;
    /* Recalculate layout with new max size */
    calculate_swatch_size(self);
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

/* Get selected swatch index */
gint swatches_widget_get_selected(SwatchesWidget* self) {
    if (!SWATCHES_IS_WIDGET(self)) {
        return -1;
    }

    return self->selected_swatch;
}

/* Set selected swatch index */
void swatches_widget_set_selected(SwatchesWidget* self, gint index) {
    if (!SWATCHES_IS_WIDGET(self)) {
        return;
    }

    if (index < -1 || index >= self->swatch_count) {
        return;
    }

    if (self->selected_swatch != index) {
        self->selected_swatch = index;
        gtk_widget_queue_draw(GTK_WIDGET(self));
    }
}
