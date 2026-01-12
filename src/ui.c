#include "ui.h"
#include "app/autosave.h"
#include "app/recent_files.h"
#include "app/settings.h"
#include "command.h"
#include "document.h"
#include "filters.h"
#include "ocular.h"
#include "panels.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "render/tile.h"
#include "render/tile_worker.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"
#include "selection/selection_undo.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "ui/dialogs/canvas_size_dialog.h"
#include "ui/dialogs/new_layer_dialog.h"
#include "ui/dialogs/recovery_dialog.h"
#include "ui/dialogs/selection_radius_dialog.h"
#include "ui/layers_panel.h"
#include "ui/tool_options_panel.h"
#include "ui/tools_panel.h"
#include "ui/ui_edit_menu.h"
#include "ui/ui_file_menu.h"
#include "ui/ui_filter.h"
#include "ui/ui_filter_adjust.h"
#include "ui/ui_filter_effects.h"
#include "ui/ui_image_menu.h"
#include "ui/ui_layer_menu.h"
#include "ui/ui_select_menu.h"
#include "ui/ui_view_menu.h"
#include "undo/undo_disk.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Log handler to suppress harmless GTK Builder menu warnings */
static gboolean gtk_builder_menu_warning_handler(const gchar* log_domain,
                                                 GLogLevelFlags log_level,
                                                 const gchar* message,
                                                 gpointer user_data) {
    (void)log_domain;
    (void)log_level;
    (void)user_data;

    /* Suppress warnings about adding GtkMenu to GtkMenuItem */
    if (message && strstr(message, "Cannot add an object of type GtkMenu")) {
        return TRUE; /* Suppress this warning */
    }

    return FALSE; /* Let other warnings through */
}

/* Forward declarations */
static void on_notebook_switch_page(GtkNotebook* notebook, GtkWidget* page,
                                    guint page_num, gpointer user_data);
static void on_tab_close_button_clicked(GtkButton* button, gpointer user_data);
static void setup_adjust_menu(GtkBuilder* builder, AppContext* ctx);
static void setup_effects_menu(GtkBuilder* builder, AppContext* ctx);
static void on_statusbar_zoom_out(GtkWidget* widget, gpointer data);
static void on_statusbar_zoom_in(GtkWidget* widget, gpointer data);
static void on_statusbar_zoom_changed(GtkComboBox* combo, gpointer data);
static void on_statusbar_size_unit_changed(GtkComboBox* combo, gpointer data);
static gboolean zoom_combo_row_separator_func(GtkTreeModel* model, GtkTreeIter* iter, gpointer data);

/**
 * Create the main application UI
 */
AppContext* ui_create_main_window(void) {
    AppContext* ctx = (AppContext*)g_malloc(sizeof(AppContext));
    GtkWidget* tools_panel;
    GtkWidget* layers_panel_widget;

    ctx->documents = NULL;
    ctx->layer_menu_new = NULL;
    ctx->layer_menu_delete = NULL;
    ctx->layer_menu_duplicate = NULL;
    ctx->edit_menu_undo = NULL;
    ctx->edit_menu_redo = NULL;
    ctx->layers_panel = NULL;        /* Initialize layers_panel early */
    ctx->settings = NULL;            /* Will be set in main.c */
    ctx->app_dir = NULL;             /* Will be set in main.c */
    ctx->size_unit = g_strdup("px"); /* Default size unit is pixels */

    /* Create and initialize tool manager */
    ctx->tool_registry = tool_manager_new();
    if (!tool_manager_init_defaults(ctx->tool_registry)) {
        g_warning("Failed to initialize tool manager");
        g_free(ctx);
        return NULL;
    }

    /* Set AppContext reference on all tools */
    if (ctx->tool_registry) {
        for (int i = 0; i < TOOL_COUNT; i++) {
            Tool* tool = tool_manager_get(ctx->tool_registry, i);
            if (tool) {
                tool->app_context = (gpointer)ctx;
            }
        }
    }

    /* Suppress GTK Builder warnings about menus (these are harmless when using standalone menus) */
    static gboolean menu_warning_suppressed = FALSE;
    if (!menu_warning_suppressed) {
        g_log_set_handler("Gtk", G_LOG_LEVEL_WARNING,
                          (GLogFunc)gtk_builder_menu_warning_handler, NULL);
        menu_warning_suppressed = TRUE;
    }

    /* Load main window from Glade */
    GtkBuilder* builder = gtk_builder_new_from_resource("/ui/main_window.glade");
    if (!builder) {
        g_warning("Failed to load main window from Glade");
        g_free(ctx);
        return NULL;
    }

    /* Get main window */
    ctx->window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    if (!ctx->window) {
        g_warning("Failed to get main_window from Glade");
        g_object_unref(builder);
        g_free(ctx);
        return NULL;
    }
    gtk_window_set_icon_name(GTK_WINDOW(ctx->window), "image-editor");

    /* Get main containers */
    GtkWidget* main_vbox = GTK_WIDGET(gtk_builder_get_object(builder, "main_vbox"));
    GtkWidget* tool_options_container = GTK_WIDGET(gtk_builder_get_object(builder, "tool_options_container"));
    GtkWidget* main_hbox = GTK_WIDGET(gtk_builder_get_object(builder, "main_hbox"));
    ctx->notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook"));
    GtkWidget* right_panel_container = GTK_WIDGET(gtk_builder_get_object(builder, "right_panel_container"));
    ctx->status_bar = GTK_WIDGET(gtk_builder_get_object(builder, "status_bar"));

    if (!main_vbox || !tool_options_container || !main_hbox ||
        !ctx->notebook || !right_panel_container || !ctx->status_bar) {
        g_warning("Failed to get required widgets from Glade");
        g_object_unref(builder);
        g_free(ctx);
        return NULL;
    }

    /* Store builder reference in window for later use (e.g., status bar labels) */
    g_object_ref(builder);
    g_object_set_data_full(G_OBJECT(ctx->window), "main_builder", builder, g_object_unref);

    /* Connect notebook signal */
    g_signal_connect(ctx->notebook, "switch-page",
                     G_CALLBACK(on_notebook_switch_page), ctx);

    /* Get menu bar and menu items from Glade */
    ctx->menu_bar = GTK_WIDGET(gtk_builder_get_object(builder, "menu_bar"));

    /* Create accelerator group for keyboard shortcuts */
    GtkAccelGroup* accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(ctx->window), accel_group);

    /* Setup each menu separately */
    ui_file_menu_setup(builder, ctx, accel_group);
    ui_edit_menu_setup(builder, ctx, accel_group);
    ui_view_menu_setup(builder, ctx, accel_group);
    ui_image_menu_setup(builder, ctx);
    ui_layer_menu_setup(builder, ctx);
    ui_select_menu_setup(builder, ctx, accel_group);
    setup_adjust_menu(builder, ctx);
    setup_effects_menu(builder, ctx);

    /* ==== TOP PANEL: Tool Options ==== */
    ctx->tool_options_panel = create_tool_options_panel();
    if (!ctx->tool_options_panel || !ctx->tool_options_panel->panel) {
        g_warning("Failed to create tool options panel");
        g_object_unref(builder);
        g_free(ctx);
        return NULL;
    }
    /* Set tool registry in panel for cursor updates */
    tool_options_panel_set_tool_registry(ctx->tool_options_panel, ctx->tool_registry);
    /* Store panel reference in panel widget */
    g_object_set_data(G_OBJECT(ctx->tool_options_panel->panel), "tool_options_panel", ctx->tool_options_panel);
    gtk_container_add(GTK_CONTAINER(tool_options_container), ctx->tool_options_panel->panel);

    /* ==== LEFT PANEL: Tools (fixed width) ==== */
    /* Tool panel is now loaded from main window Glade file */
    tools_panel = tools_panel_initialize_from_builder(builder, ctx->tool_registry);

    /* Connect tools panel to tool options panel for title updates */
    tools_panel_set_options_panel(ctx->tool_options_panel);

    /* Set main window reference for color chooser dialogs */
    tools_panel_set_main_window(GTK_WINDOW(ctx->window));

    /* ==== RIGHT PANEL: Layers ==== */
    ctx->layers_panel = create_layers_panel();
    layers_panel_widget = ctx->layers_panel->panel;
    gtk_container_add(GTK_CONTAINER(right_panel_container), layers_panel_widget);

    /* Store layers panel reference for later updates */
    g_object_set_data(G_OBJECT(ctx->window), "layers_panel", ctx->layers_panel);

    /* Connect layers panel buttons to callbacks */
    layers_panel_connect_buttons(ctx->layers_panel,
                                 G_CALLBACK(on_layer_new),
                                 G_CALLBACK(on_layer_delete),
                                 G_CALLBACK(on_layer_duplicate),
                                 ctx);

    /* Connect move layer buttons */
    if (ctx->layers_panel->btn_up) {
        g_signal_connect(ctx->layers_panel->btn_up, "clicked",
                         G_CALLBACK(on_layer_move_up), ctx);
    }
    if (ctx->layers_panel->btn_down) {
        g_signal_connect(ctx->layers_panel->btn_down, "clicked",
                         G_CALLBACK(on_layer_move_down), ctx);
    }

    /* Connect layer tree view selection changes to update UI state */
    GtkTreeSelection* layer_selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(ctx->layers_panel->tree_view));
    g_signal_connect(layer_selection, "changed",
                     G_CALLBACK(on_layer_selection_changed), ctx);

    /* Setup status bar zoom controls */
    GtkWidget* sb_zoom_out_button = GTK_WIDGET(gtk_builder_get_object(builder, "sb_zoom_out_button"));
    GtkWidget* sb_zoom_in_button = GTK_WIDGET(gtk_builder_get_object(builder, "sb_zoom_in_button"));
    GtkWidget* sb_zoom_combobox = GTK_WIDGET(gtk_builder_get_object(builder, "sb_zoom_combobox"));
    GtkWidget* sb_size_unit_combobox = GTK_WIDGET(gtk_builder_get_object(builder, "sb_size_unit_combobox"));

    if (sb_zoom_out_button) {
        g_signal_connect(sb_zoom_out_button, "clicked",
                         G_CALLBACK(on_statusbar_zoom_out), ctx);
    }
    if (sb_zoom_in_button) {
        g_signal_connect(sb_zoom_in_button, "clicked",
                         G_CALLBACK(on_statusbar_zoom_in), ctx);
    }
    if (sb_zoom_combobox) {
        /* Set up separator rendering */
        gtk_combo_box_set_row_separator_func(GTK_COMBO_BOX(sb_zoom_combobox),
                                             zoom_combo_row_separator_func,
                                             NULL, NULL);

        /* Connect change signal */
        g_signal_connect(sb_zoom_combobox, "changed",
                         G_CALLBACK(on_statusbar_zoom_changed), ctx);
    }
    if (sb_size_unit_combobox) {
        /* Connect change signal */
        g_signal_connect(sb_size_unit_combobox, "changed",
                         G_CALLBACK(on_statusbar_size_unit_changed), ctx);
    }

    /* Connect window signals */
    g_signal_connect(ctx->window, "delete-event",
                     G_CALLBACK(on_window_delete), ctx);

    gtk_widget_show_all(ctx->window);

    /* Update status bar with initial information */
    ui_update_status_bar(ctx, NULL);

    /* Initialize menu and button states */
    ui_update_menu_and_button_states(ctx);

    /* Initialize recent files menu (must be before g_object_unref(builder)) */
    ui_update_recent_files_menu(ctx);

    /* Clean up builder - widgets are now owned by their containers */
    g_object_unref(builder);

    /* Check for recovery files and show dialog if found */
    recovery_dialog_show(ctx);

    // printf("Main window created with dockable panels and status bar\n");

    return ctx;
}

