#include "document.h"
#include "command.h"
#include "tools.h"
#include "tool_manager.h"
#include "ui/layers_panel.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations */
typedef struct AppContext AppContext;

/**
 * Convert GdkPixbuf to Cairo image surface
 */
static cairo_surface_t* pixbuf_to_cairo_surface(GdkPixbuf *pixbuf)
{
    cairo_surface_t *surface;
    cairo_t *cr;
    gint width, height;
    gint rowstride;
    guchar *pixels;
    guint n_channels;
    gint y;

    width = gdk_pixbuf_get_width(pixbuf);
    height = gdk_pixbuf_get_height(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    n_channels = gdk_pixbuf_get_n_channels(pixbuf);

    /* Create RGB or ARGB surface depending on alpha channel */
    cairo_format_t format = gdk_pixbuf_get_has_alpha(pixbuf) 
                            ? CAIRO_FORMAT_ARGB32 
                            : CAIRO_FORMAT_RGB24;
    surface = cairo_image_surface_create(format, width, height);

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Failed to create Cairo surface");
        return NULL;
    }

    /* Copy pixel data row by row */
    guchar *surface_data = cairo_image_surface_get_data(surface);
    gint surface_stride = cairo_image_surface_get_stride(surface);

    for (y = 0; y < height; y++) {
        guchar *src_row = pixels + y * rowstride;
        guchar *dst_row = surface_data + y * surface_stride;

        if (n_channels == 3) {
            /* RGB to BGRX (Cairo RGB24) */
            for (int x = 0; x < width; x++) {
                dst_row[4 * x + 0] = src_row[3 * x + 2];  /* B */
                dst_row[4 * x + 1] = src_row[3 * x + 1];  /* G */
                dst_row[4 * x + 2] = src_row[3 * x + 0];  /* R */
                dst_row[4 * x + 3] = 0xFF;                /* X (opaque) */
            }
        } else if (n_channels == 4) {
            /* RGBA to BGRA (Cairo ARGB32) */
            for (int x = 0; x < width; x++) {
                dst_row[4 * x + 0] = src_row[4 * x + 2];  /* B */
                dst_row[4 * x + 1] = src_row[4 * x + 1];  /* G */
                dst_row[4 * x + 2] = src_row[4 * x + 0];  /* R */
                dst_row[4 * x + 3] = src_row[4 * x + 3];  /* A */
            }
        }
    }

    cairo_surface_mark_dirty(surface);

    return surface;
}

/**
 * Draw a checkered background pattern for transparency
 */
static void draw_checkered_background(cairo_t *cr, gint image_width, gint image_height)
{
    const gint square_size = 10;    /* Size of each check square */
    const double color1 = 0.85;    /* Light gray */
    const double color2 = 0.95;    /* Lighter gray */

    /* Draw checkerboard pattern aligned to image origin */
    for (gint y = 0; y < image_height; y += square_size) {
        for (gint x = 0; x < image_width; x += square_size) {
            /* Calculate which cell we're in (relative to origin) */
            gint cell_x = x / square_size;
            gint cell_y = y / square_size;
            
            /* Alternate colors in a checkerboard pattern */
            double color = ((cell_x + cell_y) % 2 == 0) ? color1 : color2;

            /* Draw this square */
            cairo_set_source_rgb(cr, color, color, color);
            cairo_rectangle(cr, x, y, square_size, square_size);
            cairo_fill(cr);
        }
    }
}

/**
 * Forward declarations for mouse event handlers
 */
static gboolean on_drawing_area_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static gboolean on_drawing_area_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static gboolean on_drawing_area_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data);

/**
 * Drawing area draw callback
 */
