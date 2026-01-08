#ifndef COMMAND_H
#define COMMAND_H

#include <cairo.h>
#include <glib.h>
#include <stdint.h>

/**
 * Forward declarations
 */
typedef struct _Command Command;
struct ImageDocument;
struct ImageLayer;

/**
 * Command type enumeration
 */
typedef enum {
    COMMAND_DRAW = 0,
    COMMAND_FILL = 1,
    COMMAND_ERASE = 2,
    COMMAND_LAYER_EDIT = 3,
    COMMAND_MOVE = 4,
    COMMAND_CANVAS_RESIZE = 5,
    COMMAND_SELECTION = 6, /* Selection mask modification */
    COMMAND_CUSTOM = 255
} CommandType;

/**
 * Command name enumeration
 * Used to centralize command name strings
 */
typedef enum {
    CMD_NAME_DRAW_BRUSH_STROKE = 0,
    CMD_NAME_ERASE,
    CMD_NAME_FILL,
    CMD_NAME_MOVE_LAYER,
    CMD_NAME_ADD_LAYER,
    CMD_NAME_DELETE_LAYER,
    CMD_NAME_DUPLICATE_LAYER,
    CMD_NAME_MOVE_LAYER_UP,
    CMD_NAME_MOVE_LAYER_DOWN,
    CMD_NAME_CANVAS_SIZE,
    CMD_NAME_FLIP_HORIZONTAL,
    CMD_NAME_FLIP_VERTICAL,
    CMD_NAME_TRANSPOSE,
    CMD_NAME_FIT_ACTIVE_LAYER,
    CMD_NAME_FIT_ALL_LAYERS,
    CMD_NAME_MERGE_VISIBLE,
    CMD_NAME_FLATTEN,
    CMD_NAME_SELECT_ALL,
    CMD_NAME_DESELECT_ALL,
    CMD_NAME_INVERT_SELECTION,
    CMD_NAME_FEATHER_SELECTION,
    CMD_NAME_MOVE_SELECTED_PIXELS,
    CMD_NAME_COUNT /* Total number of predefined command names */
} CommandName;

/**
 * Get command name string from enum
 * @param name The command name enum value
 * @return String representation of the command name, or NULL if invalid
 */
const gchar* command_get_name_string(CommandName name);

/**
 * Command callbacks
 */
typedef void (*CommandApplyFunc)(Command* cmd, struct ImageDocument* doc);
typedef void (*CommandRevertFunc)(Command* cmd, struct ImageDocument* doc);
typedef void (*CommandDestroyFunc)(Command* cmd);

/**
 * Command structure
 * Represents a single undoable/redoable action
 */
typedef struct _Command {
    gchar* name;                    /* Human-readable command name */
    CommandType type;               /* Command type */
    CommandApplyFunc apply;         /* Apply callback */
    CommandRevertFunc revert;       /* Revert callback */
    CommandDestroyFunc destroy;     /* Cleanup callback */
    gpointer user_data;             /* Command-specific data */
    struct ImageDocument* document; /* Associated document */
} Command;

/**
 * Create a new command
 * @param name Command name (e.g., "Draw Brush Stroke")
 * @param type Command type
 * @param apply Apply callback function
 * @param revert Revert callback function
 * @param destroy Cleanup callback (can be NULL)
 * @return Newly created Command, or NULL on failure
 */
Command* command_new(const gchar* name,
                     CommandType type,
                     CommandApplyFunc apply,
                     CommandRevertFunc revert,
                     CommandDestroyFunc destroy);

/**
 * Free a command and its resources
 * @param cmd The command to free
 */
void command_free(Command* cmd);

/**
 * Execute a command (apply it)
 * @param cmd The command to execute
 * @param doc The document to execute on
 */
void command_execute(Command* cmd, struct ImageDocument* doc);

/**
 * Undo a command (revert it)
 * @param cmd The command to undo
 * @param doc The document to undo on
 */
void command_undo(Command* cmd, struct ImageDocument* doc);