/**
 * Create and attach a new document tab
 */
ImageDocument* ui_create_document_tab(AppContext* ctx, const gchar* filename) {
    ImageDocument* doc;
    GtkWidget* page_content;
    GtkWidget* tab_hbox;
    GtkWidget* tab_label;
    GtkWidget* close_button;
    gint page_num;

    /* Create the document with worker pool for on-screen rendering */
    doc = document_new(filename, TRUE);

    /* Create disk-backed undo journal if settings are available */
    if (ctx && ctx->settings && doc) {
        gint compression_level = settings_get_undo_compression_level(ctx->settings);
        const gchar* temp_dir = settings_get_undo_temp_directory(ctx->settings);
        doc->undo_journal = undo_journal_create((struct ImageDocument*)doc, temp_dir, compression_level);
        if (!doc->undo_journal) {
            g_warning("Failed to create undo journal, falling back to in-memory undo");
        }
    }

    /* Create the drawing area (scrolled window with drawing area) */
    page_content = document_create_drawing_area(doc);

    /* Create tab label with close button */
    tab_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(tab_hbox), 0);

    tab_label = gtk_label_new(filename);
    gtk_box_pack_start(GTK_BOX(tab_hbox), tab_label, FALSE, FALSE, 0);

    close_button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(close_button, FALSE);
    gtk_widget_set_size_request(close_button, 20, 20);
    GtkWidget* close_image = gtk_image_new_from_icon_name("window-close-symbolic",
                                                          GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(close_button), close_image);
    g_signal_connect(close_button, "clicked",
                     G_CALLBACK(on_tab_close_button_clicked), doc);
    gtk_box_pack_start(GTK_BOX(tab_hbox), close_button, FALSE, FALSE, 0);

    gtk_widget_show_all(tab_hbox);
    gtk_widget_show_all(page_content);

    /* Add document to list BEFORE adding page to notebook
     * This ensures that when switch-page signal fires, the document is already in the list */
    ctx->documents = g_list_append(ctx->documents, doc);

    /* Add page to notebook */
    page_num = gtk_notebook_append_page(GTK_NOTEBOOK(ctx->notebook),
                                        page_content, tab_hbox);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(ctx->notebook), page_num);

    /* Store label in close button's data for later reference */
    g_object_set_data(G_OBJECT(close_button), "tab_label", tab_label);
    g_object_set_data(G_OBJECT(close_button), "app_context", ctx);

    /* Store AppContext and tool registry in drawing area for tool event handlers */
    if (doc && doc->drawing_area) {
        g_object_set_data(G_OBJECT(doc->drawing_area), "app_context", ctx);
        if (ctx->tool_registry) {
            g_object_set_data(G_OBJECT(doc->drawing_area), "tool_registry", ctx->tool_registry);
        }
        /* Store layers panel reference for thumbnail updates */
        if (ctx->layers_panel) {
            g_object_set_data(G_OBJECT(doc->drawing_area), "layers_panel", ctx->layers_panel);
        }
    }

    /* Update viewport background color with current canvas background color using CSS */
    if (doc && doc->viewport) {
        gdouble r_val, g_val, b_val;
        ui_get_canvas_background_color(ctx, &r_val, &g_val, &b_val);
        guint r = (guint)(r_val * 255.0);
        guint g = (guint)(g_val * 255.0);
        guint b = (guint)(b_val * 255.0);

        /* Get or create CSS provider for this viewport */
        GtkCssProvider* provider = (GtkCssProvider*)g_object_get_data(G_OBJECT(doc->viewport), "canvas_bg_provider");
        if (!provider) {
            provider = gtk_css_provider_new();
            g_object_set_data_full(G_OBJECT(doc->viewport), "canvas_bg_provider", provider, g_object_unref);
            GtkStyleContext* style_context = gtk_widget_get_style_context(doc->viewport);
            gtk_style_context_add_provider(style_context, GTK_STYLE_PROVIDER(provider),
                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }

        gchar* css = g_strdup_printf("#canvas-viewport { background-color: rgb(%u, %u, %u); }", r, g, b);
        gtk_css_provider_load_from_data(provider, css, -1, NULL);
        g_free(css);
        gtk_widget_queue_draw(doc->viewport);
    }

    /* Set the current document in tool registry so tools can access it */
    if (ctx->tool_registry) {
        ctx->tool_registry->current_doc = doc;
    }

    /* Register document for autosave */
    autosave_register_document(doc);

    /* Update window title (pass doc to ensure correct title) */
    ui_update_window_title(ctx, doc);

    return doc;
}

/**
 * Close a document tab
 */
/**
 * Internal: Actually close the document without prompting
 */
static void ui_close_document_tab_internal(AppContext* ctx, ImageDocument* doc) {
    gint page_num;
    GtkWidget* scrolled_window;

    if (!doc || !ctx) {
        return;
    }

    /* Store scrolled window pointer before we modify anything */
    scrolled_window = doc->scrolled_window;

    /* Find the page containing this document */
    if (!scrolled_window) {
        /* Document already has no scrolled window, just free it */
        ctx->documents = g_list_remove(ctx->documents, doc);
        document_free(doc);
        return;
    }

    page_num = gtk_notebook_page_num(GTK_NOTEBOOK(ctx->notebook), scrolled_window);

    if (page_num >= 0) {
        /* Disconnect all signals from drawing area to prevent callbacks
         * from accessing the document after it's freed */
        if (doc->drawing_area) {
            g_signal_handlers_disconnect_by_data(doc->drawing_area, doc);
        }

        /* NULL out widget pointers BEFORE removing page to prevent
         * access to destroyed widgets during cleanup and signal handlers */
        doc->scrolled_window = NULL;
        doc->drawing_area = NULL;

        /* Remove from document list BEFORE removing page to prevent
         * switch-page callback from accessing the document */
        ctx->documents = g_list_remove(ctx->documents, doc);

        /* Unregister from autosave before freeing */
        autosave_unregister_document(doc);

        /* Remove the notebook page (this destroys the scrolled window
         * and may trigger switch-page signal, but doc is already removed) */
        gtk_notebook_remove_page(GTK_NOTEBOOK(ctx->notebook), page_num);

        /* Free the document */
        document_free(doc);
        doc = NULL; /* Ensure doc pointer is NULL after freeing */

        /* Update window title (handles empty notebook) */
        ui_update_window_title(ctx, NULL);

        /* Update status bar and menu/button states */
        ui_update_status_bar(ctx, NULL);

        /* Update layers panel - show active document if one exists, otherwise clear */
        LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                    "layers_panel");
        if (layers_panel) {
            ImageDocument* active_doc = ui_get_active_document(ctx);
            layers_panel_update(layers_panel, active_doc);
        }

        ui_update_menu_and_button_states(ctx);
    }
}

/**
 * Public: Close document with unsaved changes prompt
 */
void ui_close_document_tab(AppContext* ctx, ImageDocument* doc) {
    if (!doc || !ctx) {
        return;
    }

    /* Check if document has unsaved changes */
    if (document_is_dirty(doc)) {
        GtkWidget* dialog;
        gint response;
        const gchar* filename = document_get_filename(doc);

        /* Create confirmation dialog */
        dialog = gtk_message_dialog_new(
            GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_NONE,
            "Save changes to \"%s\" before closing?",
            filename);

        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog),
            "If you don't save, changes will be lost.");

        /* Add buttons */
        gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                               "_Discard", GTK_RESPONSE_REJECT,
                               "_Cancel", GTK_RESPONSE_CANCEL,
                               "_Save", GTK_RESPONSE_ACCEPT,
                               NULL);

        /* Set default button to Save */
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

        /* Show dialog and get response */
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        switch (response) {
            case GTK_RESPONSE_ACCEPT:
                /* User clicked Save */
                // printf("User wants to save before closing\n");
                ui_save_document_as(ctx);
                /* Note: We don't actually close here - let user complete save */
                /* In a full implementation, we'd detect when save completes */
                break;

            case GTK_RESPONSE_REJECT:
                /* User clicked Discard */
                // printf("User discarding changes\n");
                ui_close_document_tab_internal(ctx, doc);
                break;

            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
                /* User clicked Cancel or closed dialog */
                // printf("User cancelled close operation\n");
                break;

            default:
                break;
        }
    } else {
        /* No unsaved changes, close directly */
        ui_close_document_tab_internal(ctx, doc);
    }
}