static gboolean on_drawing_area_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    ImageDocument *doc = (ImageDocument *)user_data;
    cairo_surface_t *composite;
    double x1, y1, x2, y2;
    gint clip_width, clip_height;

    /* Get the clip region to determine what needs to be drawn */
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    clip_width = (gint)(x2 - x1);
    clip_height = (gint)(y2 - y1);

    /* Draw the document if image is loaded */
    if (doc->layers && g_list_length(doc->layers) > 0) {
        /* Get the composite rendered surface */
        composite = document_get_composite_surface(doc);

        if (composite) {
            /* Apply zoom first if needed */
            if (doc->zoom_factor != 1.0) {
                cairo_scale(cr, doc->zoom_factor, doc->zoom_factor);
            }

            /* Always draw checkered background underneath to show canvas boundaries
               This helps visualize the canvas bounds and transparency areas */
            draw_checkered_background(cr, clip_width / doc->zoom_factor, 
                                     clip_height / doc->zoom_factor);

            /* Draw composite surface with proper alpha blending */
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
            cairo_set_source_surface(cr, composite, 0, 0);
            cairo_paint(cr);
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
static void widget_to_image_coords(ImageDocument *doc, gdouble widget_x, gdouble widget_y,
                                   gint *image_x, gint *image_y)
{
    gdouble scaled_x, scaled_y;

    if (!doc || !image_x || !image_y) {
        return;
    }

    /* Unscale by zoom factor */
    scaled_x = widget_x / doc->zoom_factor;
    scaled_y = widget_y / doc->zoom_factor;

    *image_x = (gint)scaled_x;
    *image_y = (gint)scaled_y;
}

/**
 * Drawing area button press callback
 */
static gboolean on_drawing_area_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    ImageDocument *doc = (ImageDocument *)user_data;
    gpointer ctx_data;
    ToolRegistry *tool_registry = NULL;
    Tool *active_tool = NULL;
    MouseEvent tool_event;

    (void)widget;  /* Unused */

    if (!doc) {
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
    tool_registry = (ToolRegistry *)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
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
static gboolean on_drawing_area_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    ImageDocument *doc = (ImageDocument *)user_data;
    ToolRegistry *tool_registry = NULL;
    Tool *active_tool = NULL;
    MouseEvent tool_event;

    (void)widget;  /* Unused */

    if (!doc) {
        return FALSE;
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry *)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
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
static gboolean on_drawing_area_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
    ImageDocument *doc = (ImageDocument *)user_data;
    ToolRegistry *tool_registry = NULL;
    Tool *active_tool = NULL;
    MouseEvent tool_event;

    (void)widget;  /* Unused */

    if (!doc) {
        return FALSE;
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry *)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
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
    tool_event.button = 0;  /* No button pressed during motion */
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_move(active_tool, doc, &tool_event);

    /* Request redraw */
    gtk_widget_queue_draw(doc->drawing_area);

    return TRUE;
}

/**
 * Create a new image layer
 */
static ImageLayer* layer_new(const gchar *name, guint width, guint height, gboolean has_alpha)
{
    ImageLayer *layer = (ImageLayer *)g_malloc(sizeof(ImageLayer));

    layer->name = g_strdup(name);
    
    /* Create layer surface */
    cairo_format_t format = has_alpha ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
    layer->surface = cairo_image_surface_create(format, width, height);
    
    /* Clear with transparent black if has alpha, white otherwise */
    cairo_t *cr = cairo_create(layer->surface);
    if (has_alpha) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    } else {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    }
    cairo_paint(cr);
    cairo_destroy(cr);

    layer->opacity = 1.0;
    layer->visible = TRUE;
    layer->width = width;
    layer->height = height;
    layer->blend_mode = BLEND_MODE_NORMAL;
    layer->offset_x = 0;  /* Initialize layer offset */
    layer->offset_y = 0;

    return layer;
}

/**
 * Free an image layer
 */
static void layer_free(ImageLayer *layer)
{
    if (!layer) {
        return;
    }

    if (layer->name) {
        g_free(layer->name);
    }

    if (layer->surface) {
        cairo_surface_destroy(layer->surface);
    }

    g_free(layer);
}

/**
 * Create a new image document
 */
ImageDocument* document_new(const gchar *filename)
{
    ImageDocument *doc = (ImageDocument *)g_malloc(sizeof(ImageDocument));

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
    doc->zoom_factor = 1.0;

    /* Initialize undo/redo stacks (max 50 undo steps) */
    doc->undo_stack = command_stack_new(50);
    doc->redo_stack = command_stack_new(50);

    return doc;
}

/**
 * Free an image document
 */
void document_free(ImageDocument *doc)
{
    if (!doc) {
        return;
    }

    if (doc->filename) {
        g_free(doc->filename);
    }

    if (doc->file_path) {
        g_free(doc->file_path);
    }

    /* Free all layers */
    for (GList *iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer *)iter->data);
    }
    g_list_free(doc->layers);

    /* Free composite surface */
    if (doc->composite_surface) {
        cairo_surface_destroy(doc->composite_surface);
    }

    /* Free undo/redo stacks */
    if (doc->undo_stack) {
        command_stack_free(doc->undo_stack);
    }
    if (doc->redo_stack) {
        command_stack_free(doc->redo_stack);
    }

    g_free(doc);
}

