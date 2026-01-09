#ifndef COMMAND_LAYER_H
#define COMMAND_LAYER_H

#include "command.h"

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
 * Create a paste command (adds layer with "Paste" name in undo/redo)
 * @param doc The document
 * @param layer The layer being pasted
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_paste(struct ImageDocument* doc, struct ImageLayer* layer);

#endif /* COMMAND_LAYER_H */