/**
 * Get the current active document
 */
ImageDocument* ui_get_active_document(AppContext* ctx) {
    gint page_num;
    GtkWidget* page;

    if (!ctx) {
        return NULL;
    }

    page_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(ctx->notebook));

    if (page_num < 0) {
        return NULL;
    }

    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(ctx->notebook), page_num);

    if (!page) {
        return NULL;
    }

    /* Find the document by scrolled window */
    for (GList* iter = ctx->documents; iter; iter = iter->next) {
        ImageDocument* doc = (ImageDocument*)iter->data;
        if (doc && doc->scrolled_window == page) {
            return doc;
        }
    }

    return NULL;
}

/**
 * Update the window title based on active document
 */
void ui_update_window_title(AppContext* ctx, ImageDocument* doc) {
    ImageDocument* active_doc;
    gchar* title;

    if (!ctx) {
        return;
    }

    /* Use provided document, or query for active document if not provided */
    active_doc = doc ? doc : ui_get_active_document(ctx);

    if (active_doc) {
        const gchar* filename = document_get_filename(active_doc);
        const gchar* modified = active_doc->modified ? "*" : "";
        title = g_strdup_printf("Image Editor - %s%s", filename, modified);
    } else {
        title = g_strdup("Image Editor");
    }

    gtk_window_set_title(GTK_WINDOW(ctx->window), title);
    g_free(title);
}

/**
 * Free the application context
 */
void ui_context_free(AppContext* ctx) {
    if (!ctx) {
        return;
    }

    /* Free all documents */
    for (GList* iter = ctx->documents; iter; iter = iter->next) {
        document_free((ImageDocument*)iter->data);
    }

    g_list_free(ctx->documents);

    /* Free tool manager */
    if (ctx->tool_registry) {
        tool_manager_free(ctx->tool_registry);
    }

    /* Free settings (will be saved in main.c before this is called) */
    if (ctx->settings) {
        settings_free(ctx->settings);
    }

    /* Free app directory */
    if (ctx->app_dir) {
        g_free(ctx->app_dir);
    }

    /* Free size unit */
    if (ctx->size_unit) {
        g_free(ctx->size_unit);
    }

    g_free(ctx);
}

/**
 * Setup Adjustments menu from Glade builder
 */
static void setup_adjust_menu(GtkBuilder* builder, AppContext* ctx) {
    ui_filter_adjust_setup_menu(builder, ctx);
}

/**
 * Setup Effects menu from Glade builder
 */
static void setup_effects_menu(GtkBuilder* builder, AppContext* ctx) {
    ui_filter_effects_setup_menu(builder, ctx);
}

/**
 * Helper function to extract pixels from a layer for copying
 * Returns a cairo surface with the pixels to copy (selection or entire layer)
 */
static cairo_surface_t* extract_pixels_for_copy(ImageDocument* doc, ImageLayer* layer) {
    if (!doc || !layer || !layer->surface) {
        return NULL;
    }

    gint width = layer->width;
    gint height = layer->height;

    /* Check if there's a selection */
    gboolean has_selection = (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));

    if (!has_selection) {
        /* No selection: copy entire layer */
        cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        if (!copy) {
            return NULL;
        }

        cairo_t* cr = cairo_create(copy);
        cairo_set_source_surface(cr, layer->surface, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);

        return copy;
    }

    /* Has selection: extract selected pixels */
    /* Calculate intersection of layer bounds and selection bounds in document coordinates */
    gint layer_x_min = layer->offset_x;
    gint layer_y_min = layer->offset_y;
    gint layer_x_max = layer->offset_x + layer->width;
    gint layer_y_max = layer->offset_y + layer->height;

    /* Clamp to document bounds */
    layer_x_min = (layer_x_min < 0) ? 0 : layer_x_min;
    layer_y_min = (layer_y_min < 0) ? 0 : layer_y_min;
    layer_x_max = (layer_x_max > (gint)doc->width) ? (gint)doc->width : layer_x_max;
    layer_y_max = (layer_y_max > (gint)doc->height) ? (gint)doc->height : layer_y_max;

    if (layer_x_max <= layer_x_min || layer_y_max <= layer_y_min) {
        return NULL; /* No intersection */
    }

    /* Create dirty rect for the layer region in document coordinates */
    DirtyRect layer_rect;
    dirty_rect_set(&layer_rect, layer_x_min, layer_y_min,
                   layer_x_max - layer_x_min, layer_y_max - layer_y_min);

    /* Get selection mask for this region */
    DirtyRect actual_region;
    SelectionMask* region_mask = selection_build_combined_mask(
        doc->selection_mask, &layer_rect, FEATHER_QUALITY_NORMAL, &actual_region);

    if (!region_mask || !region_mask->data || dirty_rect_is_empty(&actual_region)) {
        if (region_mask) {
            selection_mask_free(region_mask);
        }
        return NULL;
    }

    /* Find bounding box of selected pixels within the region */
    gint sel_x_min = actual_region.x + actual_region.width;
    gint sel_y_min = actual_region.y + actual_region.height;
    gint sel_x_max = actual_region.x;
    gint sel_y_max = actual_region.y;

    /* Scan mask to find actual bounds */
    for (gint y = 0; y < region_mask->height; y++) {
        for (gint x = 0; x < region_mask->width; x++) {
            uint8_t mask_alpha = region_mask->data[y * region_mask->stride + x];
            if (mask_alpha > 0) {
                gint doc_x = actual_region.x + x;
                gint doc_y = actual_region.y + y;
                if (doc_x < sel_x_min)
                    sel_x_min = doc_x;
                if (doc_y < sel_y_min)
                    sel_y_min = doc_y;
                if (doc_x > sel_x_max)
                    sel_x_max = doc_x;
                if (doc_y > sel_y_max)
                    sel_y_max = doc_y;
            }
        }
    }

    if (sel_x_max < sel_x_min || sel_y_max < sel_y_min) {
        selection_mask_free(region_mask);
        return NULL; /* No selected pixels */
    }

    /* Create surface with bounding box dimensions */
    gint new_width = sel_x_max - sel_x_min + 1;
    gint new_height = sel_y_max - sel_y_min + 1;

    cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!copy) {
        selection_mask_free(region_mask);
        return NULL;
    }

    /* Copy pixels from source layer to copy, masked by selection */
    cairo_surface_flush(layer->surface);
    cairo_surface_flush(copy);

    guchar* src_data = cairo_image_surface_get_data(layer->surface);
    gint src_stride = cairo_image_surface_get_stride(layer->surface);
    guchar* dst_data = cairo_image_surface_get_data(copy);
    gint dst_stride = cairo_image_surface_get_stride(copy);

    for (gint y = 0; y < new_height; y++) {
        gint doc_y = sel_y_min + y;
        gint src_y = doc_y - layer->offset_y;
        gint mask_y = doc_y - actual_region.y;

        if (src_y < 0 || src_y >= (gint)layer->height) {
            continue; /* Outside source layer */
        }
        if (mask_y < 0 || mask_y >= region_mask->height) {
            continue; /* Outside mask */
        }

        for (gint x = 0; x < new_width; x++) {
            gint doc_x = sel_x_min + x;
            gint src_x = doc_x - layer->offset_x;
            gint mask_x = doc_x - actual_region.x;

            if (src_x < 0 || src_x >= (gint)layer->width) {
                continue; /* Outside source layer */
            }
            if (mask_x < 0 || mask_x >= region_mask->width) {
                continue; /* Outside mask */
            }

            uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
            if (mask_alpha == 0) {
                continue; /* Not selected */
            }

            /* Copy pixel from source to destination */
            guchar* src_pixel = src_data + src_y * src_stride + src_x * 4;
            guchar* dst_pixel = dst_data + y * dst_stride + x * 4;

            if (mask_alpha == 255) {
                /* Fully selected: copy pixel directly */
                dst_pixel[0] = src_pixel[0];
                dst_pixel[1] = src_pixel[1];
                dst_pixel[2] = src_pixel[2];
                dst_pixel[3] = src_pixel[3];
            } else {
                /* Partially selected (feathered): apply mask to alpha */
                uint8_t src_a = src_pixel[3];
                uint8_t new_alpha = (uint8_t)((src_a * mask_alpha) / 255);

                if (new_alpha == 0) {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                } else if (src_a > 0) {
                    /* Un-premultiply, then re-premultiply with new alpha */
                    uint16_t r = (src_pixel[2] * 255 + src_a / 2) / src_a;
                    uint16_t g = (src_pixel[1] * 255 + src_a / 2) / src_a;
                    uint16_t b = (src_pixel[0] * 255 + src_a / 2) / src_a;

                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;

                    dst_pixel[0] = (b * new_alpha + 127) / 255;
                    dst_pixel[1] = (g * new_alpha + 127) / 255;
                    dst_pixel[2] = (r * new_alpha + 127) / 255;
                    dst_pixel[3] = new_alpha;
                } else {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                }
            }
        }
    }

    cairo_surface_mark_dirty(copy);
    selection_mask_free(region_mask);

    return copy;
}

/**
 * Helper function to create a merged surface from all visible layers
 * Returns a cairo surface with all visible layers composited, or NULL on error
 */