/**
 * Create a drawing area widget for the document
 */
GtkWidget* document_create_drawing_area(ImageDocument *doc)
{
    GtkWidget *scrolled_window;
    GtkWidget *viewport;
    GtkWidget *drawing_area;

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
                         GDK_POINTER_MOTION_MASK);

    /* Connect draw signal */
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_drawing_area_draw), doc);

    /* Connect mouse event signals */
    g_signal_connect(drawing_area, "button-press-event", 
                    G_CALLBACK(on_drawing_area_button_press), doc);
    g_signal_connect(drawing_area, "button-release-event", 
                    G_CALLBACK(on_drawing_area_button_release), doc);
    g_signal_connect(drawing_area, "motion-notify-event", 
                    G_CALLBACK(on_drawing_area_motion_notify), doc);

    /* Store references in document */
    doc->drawing_area = drawing_area;
    doc->scrolled_window = scrolled_window;

    gtk_widget_show(scrolled_window);

    return scrolled_window;
}

/**
 * Set the document as modified
 */
void document_set_modified(ImageDocument *doc, gboolean modified)
{
    if (!doc) {
        return;
    }

    doc->modified = modified;
}

/**
 * Get the document filename
 */
const gchar* document_get_filename(ImageDocument *doc)
{
    if (!doc) {
        return NULL;
    }

    return doc->filename;
}

/**
 * Load an image from file into the document
 */
gboolean document_load_image_from_file(ImageDocument *doc, const gchar *file_path)
{
    GdkPixbuf *pixbuf;
    GError *error = NULL;
    gchar *basename;

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
    doc->bit_depth = 8;  /* GdkPixbuf always uses 8 bits per channel */
    doc->has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);  /* Preserve original format info */

    /* Free old layers if exists */
    for (GList *iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer *)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* Create base layer from loaded image - always with alpha support
       This allows tools like the eraser to work on any image type */
    ImageLayer *base_layer = layer_new("Background", doc->width, doc->height, TRUE);
    
    /* Convert pixbuf to Cairo surface and copy to layer */
    cairo_surface_t *temp_surface = pixbuf_to_cairo_surface(pixbuf);
    if (!temp_surface) {
        g_object_unref(pixbuf);
        layer_free(base_layer);
        return FALSE;
    }

    /* Copy temp surface to layer surface */
    cairo_t *cr = cairo_create(base_layer->surface);
    cairo_set_source_surface(cr, temp_surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(temp_surface);

    /* Add layer to document */
    doc->layers = g_list_append(doc->layers, base_layer);
    
    /* Set the selected layer to index 0 (base layer) */
    ImageLayer *layer_0 = document_get_layer(doc, 0);
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

    printf("Loaded image: %s (%ux%u, %u channels, alpha=%s)\n",
           doc->filename, doc->width, doc->height, doc->channels,
           doc->has_alpha ? "yes" : "no");

    return TRUE;
}

/**
 * Get image width
 */
guint document_get_width(ImageDocument *doc)
{
    if (!doc) {
        return 0;
    }

    return doc->width;
}

/**
 * Get image height
 */
guint document_get_height(ImageDocument *doc)
{
    if (!doc) {
        return 0;
    }

    return doc->height;
}

/**
 * Get image metadata string
 */