/**
 * Command stack structure
 * Maintains a list of commands
 */
typedef struct _CommandStack {
    GList* commands; /* List of Command pointers */
    guint max_depth; /* Maximum undo depth (0 = unlimited) */
} CommandStack;

/**
 * Create a new command stack
 * @param max_depth Maximum number of commands to keep (0 = unlimited)
 * @return Newly created CommandStack
 */
CommandStack* command_stack_new(guint max_depth);

/**
 * Push a command onto the stack
 * @param stack The command stack
 * @param cmd The command to push
 * @return TRUE on success, FALSE on failure
 */
gboolean command_stack_push(CommandStack* stack, Command* cmd);

/**
 * Pop a command from the stack (and free it)
 * @param stack The command stack
 * @return Popped command (caller must free), or NULL if empty
 */
Command* command_stack_pop(CommandStack* stack);

/**
 * Peek at the top command (doesn't remove)
 * @param stack The command stack
 * @return Top command, or NULL if empty
 */
Command* command_stack_peek(CommandStack* stack);

/**
 * Check if stack is empty
 * @param stack The command stack
 * @return TRUE if empty, FALSE otherwise
 */
gboolean command_stack_is_empty(CommandStack* stack);

/**
 * Get the size of the stack
 * @param stack The command stack
 * @return Number of commands in stack
 */
guint command_stack_size(CommandStack* stack);

/**
 * Clear all commands from the stack (freeing each)
 * @param stack The command stack
 */
void command_stack_clear(CommandStack* stack);

/**
 * Free the command stack and all commands
 * @param stack The command stack to free
 */
void command_stack_free(CommandStack* stack);

/**
 * Selection undo command data structure
 * Stores selection mask delta for undo/redo
 */
typedef struct {
    gint region_x;          /* Left coordinate of affected region */
    gint region_y;          /* Top coordinate of affected region */
    gint region_width;      /* Width of affected region in pixels */
    gint region_height;     /* Height of affected region in pixels */
    uint8_t* mask_before;   /* Mask data before operation (owned) */
    uint8_t* mask_after;    /* Mask data after operation (owned) */
    gboolean is_compressed; /* TRUE if data is LZ4-compressed */
    guint compressed_size_before;
    guint compressed_size_after;
    /* Selections list (serialized as objects, not raster data) */
    guint8* selections_before;    /* Serialized selections list before operation (owned) */
    guint selections_before_size; /* Size of serialized selections_before data */
    guint8* selections_after;     /* Serialized selections list after operation (owned) */
    guint selections_after_size;  /* Size of serialized selections_after data */
} SelectionUndoDelta;

/**
 * Selection command data structure
 * Stores selection mask before/after for undo/redo
 */
typedef struct {
    struct SelectionMask* mask; /* Selection mask being modified */
    SelectionUndoDelta* delta;  /* Delta containing before/after mask data (owned) */
    struct ImageDocument* doc;  /* Document containing the selection */
} SelectionCommandData;

/**
 * Tile undo delta structure
 * Stores before/after snapshots for a single tile-sized region of a layer
 */
typedef struct {
    gint tile_x;             /* Tile X coordinate (grid position) */
    gint tile_y;             /* Tile Y coordinate (grid position) */
    cairo_surface_t* before; /* Snapshot of tile region before modification */
    cairo_surface_t* after;  /* Snapshot of tile region after modification */
} TileUndoDelta;

/* Forward declaration */
typedef struct _UndoEntryIndex UndoEntryIndex;

/**
 * Tile undo command data structure
 * Stores tile-level deltas for delta-based undo/redo
 * This replaces full layer snapshots with region-based snapshots for memory efficiency
 *
 * For disk-backed undo: If entry_index is set, pixel data is on disk and tile_deltas
 * may be empty or contain only metadata. If entry_index is NULL, pixel data is in memory.
 */