static cairo_surface_t* create_merged_surface(ImageDocument* doc) {
    if (!doc || doc->width == 0 || doc->height == 0) {
        return NULL;
    }

    /* Create surface for merged result */
    cairo_surface_t* merged = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, doc->width, doc->height);
    if (!merged) {
        return NULL;
    }

    /* Composite all visible layers */
    cairo_t* cr = cairo_create(merged);
    if (!cr) {
        cairo_surface_destroy(merged);
        return NULL;
    }

    /* Clear to transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Composite each visible layer */
    gboolean is_first_layer = TRUE;
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        ImageLayer* layer = (ImageLayer*)iter->data;

        if (!layer || !layer->surface) {
            continue;
        }

        /* Skip invisible layers */
        if (!layer->visible || layer->opacity <= 0.0) {
            continue;
        }

        /* Ensure layer cache is up to date */
        if (!layer_ensure_cache(layer)) {
            continue;
        }

        /* Draw layer with offset */
        cairo_save(cr);
        cairo_translate(cr, layer->offset_x, layer->offset_y);
        cairo_set_source_surface(cr, layer->cache_surface, 0, 0);

        /* Set operator based on layer's blend mode */
        cairo_operator_t op;
        if (is_first_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(merged);

    return merged;
}

/**
 * Helper function to extract pixels from merged layers for copying
 * Returns a cairo surface with the pixels to copy (selection or entire merged image)
 */
static cairo_surface_t* extract_merged_pixels_for_copy(ImageDocument* doc) {
    if (!doc) {
        return NULL;
    }

    /* Create merged surface from all visible layers */
    cairo_surface_t* merged = create_merged_surface(doc);
    if (!merged) {
        return NULL;
    }

    gint width = doc->width;
    gint height = doc->height;

    /* Check if there's a selection */
    gboolean has_selection = (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));

    if (!has_selection) {
        /* No selection: return merged surface as-is */
        return merged;
    }

    /* Has selection: extract selected pixels from merged surface */
    DirtyRect doc_rect;
    dirty_rect_set(&doc_rect, 0, 0, width, height);

    DirtyRect actual_region;
    SelectionMask* region_mask = selection_build_combined_mask(
        doc->selection_mask, &doc_rect, FEATHER_QUALITY_NORMAL, &actual_region);

    if (!region_mask || !region_mask->data || dirty_rect_is_empty(&actual_region)) {
        cairo_surface_destroy(merged);
        if (region_mask) {
            selection_mask_free(region_mask);
        }
        return NULL;
    }

    /* Find bounding box of selected pixels */
    gint sel_x_min = actual_region.x + actual_region.width;
    gint sel_y_min = actual_region.y + actual_region.height;
    gint sel_x_max = actual_region.x;
    gint sel_y_max = actual_region.y;

    /* Scan mask to find actual bounds */
    for (gint y = 0; y < region_mask->height; y++) {
        for (gint x = 0; x < region_mask->width; x++) {
            uint8_t mask_alpha = region_mask->data[y * region_mask->stride + x];
            if (mask_alpha > 0) {
                gint doc_x = actual_region.x + x;
                gint doc_y = actual_region.y + y;
                if (doc_x < sel_x_min)
                    sel_x_min = doc_x;
                if (doc_y < sel_y_min)
                    sel_y_min = doc_y;
                if (doc_x > sel_x_max)
                    sel_x_max = doc_x;
                if (doc_y > sel_y_max)
                    sel_y_max = doc_y;
            }
        }
    }

    if (sel_x_max < sel_x_min || sel_y_max < sel_y_min) {
        cairo_surface_destroy(merged);
        selection_mask_free(region_mask);
        return NULL; /* No selected pixels */
    }

    /* Create surface with bounding box dimensions */
    gint new_width = sel_x_max - sel_x_min + 1;
    gint new_height = sel_y_max - sel_y_min + 1;

    cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!copy) {
        cairo_surface_destroy(merged);
        selection_mask_free(region_mask);
        return NULL;
    }

    /* Copy pixels from merged surface to copy, masked by selection */
    cairo_surface_flush(merged);
    cairo_surface_flush(copy);

    guchar* src_data = cairo_image_surface_get_data(merged);
    gint src_stride = cairo_image_surface_get_stride(merged);
    guchar* dst_data = cairo_image_surface_get_data(copy);
    gint dst_stride = cairo_image_surface_get_stride(copy);

    for (gint y = 0; y < new_height; y++) {
        gint doc_y = sel_y_min + y;
        gint mask_y = doc_y - actual_region.y;

        if (doc_y < 0 || doc_y >= height) {
            continue;
        }
        if (mask_y < 0 || mask_y >= region_mask->height) {
            continue;
        }

        for (gint x = 0; x < new_width; x++) {
            gint doc_x = sel_x_min + x;
            gint mask_x = doc_x - actual_region.x;

            if (doc_x < 0 || doc_x >= width) {
                continue;
            }
            if (mask_x < 0 || mask_x >= region_mask->width) {
                continue;
            }

            uint8_t mask_alpha = region_mask->data[mask_y * region_mask->stride + mask_x];
            if (mask_alpha == 0) {
                continue; /* Not selected */
            }

            /* Copy pixel from merged to destination */
            guchar* src_pixel = src_data + doc_y * src_stride + doc_x * 4;
            guchar* dst_pixel = dst_data + y * dst_stride + x * 4;

            if (mask_alpha == 255) {
                /* Fully selected: copy pixel directly */
                dst_pixel[0] = src_pixel[0];
                dst_pixel[1] = src_pixel[1];
                dst_pixel[2] = src_pixel[2];
                dst_pixel[3] = src_pixel[3];
            } else {
                /* Partially selected (feathered): apply mask to alpha */
                uint8_t src_a = src_pixel[3];
                uint8_t new_alpha = (uint8_t)((src_a * mask_alpha) / 255);

                if (new_alpha == 0) {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                } else if (src_a > 0) {
                    /* Un-premultiply, then re-premultiply with new alpha */
                    uint16_t r = (src_pixel[2] * 255 + src_a / 2) / src_a;
                    uint16_t g = (src_pixel[1] * 255 + src_a / 2) / src_a;
                    uint16_t b = (src_pixel[0] * 255 + src_a / 2) / src_a;

                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;

                    dst_pixel[0] = (b * new_alpha + 127) / 255;
                    dst_pixel[1] = (g * new_alpha + 127) / 255;
                    dst_pixel[2] = (r * new_alpha + 127) / 255;
                    dst_pixel[3] = new_alpha;
                } else {
                    dst_pixel[0] = dst_pixel[1] = dst_pixel[2] = dst_pixel[3] = 0;
                }
            }
        }
    }

    cairo_surface_mark_dirty(copy);
    cairo_surface_destroy(merged);
    selection_mask_free(region_mask);

    return copy;
}

/**
 * Notebook page switch callback
 */
static void on_notebook_switch_page(GtkNotebook* notebook, GtkWidget* page,
                                    guint page_num, gpointer user_data) {
    (void)notebook; /* Unused */
    (void)page_num; /* Unused */

    AppContext* ctx = (AppContext*)user_data;
    ImageDocument* doc = NULL;
    LayersPanel* layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                                "layers_panel");

    /* Find the document that matches the page widget (scrolled window) */
    if (page) {
        for (GList* iter = ctx->documents; iter; iter = iter->next) {
            ImageDocument* d = (ImageDocument*)iter->data;
            if (d && d->scrolled_window == page) {
                doc = d;
                break;
            }
        }
    }

    /* Fallback to ui_get_active_document if page matching fails */
    if (!doc) {
        doc = ui_get_active_document(ctx);
    }

    /* Set the current document in the tool registry so tools can access it */
    if (ctx->tool_registry) {
        ctx->tool_registry->current_doc = doc;
    }

    ui_update_window_title(ctx, doc);
    ui_update_status_bar(ctx, doc);

    /* Update layers panel with current document's layers */
    if (layers_panel && doc) {
        layers_panel_update(layers_panel, doc);
    }

    /* Update menu and button sensitivity */
    ui_update_menu_and_button_states(ctx);
}

/**
 * Tab close button clicked callback
 */
static void on_tab_close_button_clicked(GtkButton* button, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(button),
                                                     "app_context");

    if (ctx && doc) {
        ui_close_document_tab(ctx, doc);
    } else {
        printf("  ERROR: ctx=%p or doc=%p is NULL\n", ctx, doc);
    }
}

/**
 * Helper function to flip a layer using Ocular library
 */
