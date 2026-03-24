#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "image_format_plugin.h"
#include "render/dirty.h"
#include "ui/ruler_units.h"
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

/* Forward declarations for thread pools */
struct TileThreadPool;
typedef struct TileThreadPool TileThreadPool;

struct TileWorkerPool;
typedef struct TileWorkerPool TileWorkerPool;

/* Forward declaration for GPU compositor */
struct GPUCompositor;
typedef struct GPUCompositor GPUCompositor;

/* Forward declaration for old Selection (deprecated) */
struct Selection;
typedef struct Selection Selection;

/* Forward declaration for new mask-based Selection */
struct SelectionMask;
typedef struct SelectionMask SelectionMask;

/* Forward declaration for undo journal - full definition in undo/undo_disk.h */
typedef struct _UndoJournal UndoJournal;

/**
 * Blend modes for layers (Photoshop-compatible, all 27 modes)
 * 
 * Organized by category:
 * - Normal modes: Normal, Dissolve
 * - Darken modes: Darken, Multiply, Color Burn, Linear Burn, Darker Color
 * - Lighten modes: Lighten, Screen, Color Dodge, Linear Dodge (Add), Lighter Color
 * - Contrast modes: Overlay, Soft Light, Hard Light, Vivid Light, Linear Light, Pin Light, Hard Mix
 * - Inversion modes: Difference, Exclusion
 * - Cancellation modes: Subtract, Divide
 * - Component modes: Hue, Saturation, Color, Luminosity
 */
typedef enum {
    /* Normal modes */
    BLEND_MODE_NORMAL = 0,
    BLEND_MODE_DISSOLVE = 1,
    
    /* Darken modes */
    BLEND_MODE_DARKEN = 2,
    BLEND_MODE_MULTIPLY = 3,
    BLEND_MODE_COLOR_BURN = 4,
    BLEND_MODE_LINEAR_BURN = 5,
    BLEND_MODE_DARKER_COLOR = 6,
    
    /* Lighten modes */
    BLEND_MODE_LIGHTEN = 7,
    BLEND_MODE_SCREEN = 8,
    BLEND_MODE_COLOR_DODGE = 9,
    BLEND_MODE_LINEAR_DODGE = 10,  /* Also known as "Add" */
    BLEND_MODE_LIGHTER_COLOR = 11,
    
    /* Contrast modes */
    BLEND_MODE_OVERLAY = 12,
    BLEND_MODE_SOFT_LIGHT = 13,
    BLEND_MODE_HARD_LIGHT = 14,
    BLEND_MODE_VIVID_LIGHT = 15,
    BLEND_MODE_LINEAR_LIGHT = 16,
    BLEND_MODE_PIN_LIGHT = 17,
    BLEND_MODE_HARD_MIX = 18,
    
    /* Inversion modes */
    BLEND_MODE_DIFFERENCE = 19,
    BLEND_MODE_EXCLUSION = 20,
    
    /* Cancellation modes */
    BLEND_MODE_SUBTRACT = 21,
    BLEND_MODE_DIVIDE = 22,
    
    /* Component (HSL) modes */
    BLEND_MODE_HUE = 23,
    BLEND_MODE_SATURATION = 24,
    BLEND_MODE_COLOR = 25,
    BLEND_MODE_LUMINOSITY = 26,
    
    BLEND_MODE_COUNT = 27  /* Total number of blend modes */
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

/**
 * Anchor position for canvas resize (like Photoshop)
 * Determines where existing content is pinned when resizing
 */
typedef enum {
    CANVAS_ANCHOR_NONE = -1,         /* No anchor (default - no offset change) */
    CANVAS_ANCHOR_TOP_LEFT = 0,      /* Top-left corner */
    CANVAS_ANCHOR_TOP_CENTER = 1,    /* Top-center */
    CANVAS_ANCHOR_TOP_RIGHT = 2,     /* Top-right corner */
    CANVAS_ANCHOR_MIDDLE_LEFT = 3,   /* Middle-left */
    CANVAS_ANCHOR_CENTER = 4,        /* Center */
    CANVAS_ANCHOR_MIDDLE_RIGHT = 5,  /* Middle-right */
    CANVAS_ANCHOR_BOTTOM_LEFT = 6,   /* Bottom-left corner */
    CANVAS_ANCHOR_BOTTOM_CENTER = 7, /* Bottom-center */
    CANVAS_ANCHOR_BOTTOM_RIGHT = 8   /* Bottom-right corner */
} CanvasAnchorPosition;

/* Forward declaration */
struct MipmapPyramid;
typedef struct MipmapPyramid MipmapPyramid;

/**
 * Layer type — raster pixel layers or vector text layers
 */
typedef enum {
    LAYER_TYPE_RASTER = 0, /* Standard pixel/raster layer (default) */
    LAYER_TYPE_TEXT   = 1  /* Vector text layer rendered via Pango+Cairo */
} LayerType;

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
    guint64 content_version;        /* Increments when surface content changes (for GPU cache) */
    LayerType layer_type;           /* Raster or text (vector) layer */
    void* text_data;                /* TextLayer* when layer_type == LAYER_TYPE_TEXT, else NULL */
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
    GtkWidget* canvas_container; /* Grid wrapping rulers + scrolled window (notebook page when set) */

    /* Image metadata */
    guint width;        /* Image width in pixels */
    guint height;       /* Image height in pixels */
    guint channels;     /* Number of color channels (3 or 4) */
    guint bit_depth;    /* Bits per channel (usually 8) */
    gboolean has_alpha; /* Whether image has alpha channel */

    /* Load-time ICC profile (opaque cmsHPROFILE). Set by format plugin, applied and freed by host. */
    void* load_icc_profile;

    /* Original ICC profile blob from loaded file (malloc'd). Retained for "preserve
     * original profile" on save. NULL if the file had no non-sRGB RGB profile. */
    void* original_icc_data;
    size_t original_icc_size;

    /* Cached display CMS transform (opaque DisplayTransformCache*). Avoids rebuilding
     * the lcms transform on every draw call. Freed in document_free(). */
    void* display_xform_cache;

    /* Rendering pipeline */
    GList* layers;                      /* List of ImageLayer objects */
    ImageLayer* selected_layer;         /* Currently selected layer for tools */
    cairo_surface_t* composite_surface; /* Cached composite surface (legacy, may be NULL with tiles) */
    gboolean composite_dirty;           /* Does composite need re-rendering? */
    DirtyRect dirty_region;             /* Accumulated dirty rectangle region (legacy, for compatibility) */
    DirtyRegionList* dirty_region_list; /* Coalesced dirty region list for optimized tile invalidation */
    TileGrid* tile_grid;                /* Tile-based rendering grid (replaces full-surface rendering) */
    TileThreadPool* tile_thread_pool;   /* Thread pool for asynchronous tile compositing (deprecated) */
    TileWorkerPool* tile_worker_pool;   /* Cairo-safe worker pool for pixel buffer compositing */
    GPUCompositor* gpu_compositor;      /* GPU-accelerated compositor (NULL if GPU disabled/unavailable) */

    /* Viewport and zoom */
    gdouble zoom_factor; /* Current zoom level (1.0 = 100%) */
    gint zoom_mode;      /* 0=manual, 1=fit_image, 2=fit_width, 3=fit_height */

    /* Rulers (unit controlled by statusbar) */
    RulerUnit ruler_unit;   /* Current ruler display unit */
    gdouble ruler_dpi;      /* DPI for physical units (0 = use RULER_DPI_DEFAULT) */
    GtkWidget* ruler_h;     /* Horizontal ruler (for queue_draw on unit change) */
    GtkWidget* ruler_v;     /* Vertical ruler */
    gdouble mouse_canvas_x;   /* Last mouse position in canvas coords (invalid if < -1e8) */
    gdouble mouse_canvas_y;
    gdouble prev_mouse_canvas_x; /* Previous position for minimal ruler invalidation */
    gdouble prev_mouse_canvas_y;

    /* Selection - mask-based */
    SelectionMask* selection_mask;      /* Pixel-based selection mask */
    gint selection_animation_phase;     /* Animation phase for marching ants (0-3) */
    guint selection_animation_timer_id; /* Timer ID for selection animation (0 if not active) */

    /* Undo/redo system */
    CommandStack* undo_stack;  /* Stack of undoable commands (legacy, for non-pixel ops) */
    CommandStack* redo_stack;  /* Stack of redoable commands (legacy, for non-pixel ops) */
    UndoJournal* undo_journal; /* Disk-backed undo journal (for pixel operations) */
} ImageDocument;