gchar* document_get_image_info(ImageDocument *doc)
{
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
void document_set_zoom(ImageDocument *doc, gdouble zoom_factor)
{
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
gdouble document_get_zoom(ImageDocument *doc)
{
    if (!doc) {
        return 1.0;
    }

    return doc->zoom_factor;
}

/**
 * Map BlendMode enum to Cairo operator
 */
static cairo_operator_t blend_mode_to_cairo_operator(BlendMode blend_mode)
{
    switch (blend_mode) {
        case BLEND_MODE_NORMAL:
            return CAIRO_OPERATOR_OVER;
        case BLEND_MODE_MULTIPLY:
            return CAIRO_OPERATOR_MULTIPLY;
        case BLEND_MODE_SCREEN:
            return CAIRO_OPERATOR_SCREEN;
        case BLEND_MODE_OVERLAY:
            return CAIRO_OPERATOR_OVERLAY;
        default:
            return CAIRO_OPERATOR_OVER;
    }
}

/**
 * Render all layers to composite surface
 */
gboolean document_render_composite(ImageDocument *doc)
{
    cairo_t *cr;
    GList *iter;
    ImageLayer *layer;

    if (!doc || doc->width == 0 || doc->height == 0) {
        return FALSE;
    }

    /* Create composite surface if needed */
    if (!doc->composite_surface) {
        doc->composite_surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, doc->width, doc->height);
    }

    if (cairo_surface_status(doc->composite_surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Failed to create composite surface");
        return FALSE;
    }

    /* Clear composite surface to transparent */
    cr = cairo_create(doc->composite_surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    
    /* Set operator for proper alpha blending */
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Track if this is the first visible layer */
    gboolean is_first_visible_layer = TRUE;

    /* Composite each visible layer */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer *)iter->data;

        if (!layer->visible || layer->opacity <= 0.0) {
            continue;
        }

        /* Draw layer with offset and opacity */
        cairo_save(cr);
        cairo_translate(cr, layer->offset_x, layer->offset_y);
        cairo_set_source_surface(cr, layer->surface, 0, 0);
        
        /* Set operator based on layer's blend mode
           First visible layer always uses OVER to establish the base */
        cairo_operator_t op;
        if (is_first_visible_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_visible_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);
        
        if (layer->opacity < 1.0) {
            cairo_paint_with_alpha(cr, layer->opacity);
        } else {
            cairo_paint(cr);
        }
        cairo_restore(cr);
    }

    cairo_destroy(cr);
    doc->composite_dirty = FALSE;

    return TRUE;
}

/**
 * Get the composite rendered surface
 */
cairo_surface_t* document_get_composite_surface(ImageDocument *doc)
{
    if (!doc) {
        return NULL;
    }

    /* Re-render if composite is dirty */
    if (doc->composite_dirty) {
        document_render_composite(doc);
    }

    return doc->composite_surface;
}

/**
 * Mark composite surface as needing re-render
 */
void document_invalidate_composite(ImageDocument *doc)
{
    LayersPanel *layers_panel;

    if (!doc) {
        return;
    }

    doc->composite_dirty = TRUE;

    /* Trigger redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
        
        /* Update selected layer thumbnail if layers panel is available */
        layers_panel = (LayersPanel *)g_object_get_data(G_OBJECT(doc->drawing_area), "layers_panel");
        if (layers_panel) {
            /* Only update if this document is the current one in the layers panel */
            if (layers_panel->current_doc == doc) {
                layers_panel_update_selected_thumbnail(layers_panel);
                
                /* Update overview widget */
                if (layers_panel->overview_widget) {
                    gtk_widget_queue_draw(layers_panel->overview_widget);
                }
            }
        }
    }
}

/**
 * Add a new empty layer to the document
 */
ImageLayer* document_add_layer(ImageDocument *doc, const gchar *name)
{
    ImageLayer *layer;

    if (!doc || doc->width == 0 || doc->height == 0) {
        return NULL;
    }

    /* Create new layer with document dimensions */
    layer = layer_new(name, doc->width, doc->height, TRUE);

    if (!layer) {
        return NULL;
    }

    /* Add to top of layer stack */
    doc->layers = g_list_append(doc->layers, layer);

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);

    printf("Added layer: %s\n", name);

    return layer;
}

/**
 * Delete a layer from the document
 */
