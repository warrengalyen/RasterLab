#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "render/dirty.h"
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

/* Forward declarations */
typedef struct _CommandStack CommandStack;
struct ImageDocument; /* Forward declaration for circular deps */
struct MipmapPyramid;
typedef struct MipmapPyramid MipmapPyramid;

/* Forward declaration for tile grid - full definition in render/tile.h */
/* Note: document.c includes render/tile.h to get full definition */
struct TileGrid;
typedef struct TileGrid TileGrid;

/**
 * Blend modes for layers
 */
typedef enum {
    BLEND_MODE_NORMAL = 0,
    BLEND_MODE_MULTIPLY = 1,
    BLEND_MODE_SCREEN = 2,
    BLEND_MODE_OVERLAY = 3,
} BlendMode;

/**
 * Background type for new layers
 */
typedef enum {
    LAYER_BACKGROUND_TRANSPARENT = 0, /* Transparent (default) */
    LAYER_BACKGROUND_BLACK = 1,       /* Black */
    LAYER_BACKGROUND_WHITE = 2,       /* White */
    LAYER_BACKGROUND_CUSTOM = 3       /* Custom color */
} LayerBackgroundType;

/**
 * Position for new layers in the layer stack
 */
typedef enum {
    LAYER_POSITION_ABOVE_CURRENT = 0, /* Above current layer (default) */
    LAYER_POSITION_BELOW_CURRENT = 1, /* Below current layer */
    LAYER_POSITION_TOP = 2,           /* Top of layer stack */
    LAYER_POSITION_BOTTOM = 3         /* Bottom of layer stack */
} LayerPosition;

/* Forward declaration */
struct MipmapPyramid;
typedef struct MipmapPyramid MipmapPyramid;

/**
 * Structure to represent a single layer in an image
 */
typedef struct ImageLayer {
    gchar* name;                    /* Layer name */
    cairo_surface_t* surface;       /* Layer content */
    cairo_surface_t* cache_surface; /* Cached rendered layer (with opacity/blend applied) */
    gboolean cache_dirty;           /* Does cache need regeneration? */
    gdouble opacity;                /* Layer opacity (0.0 - 1.0) */
    gboolean visible;               /* Is layer visible? */
    guint width;                    /* Layer width in pixels */
    guint height;                   /* Layer height in pixels */
    BlendMode blend_mode;           /* Layer blend mode */
    gint offset_x;                  /* Layer offset X (horizontal translation) */
    gint offset_y;                  /* Layer offset Y (vertical translation) */
    MipmapPyramid* mipmap_pyramid;  /* Mipmap pyramid for fast zooming */
} ImageLayer;

/**
 * Structure to represent an image document
 */
typedef struct ImageDocument {
    gchar* filename;            /* File path/name */
    gchar* file_path;           /* Full file path for loading/saving */
    gboolean modified;          /* Has document been modified? */
    GtkWidget* drawing_area;    /* Associated drawing area widget */
    GtkWidget* scrolled_window; /* Parent scrolled window */
    GtkWidget* viewport;        /* Viewport widget (child of scrolled window) */

    /* Image metadata */
    guint width;        /* Image width in pixels */
    guint height;       /* Image height in pixels */
    guint channels;     /* Number of color channels (3 or 4) */
    guint bit_depth;    /* Bits per channel (usually 8) */
    gboolean has_alpha; /* Whether image has alpha channel */

    /* Rendering pipeline */
    GList* layers;                      /* List of ImageLayer objects */
    ImageLayer* selected_layer;         /* Currently selected layer for tools */
    cairo_surface_t* composite_surface; /* Cached composite surface (legacy, may be NULL with tiles) */
    gboolean composite_dirty;           /* Does composite need re-rendering? */
    DirtyRect dirty_region;             /* Accumulated dirty rectangle region */
    TileGrid* tile_grid;                /* Tile-based rendering grid (replaces full-surface rendering) */

    /* Viewport and zoom */
    gdouble zoom_factor; /* Current zoom level (1.0 = 100%) */

    /* Undo/redo system */
    CommandStack* undo_stack; /* Stack of undoable commands */
    CommandStack* redo_stack; /* Stack of redoable commands */
} ImageDocument;

