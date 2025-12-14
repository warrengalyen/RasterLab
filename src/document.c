#include "document.h"
#include "command.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "render/tile.h"
#include "tool_manager.h"
#include "tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
typedef struct AppContext AppContext;

/**
 * Forward declarations for mouse event handlers
 */
static gboolean on_drawing_area_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_drawing_area_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_drawing_area_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data);
static gboolean on_drawing_area_enter_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);

/**
 * Drawing area draw callback
 */
/**
 * Drawing area draw callback - TILE-BASED RENDERING
 *
 * OLD BEHAVIOR: Drew entire composite surface at once
 * NEW BEHAVIOR: Loops over tiles and draws only visible tiles
 *
 * This dramatically improves performance for large images by:
 * - Only compositing dirty tiles instead of entire surface
 * - Only drawing tiles that are visible in the viewport
 * - Caching composited tiles to avoid recompositing
 */
static gboolean on_drawing_area_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    double x1, y1, x2, y2;
    gint clip_width, clip_height;
    gint viewport_x, viewport_y, viewport_w, viewport_h;
    gint start_tile_x, start_tile_y, end_tile_x, end_tile_y;
    gint tx, ty;
    Tile* tile;
    gdouble zoom;

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so just draw empty background */
    if (!doc || !doc->drawing_area) {
        cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
        clip_width = (gint)(x2 - x1);
        clip_height = (gint)(y2 - y1);
        draw_checkered_background(cr, clip_width, clip_height);
        return FALSE;
    }

    zoom = doc->zoom_factor;

    /* Get the clip region to determine what needs to be drawn */
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    clip_width = (gint)(x2 - x1);
    clip_height = (gint)(y2 - y1);

    /* Draw the document if image is loaded */
    if (doc->layers && g_list_length(doc->layers) > 0) {
        /* Calculate viewport in document coordinates (unscaled) */
        viewport_x = (gint)(x1 / zoom);
        viewport_y = (gint)(y1 / zoom);
        viewport_w = (gint)(clip_width / zoom);
        viewport_h = (gint)(clip_height / zoom);

        /* Apply zoom transform */
        if (zoom != 1.0) {
            cairo_scale(cr, zoom, zoom);
        }

        /* Draw checkered background for visible area */
        draw_checkered_background(cr, viewport_w, viewport_h);

        /* When zoomed, render layers directly to avoid tile boundary artifacts */
        if (zoom != 1.0) {
            document_render_layers_at_zoom(doc, cr, viewport_x, viewport_y, viewport_w, viewport_h);
        } else {
            /* At 100% zoom, use tiles for performance */
            if (doc->tile_grid) {
                /* Composite dirty tiles before drawing */
                tile_grid_composite(doc, doc->tile_grid);

                /* Calculate which tiles are visible in viewport */
                tile_grid_pixel_to_tile(doc->tile_grid, viewport_x, viewport_y, &start_tile_x, &start_tile_y);
                tile_grid_pixel_to_tile(doc->tile_grid, viewport_x + viewport_w, viewport_y + viewport_h, &end_tile_x, &end_tile_y);

                /* Clamp to grid bounds */
                if (start_tile_x < 0)
                    start_tile_x = 0;
                if (start_tile_y < 0)
                    start_tile_y = 0;
                if (end_tile_x >= doc->tile_grid->tiles_x)
                    end_tile_x = doc->tile_grid->tiles_x - 1;
                if (end_tile_y >= doc->tile_grid->tiles_y)
                    end_tile_y = doc->tile_grid->tiles_y - 1;

                /* Draw each visible tile */
                for (ty = start_tile_y; ty <= end_tile_y; ty++) {
                    for (tx = start_tile_x; tx <= end_tile_x; tx++) {
                        tile = tile_grid_get_tile(doc->tile_grid, tx, ty);

                        if (tile && tile->surface) {
                            /* Draw tile at its document pixel position */
                            cairo_set_source_surface(cr, tile->surface, tile->px, tile->py);
                            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                            cairo_paint(cr);
                        }
                    }
                }
            }
        }
    } else {
        /* Draw checkered background for empty canvas */
        draw_checkered_background(cr, clip_width, clip_height);
    }

    return FALSE;
}

