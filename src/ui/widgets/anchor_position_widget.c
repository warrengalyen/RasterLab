#include "ui/widgets/anchor_position_widget.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include "i18n.h"
#include "debug_logger.h"

/**
 * Anchor position widget structure
 */
struct _AnchorPositionWidget {
    GtkWidget* container;          /* Main container widget */
    GtkWidget* buttons[9];         /* Array of 9 toggle buttons */
    CanvasAnchorPosition position; /* Current anchor position */
    GtkIconTheme* icon_theme;      /* Icon theme for loading icons */
};

/**
 * Icon mapping: maps anchor position to icon resource name
 * The icons represent directions relative to center
 */
static const gchar* icon_resources[9] = {
    "/icons/anchor-315.png",    /* TOP_LEFT */
    "/icons/anchor-0.png",      /* TOP_CENTER */
    "/icons/anchor-45.png",     /* TOP_RIGHT */
    "/icons/anchor-270.png",    /* MIDDLE_LEFT */
    "/icons/anchor-center.png", /* CENTER */
    "/icons/anchor-90.png",     /* MIDDLE_RIGHT */
    "/icons/anchor-225.png",    /* BOTTOM_LEFT */
    "/icons/anchor-180.png",    /* BOTTOM_CENTER */
    "/icons/anchor-135.png"     /* BOTTOM_RIGHT */
};

/**
 * Update button icons based on selected anchor position
 * Only show icons for positions one step away from the selected anchor
 * The rest should be empty
 */
static void update_button_icons(AnchorPositionWidget* widget) {
    gint selected_index = widget->position;
    gint i;
    const gchar* icon_resource = NULL;
    GtkImage* btn_image;
    GError* icon_error = NULL;
    GdkPixbuf* icon_pixbuf;

    if (selected_index < 0 || selected_index >= 9) {
        selected_index = 4; /* Default to center */
    }

    /* Update icons for each button */
    for (i = 0; i < 9; i++) {
        gboolean should_show_icon = FALSE;

        if (i == selected_index) {
            /* Selected position gets the center icon */
            icon_resource = "/icons/anchor-center.png";
            should_show_icon = TRUE;
        } else {
            /* Calculate relative position from selected to this button */
            gint row_i = i / 3;
            gint col_i = i % 3;
            gint row_sel = selected_index / 3;
            gint col_sel = selected_index % 3;
            gint delta_row = row_i - row_sel;
            gint delta_col = col_i - col_sel;

            /* Only show icon if this position is exactly one step away (adjacent) */
            /* Check if it's adjacent: |delta_row| <= 1 && |delta_col| <= 1 && not both zero */
            if (abs(delta_row) <= 1 && abs(delta_col) <= 1 && (delta_row != 0 || delta_col != 0)) {
                should_show_icon = TRUE;

                /* Map relative position to icon */
                /* The icon represents the direction FROM the selected position TO this button (pointing away) */
                if (delta_row == -1 && delta_col == -1) {
                    /* This button is top-left of selected, so icon should point top-left */
                    icon_resource = "/icons/anchor-315.png";
                } else if (delta_row == -1 && delta_col == 0) {
                    /* This button is above selected, so icon should point up */
                    icon_resource = "/icons/anchor-0.png";
                } else if (delta_row == -1 && delta_col == 1) {
                    /* This button is top-right of selected, so icon should point top-right */
                    icon_resource = "/icons/anchor-45.png";
                } else if (delta_row == 0 && delta_col == -1) {
                    /* This button is left of selected, so icon should point left */
                    icon_resource = "/icons/anchor-270.png";
                } else if (delta_row == 0 && delta_col == 1) {
                    /* This button is right of selected, so icon should point right */
                    icon_resource = "/icons/anchor-90.png";
                } else if (delta_row == 1 && delta_col == -1) {
                    /* This button is bottom-left of selected, so icon should point bottom-left */
                    icon_resource = "/icons/anchor-225.png";
                } else if (delta_row == 1 && delta_col == 0) {
                    /* This button is below selected, so icon should point down */
                    icon_resource = "/icons/anchor-180.png";
                } else if (delta_row == 1 && delta_col == 1) {
                    /* This button is bottom-right of selected, so icon should point bottom-right */
                    icon_resource = "/icons/anchor-135.png";
                }
            } else {
                /* Position is not adjacent - no icon */
                should_show_icon = FALSE;
            }
        }

        /* Get or create button image */
        btn_image = GTK_IMAGE(gtk_bin_get_child(GTK_BIN(widget->buttons[i])));

        if (should_show_icon && icon_resource) {
            /* Load icon (resource is to-pixdata format) */
            icon_pixbuf = gdk_pixbuf_new_from_resource(icon_resource, &icon_error);
            if (!icon_pixbuf) {
                debug_log("WRN", "Failed to create pixbuf from %s: %s", icon_resource, icon_error ? icon_error->message : "Unknown error");
                if (icon_error) {
                    g_error_free(icon_error);
                    icon_error = NULL;
                }
                /* Clear icon if we can't create pixbuf */
                if (btn_image) {
                    gtk_image_clear(btn_image);
                }
                continue;
            }

            /* Scale to 20x20 */
            GdkPixbuf* scaled = gdk_pixbuf_scale_simple(icon_pixbuf, 20, 20, GDK_INTERP_BILINEAR);
            g_object_unref(icon_pixbuf);
            icon_pixbuf = scaled;
            if (!icon_pixbuf) {
                if (btn_image) {
                    gtk_image_clear(btn_image);
                }
                continue;
            }

            /* Update or create button image */
            if (btn_image) {
                gtk_image_set_from_pixbuf(btn_image, icon_pixbuf);
            } else {
                /* Create new image if it doesn't exist */
                btn_image = GTK_IMAGE(gtk_image_new_from_pixbuf(icon_pixbuf));
                gtk_container_add(GTK_CONTAINER(widget->buttons[i]), GTK_WIDGET(btn_image));
            }
            g_object_unref(icon_pixbuf);
        } else {
            /* Clear icon for non-adjacent positions */
            if (btn_image) {
                gtk_image_clear(btn_image);
            }
        }
    }
}

