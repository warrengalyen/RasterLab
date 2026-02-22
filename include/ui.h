#ifndef UI_H
#define UI_H

#include "app/settings.h"
#include "command.h"
#include "document.h"
#include "panels.h"
#include "tools.h"
#include "ui/layers_panel.h"
#include "ui/swatches.h"
#include "ui/tool_options_panel.h"
#include "ui/tools_panel.h"
#include "ui/workspace.h"
#include <gtk/gtk.h>

/**
 * Application context structure
 */
struct _AppContext {
    GtkWidget* window;                    /* Main application window */
    GtkWidget* notebook;                  /* Notebook widget for tabs */
    GtkWidget* menu_bar;                  /* Menu bar */
    GtkWidget* status_bar;                /* Status bar */
    GList* documents;                     /* List of open documents */
    GtkWidget* layer_menu_new;            /* Layer > New Layer menu item */
    GtkWidget* layer_menu_delete;         /* Layer > Delete Layer menu item */
    GtkWidget* layer_menu_duplicate;      /* Layer > Duplicate Layer menu item */
    GtkWidget* layer_menu_merge_up;       /* Layer > Merge up menu item */
    GtkWidget* layer_menu_merge_down;     /* Layer > Merge down menu item */
    GtkWidget* layer_menu_order_select_top;
    GtkWidget* layer_menu_order_select_above;
    GtkWidget* layer_menu_order_select_below;
    GtkWidget* layer_menu_order_select_bottom;
    GtkWidget* layer_menu_order_move_top;
    GtkWidget* layer_menu_order_move_up;
    GtkWidget* layer_menu_order_move_down;
    GtkWidget* layer_menu_order_move_bottom;
    GtkWidget* edit_menu_undo;            /* Edit > Undo menu item */
    GtkWidget* edit_menu_redo;            /* Edit > Redo menu item */
    GtkWidget* file_menu_save;            /* File > Save menu item */
    ToolRegistry* tool_registry;          /* Tool registry and management */
    ToolOptionsPanel* tool_options_panel; /* Tool options panel */
    Workspace* workspace;                 /* Workspace with accordion and panels */
    LayersPanel* layers_panel;            /* Layers panel (accessed via workspace) - kept for compatibility */
    Settings* settings;                   /* Application settings */
    gchar* app_dir;                       /* Application executable directory */
    gchar* size_unit;                     /* Current size unit for dimensions display (default: "px") */
    SwatchesData swatches;                /* Swatches data (main swatches and recent colors) */
};
typedef struct _AppContext AppContext;

/**
 * Apply crop if crop tool is active with a valid crop rectangle
 * @param ctx Application context (document and tool registry)
 * @return TRUE if crop was applied, FALSE otherwise
 */
gboolean crop_apply_if_active(AppContext* ctx);

/**
 * Create the main application UI
 * @return Initialized AppContext
 */
AppContext* ui_create_main_window(void);

/**
 * Create and attach a new document tab to the notebook
 * @param ctx The application context
 * @param filename The filename for the new document
 * @return The created ImageDocument
 */
ImageDocument* ui_create_document_tab(AppContext* ctx, const gchar* filename);
ImageDocument* ui_create_document_without_tab(AppContext* ctx, const gchar* filename);
void ui_add_document_to_notebook(AppContext* ctx, ImageDocument* doc);

/**
 * Close a document tab
 * @param ctx The application context
 * @param doc The document to close
 */
void ui_close_document_tab(AppContext* ctx, ImageDocument* doc);

/**
 * Get the current active document
 * @param ctx The application context
 * @return The active document, or NULL if none
 */
ImageDocument* ui_get_active_document(AppContext* ctx);

/**
 * Update the tab label for a document
 * @param ctx The application context
 * @param doc The document whose tab label should be updated
 */
void ui_update_document_tab_label(AppContext* ctx, ImageDocument* doc);

/**
 * Update the window title based on active document
 * @param ctx The application context
 * @param doc Optional document to use for title. If NULL, fetches active document.
 */
void ui_update_window_title(AppContext* ctx, ImageDocument* doc);