static gboolean flip_layer(ImageLayer* layer, OcDirection direction) {
    cairo_surface_t* surface;
    gint width, height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(surface, &width, &height)) {
        return FALSE;
    }

    if (width == 0 || height == 0) {
        return FALSE;
    }

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(width * height * 4);
    rgba_output = (guchar*)g_malloc(width * height * 4);

    if (!rgba_input || !rgba_output) {
        g_warning("Flip layer: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(surface, rgba_input)) {
        g_warning("Flip layer: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply flip using Ocular library (4 channels for RGBA) */
    status = ocularFlipImage(rgba_input, rgba_output, width, height, 4, direction);

    if (status != OC_STATUS_OK) {
        g_warning("Flip layer: Ocular flip returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert back from RGBA to Cairo ARGB32 */
    if (!adjustments_rgba_to_cairo(surface, rgba_output)) {
        g_warning("Flip layer: Failed to convert RGBA to surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    return TRUE;
}

/**
 * Helper function to transpose a layer using Ocular library
 */
static gboolean transpose_layer(ImageLayer* layer) {
    cairo_surface_t* old_surface;
    cairo_surface_t* new_surface;
    gint old_width, old_height;
    gint new_width, new_height;
    guchar* rgba_input;
    guchar* rgba_output;
    OC_STATUS status;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    old_surface = layer->surface;

    /* Validate surface and get dimensions */
    if (!adjustments_validate_surface(old_surface, &old_width, &old_height)) {
        return FALSE;
    }

    if (old_width == 0 || old_height == 0) {
        return FALSE;
    }

    /* Transpose swaps width and height */
    new_width = old_height;
    new_height = old_width;

    /* Allocate buffers for RGBA input and output */
    rgba_input = (guchar*)g_malloc(old_width * old_height * 4);
    rgba_output = (guchar*)g_malloc(new_width * new_height * 4);

    if (!rgba_input || !rgba_output) {
        g_warning("Transpose layer: Failed to allocate memory");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert from Cairo ARGB32 to RGBA */
    if (!adjustments_cairo_to_rgba(old_surface, rgba_input)) {
        g_warning("Transpose layer: Failed to convert surface to RGBA");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Apply transpose using Ocular library
       Stride is width * 4 for RGBA format */
    status = ocularTransposeImage(rgba_input, rgba_output, old_width, old_height, old_width * 4);

    if (status != OC_STATUS_OK) {
        g_warning("Transpose layer: Ocular transpose returned error %d", status);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Create new surface with swapped dimensions */
    new_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_width, new_height);
    if (!new_surface) {
        g_warning("Transpose layer: Failed to create new surface");
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Convert RGBA output to new Cairo surface */
    if (!adjustments_rgba_to_cairo(new_surface, rgba_output)) {
        g_warning("Transpose layer: Failed to convert RGBA to new surface");
        cairo_surface_destroy(new_surface);
        g_free(rgba_input);
        g_free(rgba_output);
        return FALSE;
    }

    /* Free temporary buffers */
    g_free(rgba_input);
    g_free(rgba_output);

    /* Replace old surface with new one */
    cairo_surface_destroy(layer->surface);
    layer->surface = new_surface;

    /* Update layer dimensions */
    layer->width = new_width;
    layer->height = new_height;

    /* Invalidate layer cache */
    layer_invalidate_cache(layer);

    return TRUE;
}

/**
 * Convert dimension from pixels to the specified unit
 * @param pixels Dimension in pixels
 * @param unit Unit string (%, px, in, cm, mm, pt, pc)
 * @param zoom_factor Current zoom factor (for % calculation)
 * @param dpi Resolution in DPI (default: 96)
 * @return Converted dimension value
 */
static gdouble convert_dimension(guint pixels, const gchar* unit, gdouble zoom_factor, gdouble dpi) {
    if (!unit) {
        return (gdouble)pixels;
    }

    if (g_strcmp0(unit, "px") == 0) {
        return (gdouble)pixels;
    } else if (g_strcmp0(unit, "%") == 0) {
        /* Percentage of image size (always 100% for actual dimensions) */
        return 100.0;
    } else if (g_strcmp0(unit, "in") == 0) {
        /* Inches */
        return (gdouble)pixels / dpi;
    } else if (g_strcmp0(unit, "cm") == 0) {
        /* Centimeters (1 inch = 2.54 cm) */
        return ((gdouble)pixels / dpi) * 2.54;
    } else if (g_strcmp0(unit, "mm") == 0) {
        /* Millimeters (1 inch = 25.4 mm) */
        return ((gdouble)pixels / dpi) * 25.4;
    } else if (g_strcmp0(unit, "pt") == 0) {
        /* Points (72 points = 1 inch) */
        return ((gdouble)pixels / dpi) * 72.0;
    } else if (g_strcmp0(unit, "pc") == 0) {
        /* Picas (6 picas = 1 inch) */
        return ((gdouble)pixels / dpi) * 6.0;
    }

    /* Default to pixels */
    return (gdouble)pixels;
}

/**
 * Format dimension value with appropriate precision
 * @param value The dimension value
 * @param unit Unit string (for determining integer vs decimal formatting)
 * @return Formatted string (must be freed)
 */
static gchar* format_dimension(gdouble value, const gchar* unit) {
    /* px and pt should always be displayed as integers (no decimals) */
    if (unit && (g_strcmp0(unit, "px") == 0 || g_strcmp0(unit, "pt") == 0)) {
        return g_strdup_printf("%d", (int)(value + 0.5)); /* Round to nearest integer */
    }

    /* Use appropriate precision based on magnitude for other units */
    if (value >= 100.0) {
        return g_strdup_printf("%.1f", value);
    } else if (value >= 10.0) {
        return g_strdup_printf("%.2f", value);
    } else {
        return g_strdup_printf("%.3f", value);
    }
}

/**
 * Callback for statusbar zoom out button
 */
static void on_statusbar_zoom_out(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_out(doc);
        ui_update_status_bar(ctx, doc);
    }
}

/**
 * Callback for statusbar zoom in button
 */
static void on_statusbar_zoom_in(GtkWidget* widget, gpointer data) {
    (void)widget; /* Unused */
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);

    if (doc) {
        document_zoom_in(doc);
        ui_update_status_bar(ctx, doc);
    }
}

/**
 * Callback for statusbar zoom combobox change
 */
static void on_statusbar_zoom_changed(GtkComboBox* combo, gpointer data) {
    AppContext* ctx = (AppContext*)data;
    ImageDocument* doc = ui_get_active_document(ctx);
    gchar* text;
    GtkTreeIter iter;

    if (!doc || !gtk_combo_box_get_active_iter(combo, &iter)) {
        return;
    }

    /* Get the text from the combo box */
    GtkTreeModel* model = gtk_combo_box_get_model(combo);
    gtk_tree_model_get(model, &iter, 0, &text, -1);

    if (!text) {
        return;
    }

    /* Skip separators */
    if (g_strcmp0(text, "Separator") == 0) {
        g_free(text);
        return;
    }

    /* Handle special zoom modes */
    if (g_strcmp0(text, "Fit image") == 0) {
        document_zoom_fit(doc);
    } else if (g_strcmp0(text, "Fit width") == 0) {
        document_zoom_fit_width(doc);
    } else if (g_strcmp0(text, "Fit height") == 0) {
        document_zoom_fit_height(doc);
    } else {
        /* Parse zoom percentage */
        int zoom_percent = atoi(text);
        if (zoom_percent > 0) {
            doc->zoom_mode = 0; /* Manual zoom */
            document_set_zoom(doc, zoom_percent / 100.0);
        }
    }

    g_free(text);
    ui_update_status_bar(ctx, doc);
}

/**
 * Callback for statusbar size unit combobox change
 */
static void on_statusbar_size_unit_changed(GtkComboBox* combo, gpointer data) {
    AppContext* ctx = (AppContext*)data;
    gchar* text;
    GtkTreeIter iter;

    if (!gtk_combo_box_get_active_iter(combo, &iter)) {
        return;
    }

    /* Get the selected unit from the combo box */
    GtkTreeModel* model = gtk_combo_box_get_model(combo);
    gtk_tree_model_get(model, &iter, 0, &text, -1);

    if (!text) {
        return;
    }

    /* Update the context's size unit */
    if (ctx->size_unit) {
        g_free(ctx->size_unit);
    }
    ctx->size_unit = g_strdup(text);
    g_free(text);

    /* Refresh status bar to show dimensions in new unit */
    ui_update_status_bar(ctx, NULL);
}

/**
 * Row separator function for zoom combobox
 */
static gboolean zoom_combo_row_separator_func(GtkTreeModel* model, GtkTreeIter* iter, gpointer data) {
    (void)data; /* Unused */
    gchar* text;
    gboolean is_separator;

    gtk_tree_model_get(model, iter, 0, &text, -1);
    is_separator = (text && g_strcmp0(text, "Separator") == 0);
    g_free(text);

    return is_separator;
}

/**
 * Update the status bar with document information
 */
void ui_update_status_bar(AppContext* ctx, ImageDocument* doc) {
    GtkWidget *size_label, *bitdepth_label, *zoom_combobox;
    GtkBuilder* builder;
    gchar *size_text, *bitdepth_text;

    if (!ctx || !ctx->status_bar) {
        return;
    }

    /* Get active document if not provided */
    if (!doc) {
        doc = ui_get_active_document(ctx);
    }

    /* Get builder from window to retrieve status bar widgets */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get status bar widgets */
    size_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_label_size"));
    bitdepth_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_label_bitdepth"));
    zoom_combobox = GTK_WIDGET(gtk_builder_get_object(builder, "sb_zoom_combobox"));

    if (!size_label || !bitdepth_label || !zoom_combobox) {
        return;
    }

    if (!doc || doc->width == 0) {
        /* No document or empty document */
        size_text = g_strdup("—");
        bitdepth_text = g_strdup("—");
        gtk_combo_box_set_active(GTK_COMBO_BOX(zoom_combobox), -1);
    } else {
        /* Size label: WIDTH x HEIGHT in selected unit */
        gdouble dpi = 96.0; /* Standard screen DPI */
        gdouble width_converted = convert_dimension(doc->width, ctx->size_unit, doc->zoom_factor, dpi);
        gdouble height_converted = convert_dimension(doc->height, ctx->size_unit, doc->zoom_factor, dpi);

        /* Format dimensions with appropriate precision */
        gchar* width_str = format_dimension(width_converted, ctx->size_unit);
        gchar* height_str = format_dimension(height_converted, ctx->size_unit);
        size_text = g_strdup_printf("%s × %s", width_str, height_str);
        g_free(width_str);
        g_free(height_str);

        /* Bit depth label: BITDEPTH-bit CHANNELS */
        gchar channels_str[32];
        if (doc->channels == 3) {
            snprintf(channels_str, sizeof(channels_str), "RGB");
        } else if (doc->channels == 4) {
            snprintf(channels_str, sizeof(channels_str), "RGBA");
        } else {
            snprintf(channels_str, sizeof(channels_str), "%u-channel", doc->channels);
        }
        bitdepth_text = g_strdup_printf("%u-bit %s", doc->bit_depth, channels_str);

        /* Update zoom combobox based on zoom mode */
        GtkTreeModel* model = gtk_combo_box_get_model(GTK_COMBO_BOX(zoom_combobox));
        GtkTreeIter iter;
        gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
        gboolean found = FALSE;
        gchar* search_text = NULL;

        /* Block the changed signal while we update */
        g_signal_handlers_block_by_func(zoom_combobox, on_statusbar_zoom_changed, ctx);

        /* Determine what to search for based on zoom mode */
        if (doc->zoom_mode == 1) {
            search_text = g_strdup("Fit image");
        } else if (doc->zoom_mode == 2) {
            search_text = g_strdup("Fit width");
        } else if (doc->zoom_mode == 3) {
            search_text = g_strdup("Fit height");
        } else {
            /* Manual zoom - search for percentage */
            int zoom_percent = (int)(doc->zoom_factor * 100.0 + 0.5);
            search_text = g_strdup_printf("%d%%", zoom_percent);
        }

        /* Search for matching item in combobox */
        while (valid && !found) {
            gchar* item_text;
            gtk_tree_model_get(model, &iter, 0, &item_text, -1);

            if (item_text && g_strcmp0(item_text, search_text) == 0) {
                gtk_combo_box_set_active_iter(GTK_COMBO_BOX(zoom_combobox), &iter);
                found = TRUE;
            }

            g_free(item_text);
            valid = gtk_tree_model_iter_next(model, &iter);
        }

        /* If no exact match found, this shouldn't happen with discrete zoom levels */
        if (!found) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(zoom_combobox), -1);
        }

        /* Unblock the signal */
        g_signal_handlers_unblock_by_func(zoom_combobox, on_statusbar_zoom_changed, ctx);

        g_free(search_text);
    }

    /* Update labels */
    gtk_label_set_text(GTK_LABEL(size_label), size_text);
    gtk_label_set_text(GTK_LABEL(bitdepth_label), bitdepth_text);

    g_free(size_text);
    g_free(bitdepth_text);
}

/**
 * Update the status bar time label with processing time
 * @param ctx The application context
 * @param time_seconds Processing time in seconds
 */
void ui_update_status_bar_time(AppContext* ctx, gdouble time_seconds) {
    GtkWidget* status_label;
    GtkBuilder* builder;
    gchar* time_text;

    if (!ctx || !ctx->status_bar) {
        return;
    }

    /* Get builder from window to retrieve status bar labels */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get time label */
    status_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_status_label"));
    if (!status_label) {
        return;
    }

    /* Format time: show seconds with appropriate precision */
    if (time_seconds < 0.001) {
        time_text = g_strdup_printf("Time taken: %.3f ms", time_seconds * 1000.0);
    } else if (time_seconds < 1.0) {
        time_text = g_strdup_printf("Time taken: %.3f s", time_seconds);
    } else {
        time_text = g_strdup_printf("Time taken: %.2f s", time_seconds);
    }

    /* Update label */
    gtk_label_set_text(GTK_LABEL(status_label), time_text);
    g_free(time_text);
}

/**
 * Update the cursor position display in the status bar
 */
void ui_update_cursor_position(AppContext* ctx, ImageDocument* doc, gint image_x, gint image_y) {
    GtkWidget* position_label;
    GtkWidget* position_box;
    GtkBuilder* builder;
    gchar* position_text;
    gdouble x_value, y_value;
    gdouble dpi = 96.0;

    if (!ctx || !ctx->status_bar || !doc) {
        return;
    }

    /* Get builder from window to retrieve status bar widgets */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get position label and box */
    position_label = GTK_WIDGET(gtk_builder_get_object(builder, "sb_label_position"));
    position_box = GTK_WIDGET(gtk_builder_get_object(builder, "sb_position_box"));
    if (!position_label || !position_box) {
        return;
    }

    /* Convert image coordinates to selected unit */
    if (g_strcmp0(ctx->size_unit, "px") == 0) {
        /* Pixels - show as integers */
        position_text = g_strdup_printf("(%d, %d)", image_x, image_y);
    } else if (g_strcmp0(ctx->size_unit, "%") == 0) {
        /* Percentage of image dimensions */
        x_value = ((gdouble)image_x / (gdouble)doc->width) * 100.0;
        y_value = ((gdouble)image_y / (gdouble)doc->height) * 100.0;
        position_text = g_strdup_printf("(%.1f, %.1f)", x_value, y_value);
    } else if (g_strcmp0(ctx->size_unit, "in") == 0) {
        /* Inches */
        x_value = (gdouble)image_x / dpi;
        y_value = (gdouble)image_y / dpi;
        position_text = g_strdup_printf("(%.3f, %.3f)", x_value, y_value);
    } else if (g_strcmp0(ctx->size_unit, "cm") == 0) {
        /* Centimeters */
        x_value = ((gdouble)image_x / dpi) * 2.54;
        y_value = ((gdouble)image_y / dpi) * 2.54;
        position_text = g_strdup_printf("(%.2f, %.2f)", x_value, y_value);
    } else if (g_strcmp0(ctx->size_unit, "mm") == 0) {
        /* Millimeters */
        x_value = ((gdouble)image_x / dpi) * 25.4;
        y_value = ((gdouble)image_y / dpi) * 25.4;
        position_text = g_strdup_printf("(%.1f, %.1f)", x_value, y_value);
    } else if (g_strcmp0(ctx->size_unit, "pt") == 0) {
        /* Points - show as integers */
        x_value = ((gdouble)image_x / dpi) * 72.0;
        y_value = ((gdouble)image_y / dpi) * 72.0;
        position_text = g_strdup_printf("(%d, %d)", (gint)x_value, (gint)y_value);
    } else if (g_strcmp0(ctx->size_unit, "pc") == 0) {
        /* Picas */
        x_value = ((gdouble)image_x / dpi) * 6.0;
        y_value = ((gdouble)image_y / dpi) * 6.0;
        position_text = g_strdup_printf("(%.2f, %.2f)", x_value, y_value);
    } else {
        /* Default to pixels */
        position_text = g_strdup_printf("(%d, %d)", image_x, image_y);
    }

    gtk_label_set_text(GTK_LABEL(position_label), position_text);

    /* Show the position box */
    gtk_widget_show(position_box);

    g_free(position_text);
}

/**
 * Hide the cursor position display in the status bar
 */
void ui_hide_cursor_position(AppContext* ctx) {
    GtkWidget* position_box;
    GtkBuilder* builder;

    if (!ctx || !ctx->status_bar) {
        return;
    }

    /* Get builder from window to retrieve status bar widgets */
    builder = GTK_BUILDER(g_object_get_data(G_OBJECT(ctx->window), "main_builder"));
    if (!builder) {
        return;
    }

    /* Get position box */
    position_box = GTK_WIDGET(gtk_builder_get_object(builder, "sb_position_box"));
    if (!position_box) {
        return;
    }

    /* Hide the position box */
    gtk_widget_hide(position_box);
}

/**
 * Update menu and button sensitivity based on document and layer state
 */
void ui_update_menu_and_button_states(AppContext* ctx) {
    ImageDocument* doc;
    LayersPanel* layers_panel;
    gboolean has_document;
    gboolean has_selection;

    if (!ctx || !ctx->window) {
        return;
    }

    /* Get current document */
    doc = ui_get_active_document(ctx);
    has_document = (doc != NULL);

    /* Get layers panel and check for selection */
    layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(ctx->window),
                                                   "layers_panel");

    has_selection = FALSE;
    ImageLayer* selected_layer = NULL;
    if (layers_panel && doc && layers_panel->tree_view) {
        selected_layer = layers_panel_get_selected_layer(layers_panel);
        has_selection = (selected_layer != NULL);
    }

    /* Update layers panel button states */
    if (layers_panel) {
        layers_panel_update_button_sensitivity(layers_panel, has_document, has_selection, doc, selected_layer);
        layers_panel_update_opacity_controls(layers_panel);
    }

    /* Update File menu item states (save) */
    /* Save is enabled only if document is dirty (if no filename, Save As will be triggered) */
    if (ctx->file_menu_save && GTK_IS_WIDGET(ctx->file_menu_save)) {
        gboolean can_save = has_document && doc && document_is_dirty(doc);
        gtk_widget_set_sensitive(ctx->file_menu_save, can_save);
    }

    /* Update Edit menu item states (undo/redo) */
    if (ctx->edit_menu_undo && GTK_IS_WIDGET(ctx->edit_menu_undo)) {
        gboolean can_undo = has_document && document_can_undo(doc);
        gtk_widget_set_sensitive(ctx->edit_menu_undo, can_undo);

        /* Update label with command name if available */
        if (can_undo && doc && doc->undo_stack) {
            Command* cmd = command_stack_peek(doc->undo_stack);
            if (cmd && cmd->name) {
                gchar* label = g_strdup_printf("_Undo: %s", cmd->name);
                gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_undo), label);
                g_free(label);
            } else {
                gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_undo), "_Undo");
            }
        } else {
            gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_undo), "_Undo");
        }
    }
    if (ctx->edit_menu_redo && GTK_IS_WIDGET(ctx->edit_menu_redo)) {
        gboolean can_redo = has_document && document_can_redo(doc);
        gtk_widget_set_sensitive(ctx->edit_menu_redo, can_redo);

        /* Update label with command name if available */
        if (can_redo && doc && doc->redo_stack) {
            Command* cmd = command_stack_peek(doc->redo_stack);
            if (cmd && cmd->name) {
                gchar* label = g_strdup_printf("_Redo: %s", cmd->name);
                gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_redo), label);
                g_free(label);
            } else {
                gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_redo), "_Redo");
            }
        } else {
            gtk_menu_item_set_label(GTK_MENU_ITEM(ctx->edit_menu_redo), "_Redo");
        }
    }

    /* Update Layer menu item states */
    if (ctx->layer_menu_new && GTK_IS_WIDGET(ctx->layer_menu_new)) {
        gtk_widget_set_sensitive(ctx->layer_menu_new, has_document);
    }
    if (ctx->layer_menu_delete && GTK_IS_WIDGET(ctx->layer_menu_delete)) {
        gtk_widget_set_sensitive(ctx->layer_menu_delete, has_document && has_selection);
    }
    if (ctx->layer_menu_duplicate && GTK_IS_WIDGET(ctx->layer_menu_duplicate)) {
        gtk_widget_set_sensitive(ctx->layer_menu_duplicate, has_document && has_selection);
    }
}