/**
 * Button toggled callback
 * Manually manage toggle group: when one is activated, deactivate all others
 */
static void on_button_toggled(GtkToggleButton* button, gpointer user_data) {
    AnchorPositionWidget* widget = (AnchorPositionWidget*)user_data;
    gint i;
    gint activated_index = -1;

    /* Find which button was toggled */
    for (i = 0; i < 9; i++) {
        if (GTK_WIDGET(button) == widget->buttons[i]) {
            activated_index = i;
            break;
        }
    }

    if (activated_index < 0) {
        return;
    }

    if (gtk_toggle_button_get_active(button)) {
        /* This button was activated - deactivate all others */
        for (i = 0; i < 9; i++) {
            if (i != activated_index) {
                g_signal_handlers_block_by_func(widget->buttons[i],
                                                G_CALLBACK(on_button_toggled),
                                                widget);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget->buttons[i]), FALSE);
                g_signal_handlers_unblock_by_func(widget->buttons[i],
                                                  G_CALLBACK(on_button_toggled),
                                                  widget);
            }
        }

        /* Update position and icons */
        widget->position = (CanvasAnchorPosition)activated_index;
        update_button_icons(widget);
    } else {
        /* Button was deactivated - reactivate it to maintain single selection */
        g_signal_handlers_block_by_func(button, G_CALLBACK(on_button_toggled), widget);
        gtk_toggle_button_set_active(button, TRUE);
        g_signal_handlers_unblock_by_func(button, G_CALLBACK(on_button_toggled), widget);
    }
}

/**
 * Create a new anchor position widget
 */