/**
 * Create a new image document
 * @param filename The filename for the document
 * @param create_worker_pool If TRUE, create tile worker pool for on-screen rendering
 * @param undo_levels Maximum number of undo levels (0 = unlimited)
 * @return Newly allocated ImageDocument
 */
ImageDocument* document_new(const gchar* filename, gboolean create_worker_pool, guint undo_levels);

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
 * Set ruler display unit (e.g. from statusbar)
 */
void document_set_ruler_unit(ImageDocument* doc, RulerUnit unit);

/**
 * Get current ruler unit
 */
RulerUnit document_get_ruler_unit(ImageDocument* doc);

/**
 * Get DPI used for ruler physical units (returns RULER_DPI_DEFAULT if not set)
 */
gdouble document_get_ruler_dpi(ImageDocument* doc);

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
 * Initialize document rendering structures after image dimensions are set
 * This should be called after loading an image to set up tile grid and rendering
 * @param doc The document to initialize
 * @return TRUE on success, FALSE on failure
 */
gboolean document_init_rendering_structures(ImageDocument* doc);

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
 * Resize the canvas
 * @param doc The document
 * @param new_width New canvas width in pixels
 * @param new_height New canvas height in pixels
 * @param resolution Resolution in PPI (pixels per inch) - for future use
 * @param anchor Anchor position for resizing (determines where content is pinned)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean document_resize_canvas(ImageDocument* doc, guint new_width, guint new_height,
                                gdouble resolution, CanvasAnchorPosition anchor);

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
 * @param opts Save options (can be NULL to use defaults)
 * @return TRUE on success, FALSE on failure
 */
gboolean document_save_as(ImageDocument* doc, const gchar* filename, const SaveOptions* opts);
gboolean document_save_as_with_error(ImageDocument* doc, const gchar* filename, const SaveOptions* opts, PluginError* error_out);

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
 * Fit image to viewport width
 * @param doc The document
 */
void document_zoom_fit_width(ImageDocument* doc);

/**
 * Fit image to viewport height
 * @param doc The document
 */
void document_zoom_fit_height(ImageDocument* doc);

/**
 * Reset zoom to 100%
 * @param doc The document
 */
void document_zoom_reset(ImageDocument* doc);

/**
 * Set zoom to a specific percentage
 * @param doc The document to set zoom for
 * @param zoom_percent Zoom level as percentage (e.g., 100.0 for 100%, 200.0 for 200%)
 */
void document_zoom_to(ImageDocument* doc, gdouble zoom_percent);

/**
 * @return TRUE if document_zoom_in would change the zoom level
 */
gboolean document_zoom_can_zoom_in(ImageDocument* doc);

/**
 * @return TRUE if document_zoom_out would change the zoom level
 */
gboolean document_zoom_can_zoom_out(ImageDocument* doc);

#endif /* DOCUMENT_H */