gboolean document_delete_layer(ImageDocument *doc, ImageLayer *layer)
{
    GList *iter;

    if (!doc || !layer) {
        return FALSE;
    }

    /* Find and remove layer */
    iter = g_list_find(doc->layers, layer);

    if (!iter) {
        return FALSE;
    }

    /* Don't delete the last layer */
    if (g_list_length(doc->layers) == 1) {
        g_warning("Cannot delete the last layer");
        return FALSE;
    }

    doc->layers = g_list_remove(doc->layers, layer);
    layer_free(layer);

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);

    printf("Deleted layer\n");

    return TRUE;
}

/**
 * Duplicate an existing layer
 */
ImageLayer* document_duplicate_layer(ImageDocument *doc, ImageLayer *layer, const gchar *name)
{
    ImageLayer *new_layer;
    cairo_t *cr;

    if (!doc || !layer) {
        return NULL;
    }

    /* Create new layer */
    new_layer = layer_new(name, layer->width, layer->height, TRUE);

    if (!new_layer) {
        return NULL;
    }

    /* Copy content from source layer */
    cr = cairo_create(new_layer->surface);
    cairo_set_source_surface(cr, layer->surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Copy properties */
    new_layer->opacity = layer->opacity;
    new_layer->blend_mode = layer->blend_mode;

    /* Add to layer stack (after source layer) */
    GList *iter = g_list_find(doc->layers, layer);
    if (iter && iter->next) {
        doc->layers = g_list_insert_before(doc->layers, iter->next, new_layer);
    } else {
        doc->layers = g_list_append(doc->layers, new_layer);
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);

    printf("Duplicated layer: %s\n", name);

    return new_layer;
}

/**
 * Move a layer up in the stack
 */
gboolean document_layer_move_up(ImageDocument *doc, ImageLayer *layer)
{
    GList *iter;
    gint pos;

    if (!doc || !layer) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->next) {
        return FALSE;  /* Already at top or not found */
    }

    pos = g_list_position(doc->layers, iter);
    doc->layers = g_list_remove(doc->layers, layer);
    doc->layers = g_list_insert(doc->layers, layer, pos + 1);

    document_invalidate_composite(doc);

    return TRUE;
}

/**
 * Move a layer down in the stack
 */
gboolean document_layer_move_down(ImageDocument *doc, ImageLayer *layer)
{
    GList *iter;
    gint pos;

    if (!doc || !layer) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->prev) {
        return FALSE;  /* Already at bottom or not found */
    }

    pos = g_list_position(doc->layers, iter);
    doc->layers = g_list_remove(doc->layers, layer);
    doc->layers = g_list_insert(doc->layers, layer, pos - 1);

    document_invalidate_composite(doc);

    return TRUE;
}

/**
 * Check if a layer can be moved up in the stack
 */
gboolean document_layer_can_move_up(ImageDocument *doc, ImageLayer *layer)
{
    GList *iter;

    if (!doc || !layer || !doc->layers) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->next) {
        return FALSE;  /* Already at top or not found */
    }

    return TRUE;
}

/**
 * Check if a layer can be moved down in the stack
 */
gboolean document_layer_can_move_down(ImageDocument *doc, ImageLayer *layer)
{
    GList *iter;

    if (!doc || !layer || !doc->layers) {
        return FALSE;
    }

    iter = g_list_find(doc->layers, layer);

    if (!iter || !iter->prev) {
        return FALSE;  /* Already at bottom or not found */
    }

    return TRUE;
}

/**
 * Get the layer at a specific index
 */
ImageLayer* document_get_layer(ImageDocument *doc, guint index)
{
    if (!doc) {
        return NULL;
    }

    return (ImageLayer *)g_list_nth_data(doc->layers, index);
}

/**
 * Get the number of layers in the document
 */
guint document_get_layer_count(ImageDocument *doc)
{
    if (!doc) {
        return 0;
    }

    return g_list_length(doc->layers);
}

/**
 * Get the top (active) layer
 */
ImageLayer* document_get_active_layer(ImageDocument *doc)
{
    if (!doc || !doc->layers) {
        return NULL;
    }

    /* Return the top layer (last in list) */
    return (ImageLayer *)g_list_nth_data(doc->layers, g_list_length(doc->layers) - 1);
}