typedef struct {
    struct ImageLayer* layer;    /* Layer being modified */
    gint tile_size;              /* Tile size used for region division */
    GPtrArray* tile_deltas;      /* Array of TileUndoDelta* pointers (may be empty if on disk) */
    UndoEntryIndex* entry_index; /* Disk journal entry index (NULL if in-memory only) */
} TileUndoCommandData;

/**
 * Draw command data structure
 * Stores layer snapshots for undo/redo (legacy, used by filters that modify entire layers)
 */
typedef struct {
    struct ImageLayer* layer;         /* Layer being drawn on */
    cairo_surface_t* before_snapshot; /* Surface snapshot before draw (for undo) */
    cairo_surface_t* after_snapshot;  /* Surface snapshot after draw (for redo) */
} DrawCommandData;

/**
 * Tile undo transaction state (internal, for tracking modifications during a stroke)
 */
typedef struct _TileUndoTransaction TileUndoTransaction;

/**
 * Begin a tile-based undo transaction
 * Tracks which tile regions are modified during a drawing operation
 * @param layer The layer being modified
 * @param doc The document (for tile_size)
 * @param name The command name
 * @return Transaction handle, or NULL on failure
 */
TileUndoTransaction* tile_undo_transaction_begin(struct ImageLayer* layer,
                                                 struct ImageDocument* doc,
                                                 const gchar* name);

/**
 * Register a tile region as modified (captures "before" state on first call)
 * @param transaction Transaction handle
 * @param doc The document (for tile_size)
 * @param layer_x X coordinate in layer space
 * @param layer_y Y coordinate in layer space
 * @return TRUE if successful, FALSE on error
 */
gboolean tile_undo_transaction_register_tile(TileUndoTransaction* transaction,
                                             struct ImageDocument* doc,
                                             gint layer_x,
                                             gint layer_y);

/**
 * Commit a tile-based undo transaction (captures "after" state and creates command)
 * @param transaction Transaction handle
 * @return Created Command, or NULL on failure. Transaction is invalid after this call.
 */
Command* tile_undo_transaction_commit(TileUndoTransaction* transaction);

/**
 * Cancel a tile-based undo transaction (frees resources without creating command)
 * @param transaction Transaction handle
 */
void tile_undo_transaction_cancel(TileUndoTransaction* transaction);

/**
 * Create a draw command (legacy, uses full layer snapshots)
 * Used for filters and operations that modify entire layers
 * @param layer The layer being drawn on
 * @param name The command name (can be NULL for default, or use command_get_name_string for predefined names)
 * @return Newly created Command for drawing, or NULL on failure
 */
Command* command_create_draw(struct ImageLayer* layer, const gchar* name);

/**
 * Finalize a draw command by taking snapshot of state after drawing
 * This must be called after drawing is complete and before pushing to undo stack
 * @param cmd The draw command to finalize
 * @return TRUE on success, FALSE on failure
 */
gboolean command_finalize_draw(Command* cmd);

/**
 * Move command data structure
 * Stores layer offset before move for undo
 */
typedef struct {
    struct ImageLayer* layer; /* Layer being moved */
    gint old_offset_x;        /* X offset before move */
    gint old_offset_y;        /* Y offset before move */
    gint new_offset_x;        /* X offset after move */
    gint new_offset_y;        /* Y offset after move */
} MoveCommandData;

/**
 * Create a move command
 * @param layer The layer being moved
 * @param old_x Previous X offset
 * @param old_y Previous Y offset
 * @param new_x New X offset
 * @param new_y New Y offset
 * @return Newly created Command for moving, or NULL on failure
 */
Command* command_create_move(struct ImageLayer* layer,
                             gint old_x, gint old_y,
                             gint new_x, gint new_y);