/**
 * Create a new image document
 * @param filename The filename for the document
 * @return Newly allocated ImageDocument
 */
ImageDocument* document_new(const gchar* filename);

/**
 * Free an image document
 * @param doc The document to free
 */
void document_free(ImageDocument* doc);

/**
 * Create a drawing area widget for the document
 * @param doc The document
 * @return The drawing area widget
 */
GtkWidget* document_create_drawing_area(ImageDocument* doc);

/**
 * Set the document as modified
 * @param doc The document
 * @param modified Whether the document has been modified
 */
void document_set_modified(ImageDocument* doc, gboolean modified);

/**
 * Get the document filename
 * @param doc The document
 * @return The filename
 */
const gchar* document_get_filename(ImageDocument* doc);

/**
 * Load an image from file into the document
 * @param doc The document
 * @param file_path The path to the image file
 * @return TRUE if successful, FALSE otherwise
 */
gboolean document_load_image_from_file(ImageDocument* doc, const gchar* file_path);

/**
 * Get image width
 * @param doc The document
 * @return Width in pixels
 */
guint document_get_width(ImageDocument* doc);

/**
 * Get image height
 * @param doc The document
 * @return Height in pixels
 */
guint document_get_height(ImageDocument* doc);

/**
 * Get image metadata string
 * @param doc The document
 * @return Allocated string with image info (must be freed)
 */
gchar* document_get_image_info(ImageDocument* doc);

/**
 * Set zoom factor for the document
 * @param doc The document
 * @param zoom_factor Zoom level (1.0 = 100%, 2.0 = 200%, 0.5 = 50%)
 */
void document_set_zoom(ImageDocument* doc, gdouble zoom_factor);

/**
 * Get current zoom factor
 * @param doc The document
 * @return Current zoom level
 */
gdouble document_get_zoom(ImageDocument* doc);

/**
 * Render all layers to composite surface
 * @param doc The document
 * @return TRUE if successful
 */
gboolean document_render_composite(ImageDocument* doc);

/**
 * Get the composite rendered surface
 * @param doc The document
 * @return Cairo surface or NULL
 */
cairo_surface_t* document_get_composite_surface(ImageDocument* doc);

/**
 * Mark composite surface as needing re-render
 * @param doc The document
 */
void document_invalidate_composite(ImageDocument* doc);

/**
 * Add a new empty layer to the document
 * @param doc The document
 * @param name The layer name
 * @param background Background type for the layer (default: LAYER_BACKGROUND_TRANSPARENT)
 * @param position Position in layer stack (default: LAYER_POSITION_ABOVE_CURRENT)
 * @param custom_color Custom color for background (only used if background is LAYER_BACKGROUND_CUSTOM)
 *                     Format: RGBA as gdouble array [r, g, b, a] where values are 0.0-1.0
 * @return The newly created layer, or NULL on error
 */
ImageLayer* document_add_layer(ImageDocument* doc, const gchar* name,
                               LayerBackgroundType background,
                               LayerPosition position,
                               const gdouble* custom_color);

/**
 * Delete a layer from the document
 * @param doc The document
 * @param layer The layer to delete
 * @return TRUE if successful, FALSE if layer not found or invalid
 */
gboolean document_delete_layer(ImageDocument* doc, ImageLayer* layer);

/**
 * Duplicate an existing layer
 * @param doc The document
 * @param layer The layer to duplicate
 * @param name The name for the new layer
 * @return The duplicated layer, or NULL on error
 */
ImageLayer* document_duplicate_layer(ImageDocument* doc, ImageLayer* layer, const gchar* name);

/**
 * Move a layer up in the stack (higher z-order)
 * @param doc The document
 * @param layer The layer to move
 * @return TRUE if successful
 */
gboolean document_layer_move_up(ImageDocument* doc, ImageLayer* layer);