/**
 * Set the selected layer (for tool operations)
 */
void document_set_selected_layer(ImageDocument *doc, ImageLayer *layer)
{
    if (!doc) {
        return;
    }

    doc->selected_layer = layer;
}

/**
 * Get the selected layer (for tool operations)
 */
ImageLayer* document_get_selected_layer(ImageDocument *doc)
{
    if (!doc) {
        return NULL;
    }

    /* Return selected layer if set, otherwise return active (top) layer */
    if (doc->selected_layer) {
        return doc->selected_layer;
    }

    return document_get_active_layer(doc);
}

/**
 * Execute an undo command
 */
gboolean document_undo(ImageDocument *doc)
{
    Command *cmd;

    if (!doc || !doc->undo_stack) {
        return FALSE;
    }

    cmd = command_stack_pop(doc->undo_stack);
    if (!cmd) {
        return FALSE;
    }

    /* Execute the undo */
    command_undo(cmd, (struct ImageDocument *)doc);

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
gboolean document_redo(ImageDocument *doc)
{
    Command *cmd;

    if (!doc || !doc->redo_stack) {
        return FALSE;
    }

    cmd = command_stack_pop(doc->redo_stack);
    if (!cmd) {
        return FALSE;
    }

    /* Execute the redo (apply again) */
    command_execute(cmd, (struct ImageDocument *)doc);

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
gboolean document_can_undo(ImageDocument *doc)
{
    if (!doc || !doc->undo_stack) {
        return FALSE;
    }

    return !command_stack_is_empty(doc->undo_stack);
}

/**
 * Check if redo is available
 */
gboolean document_can_redo(ImageDocument *doc)
{
    if (!doc || !doc->redo_stack) {
        return FALSE;
    }

    return !command_stack_is_empty(doc->redo_stack);
}

/**
 * Convert Cairo image surface to GdkPixbuf
 */
static GdkPixbuf* cairo_surface_to_pixbuf(cairo_surface_t *surface, gboolean keep_alpha)
{
    GdkPixbuf *pixbuf;
    gint width, height;
    guchar *pixels;
    guchar *surface_data;
    gint rowstride;
    gint x, y;

    if (!surface) {
        return NULL;
    }

    width = cairo_image_surface_get_width(surface);
    height = cairo_image_surface_get_height(surface);

    /* Create pixbuf with or without alpha channel */
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, keep_alpha ? TRUE : FALSE, 8, width, height);
    if (!pixbuf) {
        g_warning("Failed to create pixbuf");
        return NULL;
    }

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    surface_data = cairo_image_surface_get_data(surface);
    gint surface_stride = cairo_image_surface_get_stride(surface);

    /* Copy pixel data from Cairo surface to pixbuf */
    for (y = 0; y < height; y++) {
        guint32 *src = (guint32 *)(surface_data + y * surface_stride);
        guchar *dst = pixels + y * rowstride;

        for (x = 0; x < width; x++) {
            guint32 pixel = src[x];
            guchar a = (pixel >> 24) & 0xFF;
            guchar r = (pixel >> 16) & 0xFF;
            guchar g = (pixel >> 8) & 0xFF;
            guchar b = pixel & 0xFF;

            /* Cairo uses pre-multiplied alpha, we need to un-premultiply */
            if (a > 0) {
                r = (r * 255) / a;
                g = (g * 255) / a;
                b = (b * 255) / a;
            }

            dst[0] = r;
            dst[1] = g;
            dst[2] = b;

            if (keep_alpha) {
                dst[3] = a;
                dst += 4;
            } else {
                dst += 3;
            }
        }
    }

    return pixbuf;
}

/**
 * Flatten image to white background (for JPEG)
 */
static cairo_surface_t* flatten_to_white_background(cairo_surface_t *composite, guint width, guint height)
{
    cairo_surface_t *flattened;
    cairo_t *cr;

    /* Create RGB surface (no alpha) */
    flattened = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    if (!flattened) {
        return NULL;
    }

    cr = cairo_create(flattened);

    /* Paint white background */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    /* Composite the image on top */
    cairo_set_source_surface(cr, composite, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint(cr);

    cairo_destroy(cr);

    return flattened;
}

/**
 * Save document as PNG with alpha channel
 */
gboolean document_save_as_png(ImageDocument *doc, const gchar *filename)
{
    cairo_surface_t *composite;
    GdkPixbuf *pixbuf;
    GError *error = NULL;
    gboolean result = FALSE;

    if (!doc || !filename) {
        g_warning("Invalid parameters for document_save_as_png");
        return FALSE;
    }

    /* Get the composite surface */
    composite = document_get_composite_surface(doc);
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

    /* Save as PNG */
    result = gdk_pixbuf_save(pixbuf, filename, "png", &error, NULL);

    if (!result) {
        g_warning("Failed to save PNG: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
    } else {
        printf("Saved PNG: %s\n", filename);
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
 * Save document as JPEG (flattened with white background)
 */
gboolean document_save_as_jpeg(ImageDocument *doc, const gchar *filename, gint quality)
{
    cairo_surface_t *composite;
    cairo_surface_t *flattened;
    GdkPixbuf *pixbuf;
    GError *error = NULL;
    gboolean result = FALSE;
    gchar quality_str[4];

    if (!doc || !filename) {
        g_warning("Invalid parameters for document_save_as_jpeg");
        return FALSE;
    }

    /* Clamp quality to valid range */
    if (quality < 0) quality = 0;
    if (quality > 100) quality = 100;

    /* Get the composite surface */
    composite = document_get_composite_surface(doc);
    if (!composite) {
        g_warning("No composite surface to save");
        return FALSE;
    }

    /* Flatten to white background */
    flattened = flatten_to_white_background(composite, doc->width, doc->height);
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
        printf("Saved JPEG: %s (quality=%d)\n", filename, quality);
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
gboolean document_save_as(ImageDocument *doc, const gchar *filename)
{
    const gchar *ext;
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

    ext++;  /* Skip the dot */

    /* Save based on extension */
    if (g_ascii_strcasecmp(ext, "png") == 0) {
        result = document_save_as_png(doc, filename);
    } else if (g_ascii_strcasecmp(ext, "jpg") == 0 || g_ascii_strcasecmp(ext, "jpeg") == 0) {
        result = document_save_as_jpeg(doc, filename, 85);  /* Default quality */
    } else {
        g_warning("Unsupported file format: %s", ext);
        return FALSE;
    }

    return result;
}

/**
 * Mark document as saved
 */
void document_mark_saved(ImageDocument *doc)
{
    if (!doc) {
        return;
    }

    doc->modified = FALSE;
    printf("Document marked as saved\n");
}

/**
 * Check if document is dirty
 */
gboolean document_is_dirty(ImageDocument *doc)
{
    if (!doc) {
        return FALSE;
    }

    return doc->modified;
}

/**
 * Zoom in
 */
void document_zoom_in(ImageDocument *doc)
{
    if (!doc) {
        return;
    }

    doc->zoom_factor *= 1.25;
    if (doc->zoom_factor > 8.0) {
        doc->zoom_factor = 8.0;  /* Max 800% */
    }

    document_set_zoom(doc, doc->zoom_factor);
    printf("Zoom: %.0f%%\n", doc->zoom_factor * 100);
}

/**
 * Zoom out
 */
void document_zoom_out(ImageDocument *doc)
{
    if (!doc) {
        return;
    }

    doc->zoom_factor /= 1.25;
    if (doc->zoom_factor < 0.1) {
        doc->zoom_factor = 0.1;  /* Min 10% */
    }

    document_set_zoom(doc, doc->zoom_factor);
    printf("Zoom: %.0f%%\n", doc->zoom_factor * 100);
}

/**
 * Zoom fit (fit to window - simplified, just reset)
 */
void document_zoom_fit(ImageDocument *doc)
{
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
        printf("Zoom fit: %.0f%%\n", zoom * 100);
    }
}

/**
 * Reset zoom to 100%
 */
void document_zoom_reset(ImageDocument *doc)
{
    if (!doc) {
        return;
    }

    doc->zoom_factor = 1.0;
    document_set_zoom(doc, 1.0);
    printf("Zoom reset: 100%%\n");
}

