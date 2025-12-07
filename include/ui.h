#ifndef UI_H
#define UI_H

#include <gtk/gtk.h>
#include "document.h"
#include "tools.h"
#include "command.h"

/**
 * Application context structure
 */
typedef struct {
    GtkWidget *window;           /* Main application window */
    GtkWidget *notebook;         /* Notebook widget for tabs */
    GtkWidget *menu_bar;         /* Menu bar */
    GtkWidget *status_bar;       /* Status bar */
    GList *documents;            /* List of open documents */
    GtkWidget *layer_menu_new;   /* Layer > New Layer menu item */
    GtkWidget *layer_menu_delete; /* Layer > Delete Layer menu item */
    GtkWidget *layer_menu_duplicate; /* Layer > Duplicate Layer menu item */
    GtkWidget *edit_menu_undo;   /* Edit > Undo menu item */
    GtkWidget *edit_menu_redo;   /* Edit > Redo menu item */
    ToolRegistry *tool_registry; /* Tool registry and management */
} AppContext;

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
ImageDocument* ui_create_document_tab(AppContext *ctx, const gchar *filename);

/**
 * Close a document tab
 * @param ctx The application context
 * @param doc The document to close
 */
void ui_close_document_tab(AppContext *ctx, ImageDocument *doc);

/**
 * Get the current active document
 * @param ctx The application context
 * @return The active document, or NULL if none
 */
ImageDocument* ui_get_active_document(AppContext *ctx);

/**
 * Update the window title based on active document
 * @param ctx The application context
 */
void ui_update_window_title(AppContext *ctx);

/**
 * Free the application context
 * @param ctx The application context
 */
void ui_context_free(AppContext *ctx);

/**
 * Update the status bar with document information
 * @param ctx The application context
 */
void ui_update_status_bar(AppContext *ctx);

/**
 * Update menu and button sensitivity based on document and layer state
 * @param ctx The application context
 */
void ui_update_menu_and_button_states(AppContext *ctx);

/**
 * Save active document with file dialog
 * @param ctx The application context
 */
void ui_save_document_as(AppContext *ctx);

#endif /* UI_H */

