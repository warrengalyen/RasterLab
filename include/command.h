#ifndef COMMAND_H
#define COMMAND_H

#include <cairo.h>
#include <glib.h>

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
 * Draw command data structure
 * Stores layer snapshots for undo/redo
 */
typedef struct {
    struct ImageLayer* layer;         /* Layer being drawn on */
    cairo_surface_t* before_snapshot; /* Surface snapshot before draw (for undo) */
    cairo_surface_t* after_snapshot;  /* Surface snapshot after draw (for redo) */
} DrawCommandData;

/**
 * Create a draw command
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

#endif /* COMMAND_H */