/**
 * Free the application context
 * @param ctx The application context
 */
void ui_context_free(AppContext* ctx);

/**
 * Update the "Open Recent" submenu with current recent files
 * @param ctx The application context
 */
void ui_update_recent_files_menu(AppContext* ctx);

/**
 * Update the status bar with document information
 * @param ctx The application context
 * @param doc Optional document to display. If NULL, fetches active document.
 */
void ui_update_status_bar(AppContext* ctx, ImageDocument* doc);

/**
 * Update menu and button sensitivity based on document and layer state
 * @param ctx The application context
 */
void ui_update_menu_and_button_states(AppContext* ctx);

/**
 * Save active document with file dialog
 * @param ctx The application context
 */
void ui_save_document_as(AppContext* ctx);

/**
 * Update the status bar time label with processing time
 * @param ctx The application context
 * @param time_seconds Processing time in seconds
 */
void ui_update_status_bar_time(AppContext* ctx, gdouble time_seconds);

/**
 * Update the status bar status label with a message
 * @param ctx The application context
 * @param message The message to display (or NULL to clear)
 */
void ui_update_status_bar_message(AppContext* ctx, const gchar* message);

/**
 * Update the cursor position display in the status bar
 * @param ctx The application context
 * @param doc The document
 * @param image_x X coordinate in image space
 * @param image_y Y coordinate in image space
 */
void ui_update_cursor_position(AppContext* ctx, ImageDocument* doc, gint image_x, gint image_y);

/**
 * Hide the cursor position display in the status bar
 * @param ctx The application context
 */
void ui_hide_cursor_position(AppContext* ctx);

/**
 * Show and start the progress bar with a message
 * @param ctx The application context
 * @param message The progress message to display
 */
void ui_show_progress(AppContext* ctx, const gchar* message);

/**
 * Update the progress bar (pulse animation)
 * @param ctx The application context
 */
void ui_update_progress(AppContext* ctx);

/**
 * Hide the progress bar
 * @param ctx The application context
 */
void ui_hide_progress(AppContext* ctx);

/**
 * Get canvas background color
 * @param ctx The application context
 * @param r Output parameter for red component (0.0-1.0)
 * @param g Output parameter for green component (0.0-1.0)
 * @param b Output parameter for blue component (0.0-1.0)
 */
void ui_get_canvas_background_color(AppContext* ctx, gdouble* r, gdouble* g, gdouble* b);

/**
 * Set canvas background color
 * @param ctx The application context
 * @param r Red component (0.0-1.0)
 * @param g Green component (0.0-1.0)
 * @param b Blue component (0.0-1.0)
 */
void ui_set_canvas_background_color(AppContext* ctx, gdouble r, gdouble g, gdouble b);

/**
 * Update canvas background color for all open documents
 * @param ctx The application context
 */
void ui_update_canvas_background_color(AppContext* ctx);

/**
 * Load tool options from settings
 * @param ctx The application context
 */
void ui_load_tool_options_from_settings(AppContext* ctx);

/**
 * Save tool options to settings
 * @param ctx The application context
 * @param tool_type The tool type
 */
void ui_save_tool_options_to_settings(AppContext* ctx, ToolType tool_type);

/**
 * Save all tool options to settings (saves all tools at once)
 * @param ctx The application context
 */
void ui_save_all_tool_options_to_settings(AppContext* ctx);

/**
 * Apply list-style combobox appearance (avoids empty space when popup opens
 * near screen edge). Use for zoom, blend mode, and similar dropdowns.
 * @param combo GtkComboBox widget to style
 */
void ui_apply_list_combobox_style(GtkWidget* combo);

/**
 * Callback for notify::popup-shown to fix popup alignment (avoids top whitespace).
 * Connect to combobox: g_signal_connect(combo, "notify::popup-shown",
 *     G_CALLBACK(ui_combo_popup_shown_fix), NULL);
 */
void ui_combo_popup_shown_fix(GObject* obj, GParamSpec* pspec, gpointer user_data);

#endif /* UI_H */
