#ifndef COMMAND_TEXT_LAYER_H
#define COMMAND_TEXT_LAYER_H

#include "command.h"
#include <glib.h>

/**
 * Forward declarations
 */
struct ImageDocument;
struct ImageLayer;

/* =========================================================================
 * TextLayerState — a deep copy of all TextLayer properties
 *
 * Used to snapshot text layer state before and after an operation so that
 * undo/redo can restore every property in a single atomic step.
 * ========================================================================= */

typedef struct {
    char    *text;
    char    *font_family;
    int      font_size;
    int      font_weight;
    gboolean italic;

    double   color_r;
    double   color_g;
    double   color_b;
    double   color_a;

    double   line_spacing;
    double   letter_spacing;
    int      alignment;

    double   rotation;

    double   box_x;
    double   box_y;
    double   box_width;
    double   box_height;

    gboolean antialias;
} TextLayerState;

/**
 * Create a deep copy of all TextLayer properties.
 * @param tl  Source TextLayer (must not be NULL)
 * @return    Newly allocated TextLayerState, or NULL on allocation failure.
 *            Caller must call text_layer_state_free() when done.
 */
TextLayerState *text_layer_state_create(const void *tl);

/**
 * Restore a TextLayer's properties from a previously captured state.
 * All string fields are deep-copied; the layer's existing strings are freed.
 * @param tl     Destination TextLayer
 * @param state  Source state
 */
void text_layer_state_restore(void *tl, const TextLayerState *state);

/**
 * Free a TextLayerState and its owned strings.
 * Safe to call with NULL.
 */
void text_layer_state_free(TextLayerState *state);

/* =========================================================================
 * Text layer property-change command
 *
 * Covers all single-property and multi-property edits: text content, font,
 * size, weight, italic, color, alignment, spacing, rotation, box geometry,
 * and antialias.  The before/after TextLayerState pair is stored and used
 * for undo and redo respectively.
 *
 * Ownership of @before and @after is transferred to the command; they must
 * NOT be freed by the caller.  On failure both are freed automatically.
 * ========================================================================= */

/**
 * Create a text layer property-change command.
 *
 * @param doc    Document that owns the layer
 * @param layer  The text ImageLayer whose properties were changed
 * @param before State snapshot taken BEFORE the change (ownership transferred)
 * @param after  State snapshot taken AFTER the change  (ownership transferred)
 * @param name   Human-readable undo name (e.g. "Change Font", "Edit Text")
 * @return       New Command ready to push on the undo stack, or NULL on error.
 */
Command *command_create_text_layer_prop(struct ImageDocument *doc,
                                        struct ImageLayer    *layer,
                                        TextLayerState       *before,
                                        TextLayerState       *after,
                                        const char           *name);

/* =========================================================================
 * Text layer rasterize command
 *
 * Converts a LAYER_TYPE_TEXT layer to a LAYER_TYPE_RASTER layer in-place:
 *   1. Renders text to layer->surface via Pango+Cairo.
 *   2. Frees text_data and marks the layer as raster.
 *
 * Undo restores text_data from the captured TextLayerState and reverts the
 * layer type back to LAYER_TYPE_TEXT so the text remains vector-editable.
 *
 * This function BOTH performs the rasterization AND creates the Command.
 * The caller only needs to push the returned command onto the undo stack.
 *
 * @param doc    Document that owns the layer
 * @param layer  An ImageLayer with layer_type == LAYER_TYPE_TEXT
 * @return       Command ready to push on the undo stack, or NULL on error.
 *               On success the layer has already been rasterized.
 ========================================================================= */
Command *command_create_text_layer_rasterize(struct ImageDocument *doc,
                                              struct ImageLayer    *layer);

#endif /* COMMAND_TEXT_LAYER_H */
