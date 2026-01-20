#ifndef LAYERS_PANEL_H
#define LAYERS_PANEL_H

#include "document.h"
#include "ui/widgets/accordion.h"
#include <gtk/gtk.h>

/* Forward declaration - AppContext is defined in ui.h */
struct _AppContext;
typedef struct _AppContext AppContext;

/**
 * Layers panel structure
 */
typedef struct {
    GtkWidget* panel;             /* Main panel widget */
    Accordion* accordion;         /* Accordion widget for sections */
    GtkWidget* tree_view;         /* Layer list tree view */
    GtkListStore* store;          /* List store for layers */
    ImageDocument* current_doc;   /* Current document reference */
    GtkWidget* btn_new;           /* New layer button */
    GtkWidget* btn_delete;        /* Delete layer button */
    GtkWidget* btn_up;            /* Move layer up button */
    GtkWidget* btn_down;          /* Move layer down button */
    GtkWidget* btn_duplicate;     /* Duplicate layer button */
    GtkWidget* scale_opacity;     /* Opacity scale slider */
    GtkWidget* spin_opacity;      /* Opacity spin button */
    GtkWidget* btn_opacity_reset; /* Opacity reset button */
    GtkWidget* combo_blend;       /* Blend mode combo box */
    GtkWidget* overview_widget;   /* Overview thumbnail widget */
    gpointer app_context;         /* Reference to AppContext for callbacks */
} LayersPanel;

/**
 * Create the layers panel with tree view
 * @param ctx Application context (can be NULL, but needed for swatches sync)
 * @return LayersPanel structure
 */
LayersPanel* create_layers_panel(AppContext* ctx);

/**
 * Update layers panel with document layers
 * @param layers_panel The layers panel
 * @param doc The document to display
 */
void layers_panel_update(LayersPanel* layers_panel, ImageDocument* doc);

/**
 * Update thumbnails for all layers in the panel
 * Call this after layer content changes to refresh thumbnails
 * @param layers_panel The layers panel
 */
void layers_panel_refresh_thumbnails(LayersPanel* layers_panel);

/**
 * Update thumbnail for the currently selected layer only
 * Call this after drawing operations to update the selected layer's thumbnail
 * @param layers_panel The layers panel
 */
void layers_panel_update_selected_thumbnail(LayersPanel* layers_panel);

/**
 * Select a specific layer in the layers panel
 * @param layers_panel The layers panel
 * @param doc The document
 * @param layer The layer to select
 */
void layers_panel_select_layer(LayersPanel* layers_panel, ImageDocument* doc, ImageLayer* layer);

/**
 * Get the currently selected layer from the panel
 * @param layers_panel The layers panel
 * @return The selected layer, or NULL if none selected
 */
ImageLayer* layers_panel_get_selected_layer(LayersPanel* layers_panel);

/**
 * Update opacity controls based on selected layer
 * @param layers_panel The layers panel
 */
void layers_panel_update_opacity_controls(LayersPanel* layers_panel);

/**
 * Update button sensitivity based on state
 * @param layers_panel The layers panel
 * @param has_document Whether a document is open
 * @param has_selection Whether a layer is selected
 * @param doc The document (can be NULL)
 * @param selected_layer The selected layer (can be NULL)
 */
void layers_panel_update_button_sensitivity(LayersPanel* layers_panel,
                                            gboolean has_document,
                                            gboolean has_selection,
                                            ImageDocument* doc,
                                            ImageLayer* selected_layer);

/**
 * Connect layers panel buttons to UI callbacks
 * @param layers_panel The layers panel
 * @param new_callback Callback for New button
 * @param delete_callback Callback for Delete button
 * @param duplicate_callback Callback for Duplicate button
 * @param user_data User data to pass to callbacks
 */
void layers_panel_connect_buttons(LayersPanel* layers_panel,
                                  GCallback new_callback,
                                  GCallback delete_callback,
                                  GCallback duplicate_callback,
                                  gpointer user_data);

/**
 * Free a layers panel
 * @param layers_panel The layers panel to free
 */
void layers_panel_free(LayersPanel* layers_panel);

/**
 * Add a color to recent colors
 * @param color The color to add
 */
void layers_panel_add_recent_color(GdkRGBA* color);

#endif /* LAYERS_PANEL_H */
