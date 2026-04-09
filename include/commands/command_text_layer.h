/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef COMMAND_TEXT_LAYER_H
#define COMMAND_TEXT_LAYER_H

#include "command.h"
#include <glib.h>
#include <pango/pango.h>   /* PangoWeight, PangoStyle */

struct ImageDocument;
struct ImageLayer;

/* =========================================================================
 * TextLayerState — full deep-copy snapshot of all TextLayer properties.
 *
 * Used exclusively by the rasterize command, which must restore every
 * property at once when undoing a text→raster conversion.
 * ========================================================================= */

typedef struct {
    char       *text;
    char       *font_family;
    int         font_size;
    PangoWeight font_weight;
    PangoStyle  font_style;

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

    /* Extended typography */
    gboolean  kerning;
    char     *opentype_features;
} TextLayerState;

TextLayerState *text_layer_state_create (const void *tl);
void            text_layer_state_restore(void *tl, const TextLayerState *state);
void            text_layer_state_free   (TextLayerState *state);

/* =========================================================================
 * Property-based undo for TextLayer
 *
 * Instead of snapshotting the full TextLayer for every change, only the
 * modified property is recorded.  Multiple related changes are batched into
 * a transaction and pushed to the undo stack as a single atomic Command.
 *
 * Coalescing: if the same (layer, property) pair is pushed more than once
 * within one transaction, only the first "before" and the most recent
 * "after" value are retained, so the undo stack stays compact.
 * ========================================================================= */

typedef enum {
    TEXT_PROP_TEXT,           /* char*        — full text string              */
    TEXT_PROP_FONT,           /* char*        — font-family name              */
    TEXT_PROP_SIZE,           /* int          — font size in points           */
    TEXT_PROP_WEIGHT,         /* int          — PangoWeight (400, 700, …)     */
    TEXT_PROP_STYLE,          /* int          — PangoStyle (NORMAL/OBLIQUE/ITALIC) */
    TEXT_PROP_COLOR,          /* color        — r/g/b/a doubles               */
    TEXT_PROP_ALIGNMENT,      /* int          — 0/1/2 = left/center/right     */
    TEXT_PROP_LINE_SPACING,   /* double                                       */
    TEXT_PROP_LETTER_SPACING, /* double                                       */
    TEXT_PROP_ROTATION,       /* double       — degrees                       */
    TEXT_PROP_BOX_GEOMETRY,   /* box          — x/y/w/h doubles               */
    TEXT_PROP_ANTIALIAS,      /* bool                                         */
    TEXT_PROP_KERNING,        /* bool         — disable/enable kern feature   */
    TEXT_PROP_OT_FEATURES     /* char*        — CSS font-feature-settings str */
} TextLayerProperty;

/* Tagged union — only the field matching @property is valid. */
typedef union {
    char    *string_val;         /* TEXT_PROP_TEXT, TEXT_PROP_FONT */
    int      int_val;            /* TEXT_PROP_SIZE, TEXT_PROP_WEIGHT, TEXT_PROP_ALIGNMENT */
    double   double_val;         /* TEXT_PROP_ROTATION, *_SPACING */
    gboolean bool_val;           /* TEXT_PROP_ITALIC, TEXT_PROP_ANTIALIAS */
    struct { double x, y, w, h; } box;   /* TEXT_PROP_BOX_GEOMETRY */
    struct { double r, g, b, a; } color; /* TEXT_PROP_COLOR */
} TextLayerPropValue;

/* ── Transaction API ─────────────────────────────────────────────────────
 *
 * Usage pattern for a drag operation:
 *
 *   undo_begin_transaction(doc, "Move Text");
 *   // ... drag updates tl->box_x/y in real-time, no undo calls here ...
 *   // On mouse-up:
 *   text_layer_push_property_change(doc, layer, TEXT_PROP_BOX_GEOMETRY, &before, &after);
 *   undo_commit_transaction(doc);   // → one entry on the undo stack
 *
 * Usage pattern for a text-editing session:
 *
 *   undo_begin_transaction(doc, "Edit Text");
 *   // ... keystrokes modify tl->text, no undo calls per keystroke ...
 *   // On session end:
 *   text_layer_push_property_change(doc, layer, TEXT_PROP_TEXT, &before, &after);
 *   undo_commit_transaction(doc);   // → one entry on the undo stack
 *
 * If nothing was pushed between begin and commit the transaction is a no-op.
 * Calling undo_cancel_transaction() discards any pending operations.
 * ------------------------------------------------------------------------- */

void undo_begin_transaction  (struct ImageDocument *doc, const char *name);
void undo_commit_transaction (struct ImageDocument *doc);
void undo_cancel_transaction (void);

/* Push a single property change.
 *
 * If a transaction is active the change is collected inside it (with
 * coalescing).  Otherwise a Command is created and pushed immediately.
 *
 * @before  Value of @property before the change was applied.
 * @after   Value of @property after  the change was applied.
 *
 * For string properties (TEXT_PROP_TEXT, TEXT_PROP_FONT) the strings pointed
 * to by before->string_val / after->string_val are g_strdup'd internally;
 * the caller retains ownership of its originals.
 */
void text_layer_push_property_change(struct ImageDocument    *doc,
                                     struct ImageLayer       *layer,
                                     TextLayerProperty        property,
                                     const TextLayerPropValue *before,
                                     const TextLayerPropValue *after);

/* =========================================================================
 * Rasterize command
 *
 * Converts a LAYER_TYPE_TEXT layer to LAYER_TYPE_RASTER in-place.
 * Undo restores the full TextLayer; redo re-applies the raster surface.
 * The caller only needs to push the returned Command onto the undo stack.
 * Returns NULL on error; on success the layer is already rasterized.
 * ========================================================================= */

Command *command_create_text_layer_rasterize(struct ImageDocument *doc,
                                              struct ImageLayer    *layer);

#endif /* COMMAND_TEXT_LAYER_H */
