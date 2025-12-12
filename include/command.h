#ifndef COMMAND_H
#define COMMAND_H

#include <glib.h>
#include <cairo.h>

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
    COMMAND_CUSTOM = 255
} CommandType;

/**
 * Command callbacks
 */
typedef void (*CommandApplyFunc)(Command *cmd, struct ImageDocument *doc);
typedef void (*CommandRevertFunc)(Command *cmd, struct ImageDocument *doc);
typedef void (*CommandDestroyFunc)(Command *cmd);

/**
 * Command structure
 * Represents a single undoable/redoable action
 */
typedef struct _Command {
    gchar *name;                        /* Human-readable command name */
    CommandType type;                   /* Command type */
    CommandApplyFunc apply;             /* Apply callback */
    CommandRevertFunc revert;           /* Revert callback */
    CommandDestroyFunc destroy;         /* Cleanup callback */
    gpointer user_data;                 /* Command-specific data */
    struct ImageDocument *document;     /* Associated document */
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
Command* command_new(const gchar *name, 
                     CommandType type,
                     CommandApplyFunc apply,
                     CommandRevertFunc revert,
                     CommandDestroyFunc destroy);

/**
 * Free a command and its resources
 * @param cmd The command to free
 */
void command_free(Command *cmd);

/**
 * Execute a command (apply it)
 * @param cmd The command to execute
 * @param doc The document to execute on
 */
void command_execute(Command *cmd, struct ImageDocument *doc);

/**
 * Undo a command (revert it)
 * @param cmd The command to undo
 * @param doc The document to undo on
 */
void command_undo(Command *cmd, struct ImageDocument *doc);

/**
 * Command stack structure
 * Maintains a list of commands
 */
typedef struct _CommandStack {
    GList *commands;                    /* List of Command pointers */
    guint max_depth;                    /* Maximum undo depth (0 = unlimited) */
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
gboolean command_stack_push(CommandStack *stack, Command *cmd);

/**
 * Pop a command from the stack (and free it)
 * @param stack The command stack
 * @return Popped command (caller must free), or NULL if empty
 */
Command* command_stack_pop(CommandStack *stack);

/**
 * Peek at the top command (doesn't remove)
 * @param stack The command stack
 * @return Top command, or NULL if empty
 */
Command* command_stack_peek(CommandStack *stack);

/**
 * Check if stack is empty
 * @param stack The command stack
 * @return TRUE if empty, FALSE otherwise
 */
gboolean command_stack_is_empty(CommandStack *stack);

/**
 * Get the size of the stack
 * @param stack The command stack
 * @return Number of commands in stack
 */
guint command_stack_size(CommandStack *stack);

/**
 * Clear all commands from the stack (freeing each)
 * @param stack The command stack
 */
void command_stack_clear(CommandStack *stack);

/**
 * Free the command stack and all commands
 * @param stack The command stack to free
 */
void command_stack_free(CommandStack *stack);

/**
 * Draw command data structure
 * Stores layer snapshots for undo/redo
 */
typedef struct {
    struct ImageLayer *layer;           /* Layer being drawn on */
    cairo_surface_t *before_snapshot;   /* Surface snapshot before draw (for undo) */
    cairo_surface_t *after_snapshot;    /* Surface snapshot after draw (for redo) */
} DrawCommandData;

/**
 * Create a draw command
 * @param layer The layer being drawn on
 * @return Newly created Command for drawing, or NULL on failure
 */
Command* command_create_draw(struct ImageLayer *layer);

/**
 * Finalize a draw command by taking snapshot of state after drawing
 * This must be called after drawing is complete and before pushing to undo stack
 * @param cmd The draw command to finalize
 * @return TRUE on success, FALSE on failure
 */
gboolean command_finalize_draw(Command *cmd);

/**
 * Move command data structure
 * Stores layer offset before move for undo
 */
typedef struct {
    struct ImageLayer *layer;           /* Layer being moved */
    gint old_offset_x;                  /* X offset before move */
    gint old_offset_y;                  /* Y offset before move */
    gint new_offset_x;                  /* X offset after move */
    gint new_offset_y;                  /* Y offset after move */
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
Command* command_create_move(struct ImageLayer *layer, 
                             gint old_x, gint old_y,
                             gint new_x, gint new_y);

#endif /* COMMAND_H */