/**
 * Convert widget coordinates to image coordinates
 */
static void widget_to_image_coords(ImageDocument* doc, gdouble widget_x, gdouble widget_y,
                                   gint* image_x, gint* image_y) {
    gdouble scaled_x, scaled_y;

    if (!doc || !image_x || !image_y) {
        return;
    }

    /* Unscale by zoom factor */
    scaled_x = widget_x / doc->zoom_factor;
    scaled_y = widget_y / doc->zoom_factor;

    /* Round to nearest integer to prevent pixel shifting */
    *image_x = (gint)(scaled_x + 0.5);
    *image_y = (gint)(scaled_y + 0.5);
}

/**
 * Drawing area button press callback
 */
static gboolean on_drawing_area_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    gpointer ctx_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;

    (void)widget; /* Unused */

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so ignore the event */
    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Get app context from drawing area data and extract tool registry */
    ctx_data = g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (!ctx_data) {
        return FALSE;
    }

    /* Access tool registry through doc if available */
    /* This is a minimal integration - in real code, pass registry more directly */

    /* For now, use a safer approach: store tool_registry directly */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->mouse_down) {
        return FALSE;
    }

    /* Convert to image coordinates */
    widget_to_image_coords(doc, event->x, event->y, &tool_event.x, &tool_event.y);
    tool_event.button = event->button;
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_down(active_tool, doc, &tool_event);

    /* Request redraw */
    gtk_widget_queue_draw(doc->drawing_area);

    return TRUE;
}

/**
 * Drawing area button release callback
 */
static gboolean on_drawing_area_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;

    (void)widget; /* Unused */

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so ignore the event */
    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->mouse_up) {
        return FALSE;
    }

    /* Convert to image coordinates */
    widget_to_image_coords(doc, event->x, event->y, &tool_event.x, &tool_event.y);
    tool_event.button = event->button;
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_up(active_tool, doc, &tool_event);

    /* Request redraw */
    gtk_widget_queue_draw(doc->drawing_area);

    return TRUE;
}

/**
 * Drawing area motion notify callback
 */
static gboolean on_drawing_area_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;

    (void)widget; /* Unused */

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so ignore the event */
    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->mouse_move) {
        return FALSE;
    }

    /* Convert to image coordinates */
    widget_to_image_coords(doc, event->x, event->y, &tool_event.x, &tool_event.y);
    tool_event.button = 0; /* No button pressed during motion */
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_move(active_tool, doc, &tool_event);

    /* Request redraw */
    gtk_widget_queue_draw(doc->drawing_area);

    return TRUE;
}

/**
 * Drawing area enter notify callback - set cursor for active tool
 */
static gboolean on_drawing_area_enter_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    GdkWindow* window;

    (void)event; /* Unused */

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->cursor) {
        return FALSE;
    }

    /* Set cursor on drawing area window */
    window = gtk_widget_get_window(doc->drawing_area);
    if (window) {
        gdk_window_set_cursor(window, active_tool->cursor);
    }

    return FALSE;
}

/**
 * Create a new image document
 */
ImageDocument* document_new(const gchar* filename) {
    ImageDocument* doc = (ImageDocument*)g_malloc(sizeof(ImageDocument));

    doc->filename = g_strdup(filename);
    doc->file_path = NULL;
    doc->modified = FALSE;
    doc->drawing_area = NULL;
    doc->scrolled_window = NULL;

    /* Initialize image metadata */
    doc->width = 0;
    doc->height = 0;
    doc->channels = 0;
    doc->bit_depth = 0;
    doc->has_alpha = FALSE;

    /* Initialize rendering pipeline */
    doc->layers = NULL;
    doc->selected_layer = NULL;
    doc->composite_surface = NULL;
    doc->composite_dirty = TRUE;
    dirty_rect_init(&doc->dirty_region);
    doc->tile_grid = NULL; /* Will be created when image is loaded */
    doc->zoom_factor = 1.0;

    /* Initialize undo/redo stacks (max 50 undo steps) */
    doc->undo_stack = command_stack_new(50);
    doc->redo_stack = command_stack_new(50);

    return doc;
}

