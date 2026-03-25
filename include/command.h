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
    COMMAND_DOCUMENT_REVERT = 7, /* Full document reload from disk (revert) */
    COMMAND_CUSTOM = 255
} CommandType;

/**
 * Command name enumeration
 * Used to centralize command name strings
 */
typedef enum {
    CMD_NAME_DRAW_BRUSH_STROKE = 0,
    CMD_NAME_DRAW_PENCIL_STROKE,
    CMD_NAME_ERASE,
    CMD_NAME_FILL,
    CMD_NAME_MOVE_LAYER,
    CMD_NAME_ADD_LAYER,
    CMD_NAME_DELETE_LAYER,
    CMD_NAME_DUPLICATE_LAYER,
    CMD_NAME_MOVE_LAYER_UP,
    CMD_NAME_MOVE_LAYER_DOWN,
    CMD_NAME_MOVE_LAYER_TO_TOP,
    CMD_NAME_MOVE_LAYER_TO_BOTTOM,
    CMD_NAME_MERGE_LAYER_UP,
    CMD_NAME_MERGE_LAYER_DOWN,
    CMD_NAME_CANVAS_SIZE,
    CMD_NAME_IMAGE_RESIZE,
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
    CMD_NAME_CROP_TO_SELECTION,
    CMD_NAME_CROP_TOOL,
    CMD_NAME_TRIM_BORDERS,
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
 * Helper function to get layer position in document
 */
gint get_layer_position(struct ImageDocument* doc, struct ImageLayer* layer);

/**
 * Create a snapshot of a Cairo surface
 */
cairo_surface_t* cairo_surface_snapshot(cairo_surface_t* source);

/**
 * Helper function to composite layers into a target surface
 */
void composite_layers_to_surface(cairo_surface_t* target, struct ImageDocument* doc, gboolean only_visible);

/**
 * Helper function to composite layers into a target surface without clearing it first
 * Used for flatten operation where we want to composite into an existing layer
 */
void composite_layers_onto_surface(cairo_surface_t* target, struct ImageDocument* doc, struct ImageLayer* skip_layer);

/**
 * Composite a single source layer onto a target layer's surface.
 * Used for merge down/up operations.
 */
void composite_layer_onto_layer(struct ImageLayer* target, struct ImageLayer* source);

#endif /* COMMAND_H */