/**
 * Get canvas background color
 */
void ui_get_canvas_background_color(AppContext* ctx, gdouble* r, gdouble* g, gdouble* b) {
    if (!ctx) {
        /* Return default if context is NULL */
        if (r)
            *r = 160.0 / 255.0;
        if (g)
            *g = 160.0 / 255.0;
        if (b)
            *b = 160.0 / 255.0;
        return;
    }

    /* Get from settings if available, otherwise use defaults */
    if (ctx->settings) {
        settings_get_canvas_background(ctx->settings, r, g, b);
    } else {
        /* Return defaults if settings not loaded yet */
        if (r)
            *r = 160.0 / 255.0;
        if (g)
            *g = 160.0 / 255.0;
        if (b)
            *b = 160.0 / 255.0;
    }
}

/**
 * Set canvas background color
 */
void ui_set_canvas_background_color(AppContext* ctx, gdouble r, gdouble g, gdouble b) {
    if (!ctx) {
        return;
    }

    /* Clamp values to 0.0-1.0 */
    gdouble r_clamped = (r < 0.0) ? 0.0 : ((r > 1.0) ? 1.0 : r);
    gdouble g_clamped = (g < 0.0) ? 0.0 : ((g > 1.0) ? 1.0 : g);
    gdouble b_clamped = (b < 0.0) ? 0.0 : ((b > 1.0) ? 1.0 : b);

    /* Update settings if available */
    if (ctx->settings) {
        settings_set_canvas_background(ctx->settings, r_clamped, g_clamped, b_clamped);
        /* Save settings immediately */
        if (ctx->app_dir) {
            settings_save(ctx->settings, ctx->app_dir);
        }
    }

    /* Update all open documents */
    ui_update_canvas_background_color(ctx);
}