/**
 * Free an image document
 */
void document_free(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    if (doc->filename) {
        g_free(doc->filename);
    }

    if (doc->file_path) {
        g_free(doc->file_path);
    }

    /* Free undo/redo stacks BEFORE freeing layers
     * This ensures command destroy callbacks can safely check layer ownership
     * and free any layers they own (e.g., layers in undo state) */
    if (doc->undo_stack) {
        command_stack_free(doc->undo_stack);
        doc->undo_stack = NULL;
    }
    if (doc->redo_stack) {
        command_stack_free(doc->redo_stack);
        doc->redo_stack = NULL;
    }

    /* Free all layers */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* Free composite surface */
    if (doc->composite_surface) {
        cairo_surface_flush(doc->composite_surface);
        cairo_surface_destroy(doc->composite_surface);
        doc->composite_surface = NULL;
    }

    /* Free tile grid */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }

    g_free(doc);
}

/**
 * Create a drawing area widget for the document
 */
GtkWidget* document_create_drawing_area(ImageDocument* doc) {
    GtkWidget* scrolled_window;
    GtkWidget* viewport;
    GtkWidget* drawing_area;

    /* Create scrolled window */
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    /* Allow viewport to shrink and center content */
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window),
                                        GTK_SHADOW_NONE);

    /* Create a viewport to hold the drawing area (allows centering) */
    viewport = gtk_viewport_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled_window), viewport);
    gtk_widget_show(viewport);

    /* Create drawing area */
    drawing_area = gtk_drawing_area_new();
    /* Start with default size - will be updated when image loads */
    gtk_widget_set_size_request(drawing_area, 800, 600);

    /* Prevent drawing area from expanding to fill viewport */
    gtk_widget_set_hexpand(drawing_area, FALSE);
    gtk_widget_set_vexpand(drawing_area, FALSE);

    /* Align to top-left corner */
    gtk_widget_set_halign(drawing_area, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(drawing_area, GTK_ALIGN_CENTER);

    gtk_container_add(GTK_CONTAINER(viewport), drawing_area);
    gtk_widget_show(drawing_area);

    /* Enable mouse events on drawing area */
    gtk_widget_set_events(drawing_area,
                          gtk_widget_get_events(drawing_area) |
                              GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_ENTER_NOTIFY_MASK);

    /* Connect draw signal */
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_drawing_area_draw), doc);

    /* Connect mouse event signals */
    g_signal_connect(drawing_area, "button-press-event",
                     G_CALLBACK(on_drawing_area_button_press), doc);
    g_signal_connect(drawing_area, "button-release-event",
                     G_CALLBACK(on_drawing_area_button_release), doc);
    g_signal_connect(drawing_area, "motion-notify-event",
                     G_CALLBACK(on_drawing_area_motion_notify), doc);
    g_signal_connect(drawing_area, "enter-notify-event",
                     G_CALLBACK(on_drawing_area_enter_notify), doc);

    /* Store references in document */
    doc->drawing_area = drawing_area;
    doc->scrolled_window = scrolled_window;

    gtk_widget_show(scrolled_window);

    return scrolled_window;
}

/**
 * Set the document as modified
 */
void document_set_modified(ImageDocument* doc, gboolean modified) {
    if (!doc) {
        return;
    }

    doc->modified = modified;
}

/**
 * Get the document filename
 */
const gchar* document_get_filename(ImageDocument* doc) {
    if (!doc) {
        return NULL;
    }

    return doc->filename;
}

/**
 * Load an image from file into the document
 */
