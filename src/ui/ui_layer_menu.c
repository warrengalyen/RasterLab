#include "ui/ui_layer_menu.h"
#include "commands/command_layer.h"
#include "document.h"
#include "ui.h"
#include "ui/dialogs/new_layer_dialog.h"
#include "ui/layers_panel.h"
#include <glib.h>

/**
 * Layer selection changed callback - proper signal handler signature
 */
void on_layer_selection_changed(GtkTreeSelection* selection, gpointer user_data) {
    (void)selection; /* Unused - we get it from the tree view */

    AppContext* ctx = (AppContext*)user_data;

    if (!ctx) {
        return;
    }

    /* Get the currently active document */
    ImageDocument* active_doc = ui_get_active_document(ctx);
    if (active_doc && ctx->layers_panel) {
        /* Set the selected layer in the document so tools use the right layer */
        ImageLayer* selected_layer = layers_panel_get_selected_layer(ctx->layers_panel);
        document_set_selected_layer(active_doc, selected_layer);

        /* Update opacity controls to reflect selected layer */
        layers_panel_update_opacity_controls(ctx->layers_panel);
    }

    /* Update menu and button states */
    ui_update_menu_and_button_states(ctx);
}

/**
 * Create the Layer menu
 */
GtkWidget* create_layer_menu(AppContext* ctx) {
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* menu_item;

    /* Layer > New Layer */
    menu_item = gtk_menu_item_new_with_mnemonic("_New Layer");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_layer_new), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    ctx->layer_menu_new = menu_item;

    /* Layer > Duplicate Layer */
    menu_item = gtk_menu_item_new_with_mnemonic("_Duplicate Layer");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_layer_duplicate), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    ctx->layer_menu_duplicate = menu_item;

    /* Separator */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    /* Layer > Delete Layer */
    menu_item = gtk_menu_item_new_with_mnemonic("_Delete Layer");
    g_signal_connect(menu_item, "activate", G_CALLBACK(on_layer_delete), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    ctx->layer_menu_delete = menu_item;

    gtk_widget_show_all(menu);

    return menu;
}

/**
 * Setup Layer menu from Glade builder
 */
void ui_layer_menu_setup(GtkBuilder* builder, AppContext* ctx) {
    GtkWidget* layer_menu = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu"));
    GtkWidget* layer_menu_item = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_item"));

    if (layer_menu && layer_menu_item) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(layer_menu_item), layer_menu);
    }

    /* Get menu items that need to be updated programmatically */
    ctx->layer_menu_new = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_new"));
    ctx->layer_menu_duplicate = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_duplicate"));
    ctx->layer_menu_delete = GTK_WIDGET(gtk_builder_get_object(builder, "layer_menu_delete"));

    /* Connect Layer menu signals */
    if (ctx->layer_menu_new) {
        g_signal_connect(ctx->layer_menu_new, "activate", G_CALLBACK(on_layer_new), ctx);
    }
    if (ctx->layer_menu_duplicate) {
        g_signal_connect(ctx->layer_menu_duplicate, "activate", G_CALLBACK(on_layer_duplicate), ctx);
    }
    if (ctx->layer_menu_delete) {
        g_signal_connect(ctx->layer_menu_delete, "activate", G_CALLBACK(on_layer_delete), ctx);
    }
}

/**
 * Layer > New Layer callback
 */