AnchorPositionWidget* anchor_position_widget_new(void) {
    AnchorPositionWidget* widget;
    GtkWidget* grid;
    GtkWidget* button;
    GtkImage* image;
    gint i, row, col;
    GError* error = NULL;
    GdkPixbuf* pixbuf;

    widget = (AnchorPositionWidget*)g_malloc(sizeof(AnchorPositionWidget));
    if (!widget) {
        return NULL;
    }

    widget->position = CANVAS_ANCHOR_CENTER; /* Default to center */
    widget->icon_theme = NULL;

    /* Create grid container */
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 2);
    widget->container = grid;

    /* Create 9 toggle buttons in a 3x3 grid */
    for (i = 0; i < 9; i++) {
        row = i / 3;
        col = i % 3;

        /* Create toggle button */
        button = gtk_toggle_button_new();
        gtk_widget_set_size_request(button, 40, 40);
        gtk_widget_set_margin_start(button, 2);
        gtk_widget_set_margin_end(button, 2);
        gtk_widget_set_margin_top(button, 2);
        gtk_widget_set_margin_bottom(button, 2);

        /* Load icon (resource is to-pixdata format) and scale to 20x20 */
        pixbuf = gdk_pixbuf_new_from_resource(icon_resources[i], &error);
        if (pixbuf) {
            GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, 20, 20, GDK_INTERP_BILINEAR);
            g_object_unref(pixbuf);
            pixbuf = scaled;
        }
        if (pixbuf) {
            image = GTK_IMAGE(gtk_image_new_from_pixbuf(pixbuf));
            gtk_container_add(GTK_CONTAINER(button), GTK_WIDGET(image));
            g_object_unref(pixbuf);
        } else {
            debug_log("WRN", "Failed to load %s: %s", icon_resources[i], error ? error->message : "Unknown error");
            if (error) {
                g_error_free(error);
                error = NULL;
            }
        }

        /* Connect toggle signal */
        g_signal_connect(button, "toggled", G_CALLBACK(on_button_toggled), widget);

        /* Add to grid */
        gtk_grid_attach(GTK_GRID(grid), button, col, row, 1, 1);
        widget->buttons[i] = button;
    }

    /* Set center button as active by default */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget->buttons[4]), TRUE);

    /* Update icons for initial state */
    update_button_icons(widget);

    return widget;
}

/**
 * Get the widget container
 */
GtkWidget* anchor_position_widget_get_widget(AnchorPositionWidget* widget) {
    if (!widget) {
        return NULL;
    }
    return widget->container;
}

/**
 * Get the current anchor position
 */
CanvasAnchorPosition anchor_position_widget_get_position(AnchorPositionWidget* widget) {
    if (!widget) {
        return CANVAS_ANCHOR_CENTER;
    }
    return widget->position;
}

/**
 * Set the anchor position
 */
void anchor_position_widget_set_position(AnchorPositionWidget* widget, CanvasAnchorPosition position) {
    if (!widget) {
        return;
    }

    if (position < CANVAS_ANCHOR_TOP_LEFT || position > CANVAS_ANCHOR_BOTTOM_RIGHT) {
        position = CANVAS_ANCHOR_CENTER;
    }

    widget->position = position;

    /* Update button states */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget->buttons[position]), TRUE);

    /* Update icons */
    update_button_icons(widget);
}

/**
 * Reset to default position (center)
 */
void anchor_position_widget_reset(AnchorPositionWidget* widget) {
    anchor_position_widget_set_position(widget, CANVAS_ANCHOR_CENTER);
}

/**
 * Free the anchor position widget
 */
void anchor_position_widget_free(AnchorPositionWidget* widget) {
    if (!widget) {
        return;
    }

    if (widget->icon_theme) {
        g_object_unref(widget->icon_theme);
    }

    /* Widgets will be destroyed when container is destroyed */
    gtk_widget_destroy(widget->container);

    g_free(widget);
}