gboolean document_load_image_from_file(ImageDocument* doc, const gchar* file_path) {
    GdkPixbuf* pixbuf;
    GError* error = NULL;
    gchar* basename;

    if (!doc || !file_path) {
        return FALSE;
    }

    /* Load image file with GdkPixbuf */
    pixbuf = gdk_pixbuf_new_from_file(file_path, &error);

    if (!pixbuf) {
        g_warning("Failed to load image: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        return FALSE;
    }

    /* Store metadata first */
    doc->width = gdk_pixbuf_get_width(pixbuf);
    doc->height = gdk_pixbuf_get_height(pixbuf);
    doc->channels = gdk_pixbuf_get_n_channels(pixbuf);
    doc->bit_depth = 8;                                /* GdkPixbuf always uses 8 bits per channel */
    doc->has_alpha = gdk_pixbuf_get_has_alpha(pixbuf); /* Preserve original format info */

    /* Free old tile grid if exists */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }

    /* Create tile grid for tile-based rendering
       Tile size of 128 is a good balance between memory and performance */
    doc->tile_grid = tile_grid_create(doc->width, doc->height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid");
        g_object_unref(pixbuf);
        return FALSE;
    }

    /* Free old layers if exists */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* Create base layer from loaded image - always with alpha support
       This allows tools like the eraser to work on any image type */
    ImageLayer* base_layer = layer_new("Background", doc->width, doc->height, TRUE);

    /* Convert pixbuf to Cairo surface and copy to layer */
    cairo_surface_t* temp_surface = pixbuf_to_cairo_surface(pixbuf);
    if (!temp_surface) {
        g_object_unref(pixbuf);
        layer_free(base_layer);
        return FALSE;
    }

    /* Copy temp surface to layer surface */
    cairo_t* cr = cairo_create(base_layer->surface);
    cairo_set_source_surface(cr, temp_surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(temp_surface);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);

    /* Set the selected layer to index 0 (base layer) */
    ImageLayer* layer_0 = document_get_layer(doc, 0);
    if (layer_0) {
        document_set_selected_layer(doc, layer_0);
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);

    /* Update filename to basename */
    basename = g_path_get_basename(file_path);
    g_free(doc->filename);
    doc->filename = basename;

    /* Store full file path */
    if (doc->file_path) {
        g_free(doc->file_path);
    }
    doc->file_path = g_strdup(file_path);

    /* Update drawing area size to match image dimensions */
    if (doc->drawing_area) {
        /* Set exact size for the drawing area based on image dimensions */
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);

        /* Queue redraw to display the image */
        gtk_widget_queue_draw(doc->drawing_area);
    }

    g_object_unref(pixbuf);

    // printf("Loaded image: %s (%ux%u, %u channels, alpha=%s)\n",
    //        doc->filename, doc->width, doc->height, doc->channels,
    //        doc->has_alpha ? "yes" : "no");

    return TRUE;
}

/**
 * Get image width
 */
guint document_get_width(ImageDocument* doc) {
    if (!doc) {
        return 0;
    }

    return doc->width;
}

/**
 * Get image height
 */
guint document_get_height(ImageDocument* doc) {
    if (!doc) {
        return 0;
    }

    return doc->height;
}

/**
 * Get image metadata string
 */
gchar* document_get_image_info(ImageDocument* doc) {
    if (!doc || doc->width == 0) {
        return g_strdup("No image loaded");
    }

    return g_strdup_printf("%ux%u, %d-bit %s%s (zoom: %.0f%%)",
                           doc->width, doc->height,
                           doc->bit_depth,
                           doc->channels == 3 ? "RGB" : "RGBA",
                           doc->has_alpha ? " (with alpha)" : "",
                           doc->zoom_factor * 100.0);
}

/**
 * Set zoom factor for the document
 */