/**
 * Create a move selected pixels command
 * Extracts selected pixels to a new layer and moves that layer
 * @param doc The document
 * @param new_layer The extracted layer with selected pixels
 * @param original_layer The original layer pixels were extracted from
 * @param initial_x Initial X position of extracted layer
 * @param initial_y Initial Y position of extracted layer
 * @param final_x Final X position after moving
 * @param final_y Final Y position after moving
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_move_selected_pixels(struct ImageDocument* doc,
                                             struct ImageLayer* new_layer,
                                             struct ImageLayer* original_layer,
                                             gint initial_x, gint initial_y,
                                             gint final_x, gint final_y);

/**
 * Create a move selected pixels command with provided snapshot
 * @param snapshot Snapshot of original layer taken BEFORE extraction (for proper undo)
 */
Command* command_create_move_selected_pixels_with_snapshot(struct ImageDocument* doc,
                                                           struct ImageLayer* new_layer,
                                                           struct ImageLayer* original_layer,
                                                           gint initial_x, gint initial_y,
                                                           gint final_x, gint final_y,
                                                           cairo_surface_t* snapshot);

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
 * Layer operation command data structures
 */
typedef struct {
    struct ImageDocument* doc; /* Document containing the layer */
    struct ImageLayer* layer;  /* Layer being added */
} LayerAddCommandData;

typedef struct {
    struct ImageDocument* doc; /* Document containing the layer */
    struct ImageLayer* layer;  /* Layer being deleted */
    gint position;             /* Position in layer list before deletion */
    gchar* layer_name;         /* Layer name for restoration */
    guint width;               /* Layer width */
    guint height;              /* Layer height */
    cairo_surface_t* snapshot; /* Snapshot of layer content */
    gfloat opacity;            /* Layer opacity */
    gint blend_mode;           /* Layer blend mode */
} LayerDeleteCommandData;

typedef struct {
    struct ImageDocument* doc;       /* Document containing the layer */
    struct ImageLayer* source_layer; /* Source layer */
    struct ImageLayer* new_layer;    /* Duplicated layer */
} LayerDuplicateCommandData;

typedef struct {
    struct ImageDocument* doc; /* Document containing the layer */
    struct ImageLayer* layer;  /* Layer being moved */
    gint old_position;         /* Position before move */
    gint new_position;         /* Position after move */
} LayerMoveUpCommandData;

typedef struct {
    struct ImageDocument* doc; /* Document containing the layer */
    struct ImageLayer* layer;  /* Layer being moved */
    gint old_position;         /* Position before move */
    gint new_position;         /* Position after move */
} LayerMoveDownCommandData;

/**
 * Create a layer add command
 * @param doc The document
 * @param layer The layer being added
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_layer_add(struct ImageDocument* doc, struct ImageLayer* layer);

/**
 * Create a paste command (adds layer with "Paste" name in undo/redo)
 * @param doc The document
 * @param layer The layer being pasted
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_paste(struct ImageDocument* doc, struct ImageLayer* layer);

/**
 * Create a layer delete command
 * @param doc The document
 * @param layer The layer being deleted
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_layer_delete(struct ImageDocument* doc, struct ImageLayer* layer);

/**
 * Create a layer duplicate command
 * @param doc The document
 * @param source_layer The source layer
 * @param new_layer The duplicated layer
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_layer_duplicate(struct ImageDocument* doc,
                                        struct ImageLayer* source_layer,
                                        struct ImageLayer* new_layer);

/**
 * Create a layer move up command
 * @param doc The document
 * @param layer The layer being moved
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_layer_move_up(struct ImageDocument* doc, struct ImageLayer* layer);

/**
 * Create a layer move down command
 * @param doc The document
 * @param layer The layer being moved
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_layer_move_down(struct ImageDocument* doc, struct ImageLayer* layer);

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
 * Create a flatten image command
 * @param doc The document
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_flatten(struct ImageDocument* doc);

/**
 * Create a selection command
 * Used by selection undo system to wrap SelectionUndoDelta changes
 *
 * @param mask The selection mask
 * @param delta The undo delta (ownership transferred to command)
 * @param doc The document
 * @param name Command name (e.g., from command_get_name_string)
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_selection(struct SelectionMask* mask,
                                  SelectionUndoDelta* delta,
                                  struct ImageDocument* doc,
                                  const gchar* name);

#endif /* COMMAND_H */
