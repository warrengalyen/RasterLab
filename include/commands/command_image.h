#ifndef COMMAND_IMAGE_H
#define COMMAND_IMAGE_H

#include "command.h"

/**
 * Canvas resize command data structure
 * Stores canvas dimensions and layer offset adjustments
 */
typedef struct {
    guint old_width;        /* Canvas width before resize */
    guint old_height;       /* Canvas height before resize */
    guint new_width;        /* Canvas width after resize */
    guint new_height;       /* Canvas height after resize */
    gdouble old_resolution; /* Resolution before resize */
    gdouble new_resolution; /* Resolution after resize */
    gint offset_x;          /* X offset adjustment applied to all layers */
    gint offset_y;          /* Y offset adjustment applied to all layers */
    GList* layer_offsets;   /* List of (old_offset_x, old_offset_y) pairs for each layer */
} CanvasResizeCommandData;

/**
 * Create a canvas resize command
 * @param old_width Canvas width before resize
 * @param old_height Canvas height before resize
 * @param new_width Canvas width after resize
 * @param new_height Canvas height after resize
 * @param old_resolution Resolution before resize
 * @param new_resolution Resolution after resize
 * @param offset_x X offset adjustment applied to layers
 * @param offset_y Y offset adjustment applied to layers
 * @param doc Document containing layers (to capture current offsets)
 * @return Newly created Command for canvas resize, or NULL on failure
 */
Command* command_create_canvas_resize(guint old_width, guint old_height,
                                      guint new_width, guint new_height,
                                      gdouble old_resolution, gdouble new_resolution,
                                      gint offset_x, gint offset_y,
                                      struct ImageDocument* doc);

/**
 * Flip/Transpose command data structures
 */
typedef struct {
    struct ImageDocument* doc; /* Document containing the layers */
    GList* layer_snapshots;    /* List of cairo_surface_t* snapshots (before operation) */
    GList* layers;             /* List of ImageLayer* pointers */
} FlipCommandData;

typedef struct {
    struct ImageDocument* doc; /* Document containing the layers */
    GList* layer_snapshots;    /* List of cairo_surface_t* snapshots (before operation) */
    GList* layers;             /* List of ImageLayer* pointers */
    guint old_width;           /* Document width before transpose */
    guint old_height;          /* Document height before transpose */
    guint new_width;           /* Document width after transpose */
    guint new_height;          /* Document height after transpose */
} TransposeCommandData;

/**
 * Create a flip horizontal command
 * @param doc The document
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_flip_horizontal(struct ImageDocument* doc);

/**
 * Create a flip vertical command
 * @param doc The document
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_flip_vertical(struct ImageDocument* doc);

/**
 * Create a transpose command
 * @param doc The document
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_transpose(struct ImageDocument* doc);

/**
 * Create a rotate arbitrary command (rotates all layers).
 * @param doc The document
 * @param angle_degrees Rotation angle in degrees
 * @param preserve_size If TRUE, keep original size (crop). If FALSE, enlarge to fit.
 * @param use_transparency If TRUE, fill outside with transparent; otherwise fillColor is used.
 * @param interpolation_mode OcInterpolationMode value (stored as int to avoid header coupling)
 * @param fill_r Fill color R (0-255)
 * @param fill_g Fill color G (0-255)
 * @param fill_b Fill color B (0-255)
 */
Command* command_create_rotate_arbitrary(struct ImageDocument* doc,
                                         gfloat angle_degrees,
                                         gboolean preserve_size,
                                         gboolean use_transparency,
                                         gint interpolation_mode,
                                         guchar fill_r,
                                         guchar fill_g,
                                         guchar fill_b);

/**
 * Same as command_create_rotate_arbitrary but allows specifying the command name
 * shown in Undo/Redo history.
 */
Command* command_create_rotate_arbitrary_named(const gchar* name,
                                               struct ImageDocument* doc,
                                               gfloat angle_degrees,
                                               gboolean preserve_size,
                                               gboolean use_transparency,
                                               gint interpolation_mode,
                                               guchar fill_r,
                                               guchar fill_g,
                                               guchar fill_b);