void document_set_zoom(ImageDocument* doc, gdouble zoom_factor) {
    if (!doc) {
        return;
    }

    /* Clamp zoom to reasonable range (10% - 400%) */
    if (zoom_factor < 0.1) {
        zoom_factor = 0.1;
    } else if (zoom_factor > 4.0) {
        zoom_factor = 4.0;
    }

    doc->zoom_factor = zoom_factor;

    /* Update drawing area and trigger redraw */
    if (doc->drawing_area) {
        gint scaled_width = (gint)(doc->width * zoom_factor);
        gint scaled_height = (gint)(doc->height * zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, scaled_width, scaled_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }
}

/**
 * Get current zoom factor
 */
gdouble document_get_zoom(ImageDocument* doc) {
    if (!doc) {
        return 1.0;
    }

    return doc->zoom_factor;
}

/**
 * Execute an undo command
 */
gboolean document_undo(ImageDocument* doc) {
    Command* cmd;

    if (!doc || !doc->undo_stack) {
        return FALSE;
    }

    cmd = command_stack_pop(doc->undo_stack);
    if (!cmd) {
        return FALSE;
    }

    /* Execute the undo */
    command_undo(cmd, (struct ImageDocument*)doc);

    /* Push to redo stack */
    command_stack_push(doc->redo_stack, cmd);

    /* Mark composite for redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Mark document as modified after undo */
    doc->modified = TRUE;

    return TRUE;
}

/**
 * Execute a redo command
 */
gboolean document_redo(ImageDocument* doc) {
    Command* cmd;

    if (!doc || !doc->redo_stack) {
        return FALSE;
    }

    cmd = command_stack_pop(doc->redo_stack);
    if (!cmd) {
        return FALSE;
    }

    /* Execute the redo (apply again) */
    command_execute(cmd, (struct ImageDocument*)doc);

    /* Push back to undo stack */
    command_stack_push(doc->undo_stack, cmd);

    /* Mark composite for redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Mark document as modified after redo */
    doc->modified = TRUE;

    return TRUE;
}

/**
 * Check if undo is available
 */
gboolean document_can_undo(ImageDocument* doc) {
    if (!doc || !doc->undo_stack) {
        return FALSE;
    }

    return !command_stack_is_empty(doc->undo_stack);
}

/**
 * Check if redo is available
 */
gboolean document_can_redo(ImageDocument* doc) {
    if (!doc || !doc->redo_stack) {
        return FALSE;
    }

    return !command_stack_is_empty(doc->redo_stack);
}

/**
 * Save document as PNG with alpha channel
 */
gboolean document_save_as_png(ImageDocument* doc, const gchar* filename) {
    cairo_surface_t* composite;
    GdkPixbuf* pixbuf;
    GError* error = NULL;
    gboolean result = FALSE;

    if (!doc || !filename) {
        g_warning("Invalid parameters for document_save_as_png");
        return FALSE;
    }

    /* Get a fresh composite surface for export (includes all layers) */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        g_warning("No composite surface to save");
        return FALSE;
    }

    /* Convert to pixbuf with alpha channel */
    pixbuf = cairo_surface_to_pixbuf(composite, TRUE);
    if (!pixbuf) {
        g_warning("Failed to convert surface to pixbuf");
        return FALSE;
    }

    /* Verify pixbuf has alpha channel (should always be true when keep_alpha=TRUE) */
    if (!gdk_pixbuf_get_has_alpha(pixbuf)) {
        g_warning("Pixbuf does not have alpha channel - this should not happen");
    }

    /* Save as PNG with alpha channel preserved
       gdk_pixbuf_save automatically saves alpha if pixbuf has it */
    result = gdk_pixbuf_save(pixbuf, filename, "png", &error, NULL);

    if (!result) {
        g_warning("Failed to save PNG: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
    } else {
        // printf("Saved PNG: %s\n", filename);
        /* Update document path */
        if (doc->file_path) {
            g_free(doc->file_path);
        }
        doc->file_path = g_strdup(filename);
        doc->modified = FALSE;
    }

    g_object_unref(pixbuf);

    /* Clean up the export surface */
    cairo_surface_destroy(composite);

    return result;
}

/**
 * Save document as JPEG (flattened with white background)
 */
gboolean document_save_as_jpeg(ImageDocument* doc, const gchar* filename, gint quality) {
    cairo_surface_t* composite;
    cairo_surface_t* flattened;
    GdkPixbuf* pixbuf;
    GError* error = NULL;
    gboolean result = FALSE;
    gchar quality_str[4];

    if (!doc || !filename) {
        g_warning("Invalid parameters for document_save_as_jpeg");
        return FALSE;
    }

    /* Clamp quality to valid range */
    if (quality < 0)
        quality = 0;
    if (quality > 100)
        quality = 100;

    /* Get a fresh composite surface for export (includes all layers) */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        g_warning("No composite surface to save");
        return FALSE;
    }

    /* Flatten to white background */
    flattened = compositor_flatten_to_white_background(composite, doc->width, doc->height);

    /* Clean up the export surface */
    cairo_surface_destroy(composite);
    if (!flattened) {
        g_warning("Failed to flatten image");
        return FALSE;
    }

    /* Convert to pixbuf (no alpha) */
    pixbuf = cairo_surface_to_pixbuf(flattened, FALSE);
    cairo_surface_destroy(flattened);

    if (!pixbuf) {
        g_warning("Failed to convert surface to pixbuf");
        return FALSE;
    }

    /* Save as JPEG with quality parameter */
    g_snprintf(quality_str, sizeof(quality_str), "%d", quality);
    result = gdk_pixbuf_save(pixbuf, filename, "jpeg", &error, "quality", quality_str, NULL);

    if (!result) {
        g_warning("Failed to save JPEG: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
    } else {
        // printf("Saved JPEG: %s (quality=%d)\n", filename, quality);
        /* Update document path */
        if (doc->file_path) {
            g_free(doc->file_path);
        }
        doc->file_path = g_strdup(filename);
        doc->modified = FALSE;
    }

    g_object_unref(pixbuf);

    return result;
}

/**
 * Save document with auto-detection by file extension
 */
gboolean document_save_as(ImageDocument* doc, const gchar* filename) {
    const gchar* ext;
    gboolean result;

    if (!doc || !filename) {
        return FALSE;
    }

    /* Get file extension */
    ext = strrchr(filename, '.');
    if (!ext) {
        g_warning("No file extension provided");
        return FALSE;
    }

    ext++; /* Skip the dot */

    /* Save based on extension */
    if (g_ascii_strcasecmp(ext, "png") == 0) {
        result = document_save_as_png(doc, filename);
    } else if (g_ascii_strcasecmp(ext, "jpg") == 0 || g_ascii_strcasecmp(ext, "jpeg") == 0) {
        result = document_save_as_jpeg(doc, filename, 85); /* Default quality */
    } else {
        g_warning("Unsupported file format: %s", ext);
        return FALSE;
    }

    return result;
}

/**
 * Mark document as saved
 */
void document_mark_saved(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    doc->modified = FALSE;
    // printf("Document marked as saved\n");
}

/**
 * Check if document is dirty
 */
gboolean document_is_dirty(ImageDocument* doc) {
    if (!doc) {
        return FALSE;
    }

    return doc->modified;
}

/**
 * Zoom in
 */
void document_zoom_in(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    doc->zoom_factor *= 1.25;
    if (doc->zoom_factor > 8.0) {
        doc->zoom_factor = 8.0; /* Max 800% */
    }

    document_set_zoom(doc, doc->zoom_factor);
    // printf("Zoom: %.0f%%\n", doc->zoom_factor * 100);
}

/**
 * Zoom out
 */
void document_zoom_out(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    doc->zoom_factor /= 1.25;
    if (doc->zoom_factor < 0.1) {
        doc->zoom_factor = 0.1; /* Min 10% */
    }

    document_set_zoom(doc, doc->zoom_factor);
    // printf("Zoom: %.0f%%\n", doc->zoom_factor * 100);
}

/**
 * Zoom fit (fit to window - simplified, just reset)
 */
void document_zoom_fit(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    /* Simple fit: scale to fit typical window */
    /* For now, just set a reasonable zoom for the image size */
    if (doc->width > 0 && doc->height > 0) {
        gdouble zoom = 1.0;

        /* Fit to roughly 800x600 visible area */
        if (doc->width > 800 || doc->height > 600) {
            gdouble zoom_w = 800.0 / doc->width;
            gdouble zoom_h = 600.0 / doc->height;
            zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
        }

        doc->zoom_factor = zoom;
        document_set_zoom(doc, zoom);
        // printf("Zoom fit: %.0f%%\n", zoom * 100);
    }
}

/**
 * Reset zoom to 100%
 */
void document_zoom_reset(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    doc->zoom_factor = 1.0;
    document_set_zoom(doc, 1.0);
    // printf("Zoom reset: 100%%\n");
}
