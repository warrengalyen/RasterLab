#ifndef UI_H
#define UI_H

#include <gtk/gtk.h>
#include "document.h"

/**
 * Application context structure
 */
typedef struct {
    GtkWidget *window;           /* Main application window */
    GtkWidget *notebook;         /* Notebook widget for tabs */
    GtkWidget *menu_bar;         /* Menu bar */
    GList *documents;            /* List of open documents */
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

#endif /* UI_H */