/**
 * Create a fit canvas to active layer command
 * @param old_width Canvas width before resize
 * @param old_height Canvas height before resize
 * @param new_width Canvas width after resize
 * @param new_height Canvas height after resize
 * @param old_resolution Resolution before resize
 * @param new_resolution Resolution after resize
 * @param offset_x X offset adjustment applied to layers
 * @param offset_y Y offset adjustment applied to layers
 * @param doc Document containing layers (to capture current offsets)
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_fit_active_layer(guint old_width, guint old_height,
                                         guint new_width, guint new_height,
                                         gdouble old_resolution, gdouble new_resolution,
                                         gint offset_x, gint offset_y,
                                         struct ImageDocument* doc);

/**
 * Create a fit canvas to all layers command
 * @param old_width Canvas width before resize
 * @param old_height Canvas height before resize
 * @param new_width Canvas width after resize
 * @param new_height Canvas height after resize
 * @param old_resolution Resolution before resize
 * @param new_resolution Resolution after resize
 * @param offset_x X offset adjustment applied to layers
 * @param offset_y Y offset adjustment applied to layers
 * @param doc Document containing layers (to capture current offsets)
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_fit_all_layers(guint old_width, guint old_height,
                                       guint new_width, guint new_height,
                                       gdouble old_resolution, gdouble new_resolution,
                                       gint offset_x, gint offset_y,
                                       struct ImageDocument* doc);

/**
 * Merge/Flatten command data structures
 */
typedef struct {
    struct ImageLayer* layer;  /* Layer that was deleted */
    gint position;             /* Position in layer list before deletion */
    gchar* layer_name;         /* Layer name for restoration */
    guint width;               /* Layer width */
    guint height;              /* Layer height */
    cairo_surface_t* snapshot; /* Snapshot of layer content */
    gfloat opacity;            /* Layer opacity */
    gint blend_mode;           /* Layer blend mode */
    gint offset_x;             /* Layer offset X */
    gint offset_y;             /* Layer offset Y */
    gboolean visible;          /* Layer visibility state */
} MergedLayerInfo;

typedef struct {
    struct ImageDocument* doc;       /* Document containing the layers */
    struct ImageLayer* merged_layer; /* New merged layer (for merge visible) or bottom layer (for flatten) */
    GList* layer_infos;              /* List of MergedLayerInfo* for deleted layers */
    gint merged_layer_position;      /* Position where merged layer was inserted (for merge visible) */
} MergeCommandData;

/**
 * Create a merge visible layers command
 * @param doc The document
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_merge_visible(struct ImageDocument* doc);

/**
 * Structure to store old layer offsets for canvas resize undo
 */
typedef struct {
    struct ImageLayer* layer;
    gint old_offset_x;
    gint old_offset_y;
} LayerOffsetPair;

/**
 * Create a flatten image command
 * @param doc The document
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_flatten(struct ImageDocument* doc);

/**
 * Crop command data structure
 * Stores layer snapshots and positions for crop operations (crop to selection, trim borders)
 */
typedef struct {
    struct ImageDocument* doc; /* Document containing the layers */
    GList* layer_snapshots;    /* List of cairo_surface_t* snapshots (before operation) */
    GList* layers;             /* List of ImageLayer* pointers */
    GList* layer_offsets;      /* List of LayerOffsetPair* for old offsets */
    guint old_width;           /* Document width before crop */
    guint old_height;          /* Document height before crop */
    guint new_width;           /* Document width after crop */
    guint new_height;          /* Document height after crop */
    gint crop_x;               /* X offset of crop region (document coords) */
    gint crop_y;               /* Y offset of crop region (document coords) */
} CropCommandData;

/**
 * Create a crop to selection command
 * Crops all layers and canvas to the bounding box of the current selection
 * @param doc The document
 * @return Newly created Command, or NULL on failure (e.g., no selection)
 */
Command* command_create_crop_to_selection(struct ImageDocument* doc);

/**
 * Create a crop to rect command
 * Crops all layers and canvas to the given rectangle
 * @param doc The document
 * @param x Left edge of crop region (document coords)
 * @param y Top edge of crop region (document coords)
 * @param w Width of crop region
 * @param h Height of crop region
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_crop_to_rect(struct ImageDocument* doc,
                                     gint x, gint y, guint w, guint h);

/**
 * Create a trim borders command
 * Crops all layers and canvas to remove transparent borders
 * @param doc The document
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_trim_borders(struct ImageDocument* doc);

#endif /* COMMAND_IMAGE_H */