/**
 * Update canvas background color for all open documents
 */
void ui_update_canvas_background_color(AppContext* ctx) {
    if (!ctx) {
        return;
    }

    gdouble r_val, g_val, b_val;
    ui_get_canvas_background_color(ctx, &r_val, &g_val, &b_val);
    guint r = (guint)(r_val * 255.0);
    guint g = (guint)(g_val * 255.0);
    guint b = (guint)(b_val * 255.0);

    /* Update viewport background for all documents */
    for (GList* iter = ctx->documents; iter; iter = iter->next) {
        ImageDocument* doc = (ImageDocument*)iter->data;
        if (doc && doc->viewport) {
            /* Get or create CSS provider for this viewport */
            GtkCssProvider* provider = (GtkCssProvider*)g_object_get_data(G_OBJECT(doc->viewport), "canvas_bg_provider");
            if (!provider) {
                provider = gtk_css_provider_new();
                g_object_set_data_full(G_OBJECT(doc->viewport), "canvas_bg_provider", provider, g_object_unref);
                GtkStyleContext* style_context = gtk_widget_get_style_context(doc->viewport);
                gtk_style_context_add_provider(style_context, GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            }

            gchar* css = g_strdup_printf("#canvas-viewport { background-color: rgb(%u, %u, %u); }", r, g, b);
            gtk_css_provider_load_from_data(provider, css, -1, NULL);
            g_free(css);
            gtk_widget_queue_draw(doc->viewport);
        }
    }
}

/**
 * Save active document with file dialog
 */
void ui_save_document_as(AppContext* ctx) {
    if (!ctx) {
        return;
    }

    on_file_save_as(NULL, ctx);
}

/* Forward declaration */
static void ui_save_tool_options_to_settings_internal(AppContext* ctx, ToolType tool_type, gboolean save_immediately);

/**
 * Get tool name from tool type (for settings storage)
 */
static const char* tool_type_to_name(ToolType tool_type) {
    switch (tool_type) {
        case TOOL_BRUSH:
            return "brush";
        case TOOL_PENCIL:
            return "pencil";
        case TOOL_ERASER:
            return "eraser";
        case TOOL_PAINT_BUCKET:
            return "paintbucket";
        case TOOL_HAND:
            return "hand";
        case TOOL_ZOOM:
            return "zoom";
        case TOOL_MOVE:
            return "move";
        case TOOL_RECT_SELECT:
            return "rect_select";
        default:
            return NULL;
    }
}

/**
 * Load tool options from settings
 */
void ui_load_tool_options_from_settings(AppContext* ctx) {
    if (!ctx || !ctx->settings || !ctx->tool_registry) {
        return;
    }

    /* Load options for each tool type */
    for (int i = 0; i < TOOL_COUNT; i++) {
        ToolType tool_type = (ToolType)i;
        const char* tool_name = tool_type_to_name(tool_type);
        if (!tool_name) {
            continue;
        }

        ToolOptions* opts = tool_options_get_for_tool(tool_type);
        if (!opts) {
            continue;
        }

        /* Load size (use default if not found) */
        const char* size_str = settings_get_tool_option(ctx->settings, tool_name, "size");
        if (size_str) {
            gfloat size = (gfloat)g_ascii_strtod(size_str, NULL);
            if (size > 0.0f) {
                tool_options_set_size(opts, size);
            } else {
                tool_options_set_size(opts, settings_get_default_tool_size());
            }
        } else {
            tool_options_set_size(opts, settings_get_default_tool_size());
        }

        /* Load opacity (use default if not found) */
        const char* opacity_str = settings_get_tool_option(ctx->settings, tool_name, "opacity");
        if (opacity_str) {
            gfloat opacity = (gfloat)g_ascii_strtod(opacity_str, NULL);
            tool_options_set_opacity(opts, opacity);
        } else {
            tool_options_set_opacity(opts, settings_get_default_tool_opacity());
        }

        /* Load hardness (use default if not found) */
        const char* hardness_str = settings_get_tool_option(ctx->settings, tool_name, "hardness");
        if (hardness_str) {
            gfloat hardness = (gfloat)g_ascii_strtod(hardness_str, NULL);
            tool_options_set_hardness(opts, hardness);
        } else {
            tool_options_set_hardness(opts, settings_get_default_tool_hardness());
        }

        /* Load flow (use default if not found) */
        const char* flow_str = settings_get_tool_option(ctx->settings, tool_name, "flow");
        if (flow_str) {
            gfloat flow = (gfloat)g_ascii_strtod(flow_str, NULL);
            tool_options_set_flow(opts, flow);
        } else {
            tool_options_set_flow(opts, settings_get_default_tool_flow());
        }

        /* Load spacing (use default if not found) */
        const char* spacing_str = settings_get_tool_option(ctx->settings, tool_name, "spacing");
        if (spacing_str) {
            gfloat spacing = (gfloat)g_ascii_strtod(spacing_str, NULL);
            tool_options_set_spacing(opts, spacing);
        } else {
            tool_options_set_spacing(opts, settings_get_default_tool_spacing());
        }

        /* Load tolerance (use default if not found) */
        const char* tolerance_str = settings_get_tool_option(ctx->settings, tool_name, "tolerance");
        if (tolerance_str) {
            gfloat tolerance = (gfloat)g_ascii_strtod(tolerance_str, NULL);
            tool_options_set_tolerance(opts, tolerance);
        } else {
            tool_options_set_tolerance(opts, settings_get_default_tool_tolerance());
        }

        /* Load fill_contiguous (use default if not found) */
        const char* contiguous_str = settings_get_tool_option(ctx->settings, tool_name, "fill_contiguous");
        if (contiguous_str) {
            gboolean contiguous = (g_strcmp0(contiguous_str, "true") == 0 || g_strcmp0(contiguous_str, "1") == 0);
            tool_options_set_fill_contiguous(opts, contiguous);
        } else {
            tool_options_set_fill_contiguous(opts, settings_get_default_tool_fill_contiguous());
        }

        /* Load fill_antialiased (use default if not found) */
        const char* antialiased_str = settings_get_tool_option(ctx->settings, tool_name, "fill_antialiased");
        if (antialiased_str) {
            gboolean antialiased = (g_strcmp0(antialiased_str, "true") == 0 || g_strcmp0(antialiased_str, "1") == 0);
            tool_options_set_fill_antialiased(opts, antialiased);
        } else {
            tool_options_set_fill_antialiased(opts, settings_get_default_tool_fill_antialiased());
        }

        /* Pencil tool specific options */
        if (tool_type == TOOL_PENCIL) {
            /* Load pencil antialias (default: false) */
            const char* pencil_antialias_str = settings_get_tool_option(ctx->settings, tool_name, "pencil_antialias");
            if (pencil_antialias_str) {
                gboolean antialias = (g_strcmp0(pencil_antialias_str, "true") == 0 || g_strcmp0(pencil_antialias_str, "1") == 0);
                opts->pencil_antialias = antialias;
            } else {
                opts->pencil_antialias = FALSE;
            }

            /* Load pencil align to pixel grid (default: true) */
            const char* pencil_align_pixel_grid_str = settings_get_tool_option(ctx->settings, tool_name, "pencil_align_pixel_grid");
            if (pencil_align_pixel_grid_str) {
                gboolean align = (g_strcmp0(pencil_align_pixel_grid_str, "true") == 0 || g_strcmp0(pencil_align_pixel_grid_str, "1") == 0);
                opts->pencil_align_pixel_grid = align;
            } else {
                opts->pencil_align_pixel_grid = TRUE;
            }
        }

        /* Rectangular select tool specific options */
        if (tool_type == TOOL_RECT_SELECT) {
            /* Load combine mode (0=NEW, 1=ADD, 2=SUBTRACT, 3=INTERSECT) */
            const char* combine_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_combine");
            if (combine_str) {
                gint combine_val = (gint)g_ascii_strtoll(combine_str, NULL, 10);
                if (combine_val >= 0 && combine_val < 4) {
                    tool_options_set_rect_select_combine(opts, (SelectionCombineMode)combine_val);
                } else {
                    tool_options_set_rect_select_combine(opts, SELECTION_COMBINE_NEW);
                }
            } else {
                tool_options_set_rect_select_combine(opts, SELECTION_COMBINE_NEW);
            }

            /* Load smoothing mode (0=NONE, 1=ANTIALIASED, 2=FEATHERED) */
            const char* smooth_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_smooth");
            if (smooth_str) {
                gint smooth_val = (gint)g_ascii_strtoll(smooth_str, NULL, 10);
                if (smooth_val >= 0 && smooth_val < 3) {
                    tool_options_set_rect_select_smooth(opts, (SelectionSmoothingMode)smooth_val);
                } else {
                    tool_options_set_rect_select_smooth(opts, SELECTION_SMOOTH_ANTIALIASED);
                }
            } else {
                tool_options_set_rect_select_smooth(opts, SELECTION_SMOOTH_ANTIALIASED);
            }

            /* Load feather radius */
            const char* feather_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_feather");
            if (feather_str) {
                gfloat feather_val = (gfloat)g_ascii_strtod(feather_str, NULL);
                if (feather_val >= 0.0f && feather_val <= 200.0f) {
                    tool_options_set_rect_select_feather(opts, feather_val);
                } else {
                    tool_options_set_rect_select_feather(opts, 0.0f);
                }
            } else {
                tool_options_set_rect_select_feather(opts, 0.0f);
            }

            /* Load animation toggle */
            const char* animate_str = settings_get_tool_option(ctx->settings, tool_name, "rect_select_animate");
            if (animate_str) {
                gboolean animate = (g_strcmp0(animate_str, "true") == 0 || g_strcmp0(animate_str, "1") == 0);
                tool_options_set_rect_select_animate(opts, animate);
            } else {
                tool_options_set_rect_select_animate(opts, TRUE);
            }
        }
    }
}

/* Forward declaration */
static void ui_save_tool_options_to_settings_internal(AppContext* ctx, ToolType tool_type, gboolean save_immediately);

/**
 * Save tool options to settings (in-memory only, not to file)
 * Tool options are saved to file only on app shutdown
 */
void ui_save_tool_options_to_settings(AppContext* ctx, ToolType tool_type) {
    ui_save_tool_options_to_settings_internal(ctx, tool_type, FALSE);
}

/**
 * Internal function to save tool options to settings
 * @param save_immediately If TRUE, saves settings to file immediately. If FALSE, only updates in-memory settings.
 */
static void ui_save_tool_options_to_settings_internal(AppContext* ctx, ToolType tool_type, gboolean save_immediately) {
    if (!ctx || !ctx->settings || !ctx->tool_registry) {
        return;
    }

    const char* tool_name = tool_type_to_name(tool_type);
    if (!tool_name) {
        return;
    }

    /* Get the tool to check which options it supports */
    Tool* tool = tool_manager_get(ctx->tool_registry, tool_type);
    if (!tool) {
        return;
    }

    /* Read tool options flags immediately to avoid accessing freed memory */
    ToolOptionFlags tool_options_flags = tool->options;

    ToolOptions* opts = tool_options_get_for_tool(tool_type);
    if (!opts) {
        return;
    }

    /* Read all values from opts structure immediately to avoid accessing freed memory */
    /* Store values in local variables before any operations that might free memory */
    gfloat size_val = opts->size;
    gfloat opacity_val = opts->opacity;
    gfloat hardness_val = opts->hardness;
    gfloat flow_val = opts->flow;
    gfloat spacing_val = opts->spacing;
    gfloat tolerance_val = opts->tolerance;
    gboolean fill_contiguous_val = opts->fill_contiguous;
    gboolean fill_antialiased_val = opts->fill_antialiased;
    gboolean pencil_antialias_val = opts->pencil_antialias;
    gboolean pencil_align_pixel_grid_val = opts->pencil_align_pixel_grid;
    SelectionCombineMode rect_select_combine_val = opts->rect_select_combine;
    SelectionSmoothingMode rect_select_smooth_val = opts->rect_select_smooth;
    gfloat rect_select_feather_val = opts->rect_select_feather;
    gboolean rect_select_animate_val = opts->rect_select_animate;

    /* Save only the options that this tool supports */
    /* Check tool_options_flags to determine which options to save */

    if (tool_options_flags & TOOL_OPT_SIZE) {
        gchar size_str[32];
        gfloat safe_size = (size_val >= 1.0f && size_val <= 10000.0f) ? size_val : 5.0f;
        g_snprintf(size_str, sizeof(size_str), "%.2f", safe_size);
        settings_set_tool_option(ctx->settings, tool_name, "size", size_str);
    }

    if (tool_options_flags & TOOL_OPT_OPACITY) {
        gchar opacity_str[32];
        gfloat safe_opacity = (opacity_val >= 0.0f && opacity_val <= 1.0f) ? opacity_val : 1.0f;
        g_snprintf(opacity_str, sizeof(opacity_str), "%.3f", safe_opacity);
        settings_set_tool_option(ctx->settings, tool_name, "opacity", opacity_str);
    }

    if (tool_options_flags & TOOL_OPT_HARDNESS) {
        gchar hardness_str[32];
        gfloat safe_hardness = (hardness_val >= 0.0f && hardness_val <= 1.0f) ? hardness_val : 1.0f;
        g_snprintf(hardness_str, sizeof(hardness_str), "%.3f", safe_hardness);
        settings_set_tool_option(ctx->settings, tool_name, "hardness", hardness_str);
    }

    if (tool_options_flags & TOOL_OPT_FLOW) {
        gchar flow_str[32];
        gfloat safe_flow = (flow_val >= 0.0f && flow_val <= 1.0f) ? flow_val : 1.0f;
        g_snprintf(flow_str, sizeof(flow_str), "%.3f", safe_flow);
        settings_set_tool_option(ctx->settings, tool_name, "flow", flow_str);
    }

    if (tool_options_flags & TOOL_OPT_SPACING) {
        gchar spacing_str[32];
        gfloat safe_spacing = (spacing_val >= 0.0f && spacing_val <= 1.0f) ? spacing_val : 0.25f;
        g_snprintf(spacing_str, sizeof(spacing_str), "%.3f", safe_spacing);
        settings_set_tool_option(ctx->settings, tool_name, "spacing", spacing_str);
    }

    /* Paint bucket tool uses tolerance, fill_contiguous, and fill_antialiased */
    /* These are not in ToolOptionFlags, so check tool type directly */
    if (tool_type == TOOL_PAINT_BUCKET) {
        gchar tolerance_str[32];
        gfloat safe_tolerance = (tolerance_val >= 0.0f && tolerance_val <= 100.0f) ? tolerance_val : 15.0f;
        g_snprintf(tolerance_str, sizeof(tolerance_str), "%.2f", safe_tolerance);
        settings_set_tool_option(ctx->settings, tool_name, "tolerance", tolerance_str);
        settings_set_tool_option(ctx->settings, tool_name, "fill_contiguous", fill_contiguous_val ? "true" : "false");
        settings_set_tool_option(ctx->settings, tool_name, "fill_antialiased", fill_antialiased_val ? "true" : "false");
    }

    /* Pencil tool uses antialias and align_pixel_grid options */
    if (tool_type == TOOL_PENCIL) {
        settings_set_tool_option(ctx->settings, tool_name, "pencil_antialias", pencil_antialias_val ? "true" : "false");
        settings_set_tool_option(ctx->settings, tool_name, "pencil_align_pixel_grid", pencil_align_pixel_grid_val ? "true" : "false");
    }

    /* Rectangular select tool uses combine mode, smoothing, feather, and animate options */
    if (tool_type == TOOL_RECT_SELECT) {
        gchar combine_str[16];
        gint combine_val = (gint)rect_select_combine_val;
        g_snprintf(combine_str, sizeof(combine_str), "%d", combine_val);
        settings_set_tool_option(ctx->settings, tool_name, "rect_select_combine", combine_str);

        gchar smooth_str[16];
        gint smooth_val = (gint)rect_select_smooth_val;
        g_snprintf(smooth_str, sizeof(smooth_str), "%d", smooth_val);
        settings_set_tool_option(ctx->settings, tool_name, "rect_select_smooth", smooth_str);

        gchar feather_str[32];
        gfloat safe_feather = (rect_select_feather_val >= 0.0f && rect_select_feather_val <= 200.0f) ? rect_select_feather_val : 0.0f;
        g_snprintf(feather_str, sizeof(feather_str), "%.2f", safe_feather);
        settings_set_tool_option(ctx->settings, tool_name, "rect_select_feather", feather_str);

        settings_set_tool_option(ctx->settings, tool_name, "rect_select_animate", rect_select_animate_val ? "true" : "false");
    }

    /* Save settings immediately if requested */
    if (save_immediately && ctx->app_dir) {
        settings_save(ctx->settings, ctx->app_dir);
    }

    /* Save settings immediately if requested */
    if (save_immediately && ctx->app_dir) {
        settings_save(ctx->settings, ctx->app_dir);
    }
}

/**
 * Save all tool options to settings (saves all tools at once)
 */
void ui_save_all_tool_options_to_settings(AppContext* ctx) {
    if (!ctx || !ctx->settings || !ctx->tool_registry) {
        return;
    }

    /* Save options for each tool type (without saving to file after each one) */
    /* settings_set_tool_option will create the hash table if needed */
    /* Tool options should always be saved on app close, even if unchanged */
    for (int i = 0; i < TOOL_COUNT; i++) {
        ToolType tool_type = (ToolType)i;
        ui_save_tool_options_to_settings_internal(ctx, tool_type, FALSE);
    }
}