/**
 * Move a layer down in the stack (lower z-order)
 * @param doc The document
 * @param layer The layer to move
 * @return TRUE if successful
 */
gboolean document_layer_move_down(ImageDocument* doc, ImageLayer* layer);

/**
 * Check if a layer can be moved up in the stack
 * @param doc The document
 * @param layer The layer to check
 * @return TRUE if layer can move up, FALSE otherwise
 */
gboolean document_layer_can_move_up(ImageDocument* doc, ImageLayer* layer);

/**
 * Check if a layer can be moved down in the stack
 * @param doc The document
 * @param layer The layer to check
 * @return TRUE if layer can move down, FALSE otherwise
 */
gboolean document_layer_can_move_down(ImageDocument* doc, ImageLayer* layer);

/**
 * Get the layer at a specific index
 * @param doc The document
 * @param index The layer index (0 = bottom)
 * @return The layer, or NULL if index is invalid
 */
ImageLayer* document_get_layer(ImageDocument* doc, guint index);

/**
 * Get the top (active) layer
 * @param doc The document
 * @return The top layer, or NULL if no layers exist
 */
ImageLayer* document_get_active_layer(ImageDocument* doc);

/**
 * Set the selected layer (for tool operations)
 * @param doc The document
 * @param layer The layer to select, or NULL to select top layer
 */
void document_set_selected_layer(ImageDocument* doc, ImageLayer* layer);

/**
 * Get the selected layer (for tool operations)
 * @param doc The document
 * @return The selected layer, or top layer if none selected
 */
ImageLayer* document_get_selected_layer(ImageDocument* doc);

/**
 * Get the number of layers in the document
 * @param doc The document
 * @return The number of layers
 */
guint document_get_layer_count(ImageDocument* doc);

/**
 * Execute an undo command
 * @param doc The document
 * @return TRUE if undo was performed, FALSE if undo stack is empty
 */
gboolean document_undo(ImageDocument* doc);

/**
 * Execute a redo command
 * @param doc The document
 * @return TRUE if redo was performed, FALSE if redo stack is empty
 */
gboolean document_redo(ImageDocument* doc);

/**
 * Check if undo is available
 * @param doc The document
 * @return TRUE if undo stack is not empty
 */
gboolean document_can_undo(ImageDocument* doc);

/**
 * Check if redo is available
 * @param doc The document
 * @return TRUE if redo stack is not empty
 */
gboolean document_can_redo(ImageDocument* doc);

/**
 * Save document as PNG with alpha channel
 * @param doc The document
 * @param filename Path to save file
 * @return TRUE on success, FALSE on failure
 */
gboolean document_save_as_png(ImageDocument* doc, const gchar* filename);

/**
 * Save document as JPEG (flattened with white background)
 * @param doc The document
 * @param filename Path to save file
 * @param quality JPEG quality (0-100, default 85)
 * @return TRUE on success, FALSE on failure
 */
gboolean document_save_as_jpeg(ImageDocument* doc, const gchar* filename, gint quality);

/**
 * Save document with auto-detection by file extension
 * @param doc The document
 * @param filename Path to save file
 * @return TRUE on success, FALSE on failure
 */
gboolean document_save_as(ImageDocument* doc, const gchar* filename);

/**
 * Mark document as saved (clear dirty flag)
 * @param doc The document
 */
void document_mark_saved(ImageDocument* doc);

/**
 * Check if document has unsaved changes
 * @param doc The document
 * @return TRUE if document has unsaved changes
 */
gboolean document_is_dirty(ImageDocument* doc);

/**
 * Zoom in (multiply zoom by 1.25x)
 * @param doc The document
 */
void document_zoom_in(ImageDocument* doc);

/**
 * Zoom out (divide zoom by 1.25x)
 * @param doc The document
 */
void document_zoom_out(ImageDocument* doc);

/**
 * Fit image to window
 * @param doc The document
 */
void document_zoom_fit(ImageDocument* doc);

/**
 * Reset zoom to 100%
 * @param doc The document
 */
void document_zoom_reset(ImageDocument* doc);

#endif /* DOCUMENT_H */