void on_layer_new(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;
    NewLayerDialog* dialog;
    NewLayerDialogResult* result;
    gint response;
    ImageLayer* new_layer;
    const gdouble* custom_color = NULL;

    if (!doc) {
        g_warning("No document open");
        return;
    }

    /* Create and show new layer dialog */
    dialog = new_layer_dialog_new();
    if (!dialog) {
        g_warning("Failed to create new layer dialog");
        return;
    }

    response = new_layer_dialog_run(dialog, GTK_WINDOW(ctx->window), &result);

    if (response == GTK_RESPONSE_OK && result) {
        /* Get custom color if needed */
        if (result->background == LAYER_BACKGROUND_CUSTOM) {
            custom_color = result->custom_color;
        }

        /* Create new layer with dialog parameters */
        new_layer = document_add_layer(doc, result->name,
                                       result->background,
                                       result->position,
                                       custom_color);

        if (new_layer) {
            /* Set as active layer if requested */
            if (result->set_active) {
                document_set_selected_layer(doc, new_layer);
            }

            /* Create undo command */
            cmd = command_create_layer_add(doc, new_layer);
            if (cmd && doc->undo_stack) {
                command_stack_push(doc->undo_stack, cmd);

                /* Clear redo stack */
                if (doc->redo_stack) {
                    command_stack_clear(doc->redo_stack);
                }
            } else if (cmd) {
                command_free(cmd);
            }

            /* Update layers panel */
            if (layers_panel) {
                layers_panel_update(layers_panel, doc);

                /* Select the new layer if requested */
                if (result->set_active) {
                    layers_panel_select_layer(layers_panel, doc, new_layer);
                }
            }

            /* Update UI state */
            ui_update_menu_and_button_states(ctx);
            ui_update_window_title(ctx, NULL);
            doc->modified = TRUE;
        }

        /* Free dialog result */
        new_layer_dialog_result_free(result);
    }

    /* Free dialog */
    new_layer_dialog_free(dialog);
}

/**
 * Layer > Delete Layer callback
 */
void on_layer_delete(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Delete the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        g_warning("No layer selected");
        return;
    }

    /* Create undo command before deleting */
    cmd = command_create_layer_delete(doc, selected_layer);
    if (!cmd) {
        g_warning("Failed to create delete layer command");
        return;
    }

    /* Execute the delete (apply the command) */
    command_execute(cmd, doc);

    /* Push to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);

        /* Clear redo stack */
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
    } else {
        command_free(cmd);
    }

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Layer > Duplicate Layer callback
 */
void on_layer_duplicate(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Duplicate the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        g_warning("No layer selected");
        return;
    }

    static int dup_count = 1;
    gchar* layer_name = g_strdup_printf("%s copy %d", selected_layer->name, dup_count++);

    ImageLayer* dup_layer = document_duplicate_layer(doc, selected_layer, layer_name);
    g_free(layer_name);

    if (dup_layer) {
        /* Create undo command */
        cmd = command_create_layer_duplicate(doc, selected_layer, dup_layer);
        if (cmd && doc->undo_stack) {
            command_stack_push(doc->undo_stack, cmd);

            /* Clear redo stack */
            if (doc->redo_stack) {
                command_stack_clear(doc->redo_stack);
            }
        } else if (cmd) {
            command_free(cmd);
        }

        /* Update layers panel */
        if (layers_panel) {
            layers_panel_update(layers_panel, doc);
            /* Select the duplicated layer */
            document_set_selected_layer(doc, dup_layer);
            layers_panel_select_layer(layers_panel, doc, dup_layer);
        }

        /* Update UI state */
        ui_update_menu_and_button_states(ctx);
        ui_update_window_title(ctx, NULL);
        doc->modified = TRUE;
    }
}

/**
 * Layer > Move Up callback
 */
void on_layer_move_up(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Get the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        g_warning("No layer selected");
        return;
    }

    /* Create undo command before moving */
    cmd = command_create_layer_move_up(doc, selected_layer);
    if (!cmd) {
        return; /* Can't move up */
    }

    /* Execute the move (apply the command) */
    command_execute(cmd, doc);

    /* Push to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);

        /* Clear redo stack */
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
    } else {
        command_free(cmd);
    }

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    doc->modified = TRUE;
}

/**
 * Layer > Move Down callback
 */
void on_layer_move_down(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */

    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");
    Command* cmd;

    if (!doc || !doc->layers) {
        g_warning("No document or layers");
        return;
    }

    /* Get the currently selected layer */
    ImageLayer* selected_layer = NULL;
    if (layers_panel) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
    }

    if (!selected_layer) {
        g_warning("No layer selected");
        return;
    }

    /* Create undo command before moving */
    cmd = command_create_layer_move_down(doc, selected_layer);
    if (!cmd) {
        return; /* Can't move down */
    }

    /* Execute the move (apply the command) */
    command_execute(cmd, doc);

    /* Push to undo stack */
    if (doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);

        /* Clear redo stack */
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }
    } else {
        command_free(cmd);
    }

    /* Update layers panel */
    if (layers_panel) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update UI state */
    ui_update_menu_and_button_states(ctx);
    ui_update_window_title(ctx, NULL);
    doc->modified = TRUE;
}
