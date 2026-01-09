#ifndef COMMAND_MOVE_H
#define COMMAND_MOVE_H

#include "command.h"

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

#endif /* COMMAND_MOVE_H */