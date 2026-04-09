/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef SELECTION_UNDO_H
#define SELECTION_UNDO_H

#include "command.h"
#include "selection.h"
#include <glib.h>
#include <stdint.h>

/**
 * Selection undo/redo system
 *
 * Integrates mask-based selection changes into the disk-backed undo system.
 * Uses delta-based storage to minimize disk usage:
 * - Only the affected bounding region is stored
 * - Before/after mask data is compressed with LZ4
 * - Selection undo entries do not affect layer undo history
 */

/* Forward declarations */
typedef struct ImageDocument ImageDocument;
typedef struct SelectionMask SelectionMask;

/* SelectionUndoDelta is defined in command.h */

/**
 * Selection undo transaction state (internal)
 * Tracks which regions are modified during a selection operation
 */
typedef struct _SelectionUndoTransaction SelectionUndoTransaction;

/**
 * Create a selection undo delta for a rectangular region
 * Captures the current mask state for the given region
 *
 * @param mask The selection mask to capture from
 * @param region_x Left coordinate of region
 * @param region_y Top coordinate of region
 * @param region_width Region width in pixels
 * @param region_height Region height in pixels
 * @return New SelectionUndoDelta, or NULL on error
 *         Caller must free with selection_undo_delta_free()
 */
SelectionUndoDelta* selection_undo_delta_create(
    SelectionMask* mask,
    gint region_x,
    gint region_y,
    gint region_width,
    gint region_height);

/**
 * Free a selection undo delta and its buffers
 * @param delta The delta to free
 */
void selection_undo_delta_free(SelectionUndoDelta* delta);

/**
 * Begin a selection undo transaction
 * Captures the current selection mask state (used as "before" for delta)
 *
 * @param mask The selection mask
 * @param doc The document
 * @param operation_name Human-readable name (e.g., "Feather Selection")
 * @return Transaction handle, or NULL on failure
 */
SelectionUndoTransaction* selection_undo_transaction_begin(
    SelectionMask* mask,
    ImageDocument* doc,
    const gchar* operation_name);

/**
 * Register a region as modified in the transaction
 * The transaction tracks the bounding box of all registered regions
 * Multiple registrations are merged into a single bounding region
 *
 * @param transaction Transaction handle
 * @param region_x Left coordinate
 * @param region_y Top coordinate
 * @param region_width Region width
 * @param region_height Region height
 * @return TRUE on success, FALSE on error
 */
gboolean selection_undo_transaction_register_region(
    SelectionUndoTransaction* transaction,
    gint region_x,
    gint region_y,
    gint region_width,
    gint region_height);

/**
 * Commit a selection undo transaction
 * Captures the "after" state and creates a command for undo/redo
 *
 * @param transaction Transaction handle
 * @return Created Command, or NULL on failure
 *         Command is NULL if the selection did not change
 *         Caller must push to undo stack or free if not used
 *         Transaction is invalid after this call
 */
Command* selection_undo_transaction_commit(SelectionUndoTransaction* transaction);

/**
 * Cancel a selection undo transaction without creating a command
 * Frees all resources
 *
 * @param transaction Transaction handle
 */
void selection_undo_transaction_cancel(SelectionUndoTransaction* transaction);

/**
 * Create a command for selecting all (empty selection → full selection)
 * @param mask The selection mask
 * @param doc The document
 * @return New Command, or NULL on failure
 */
Command* selection_command_create_select_all(SelectionMask* mask, ImageDocument* doc);

/**
 * Create a command for deselecting all (full selection → empty selection)
 * @param mask The selection mask
 * @param doc The document
 * @return New Command, or NULL on failure
 */
Command* selection_command_create_deselect_all(SelectionMask* mask, ImageDocument* doc);

/**
 * Create a command for inverting selection
 * @param mask The selection mask
 * @param doc The document
 * @return New Command, or NULL on failure
 */
Command* selection_command_create_invert(SelectionMask* mask, ImageDocument* doc);

/**
 * Create a command for feathering selection
 * @param mask The selection mask
 * @param doc The document
 * @param feather_radius Feather radius in pixels
 * @return New Command, or NULL on failure
 */
Command* selection_command_create_feather(SelectionMask* mask, ImageDocument* doc, float feather_radius);

/**
 * Apply an undo for a selection operation
 * Restores the mask to its "before" state
 *
 * @param mask The selection mask to restore to
 * @param delta The undo delta containing "before" state
 * @return TRUE on success, FALSE on error
 */
gboolean selection_undo_apply_before(SelectionMask* mask, SelectionUndoDelta* delta);

/**
 * Apply a redo for a selection operation
 * Restores the mask to its "after" state
 *
 * @param mask The selection mask to restore to
 * @param delta The undo delta containing "after" state
 * @return TRUE on success, FALSE on error
 */
gboolean selection_undo_apply_after(SelectionMask* mask, SelectionUndoDelta* delta);

/**
 * Serialize a selection undo delta for disk storage
 * Compresses mask data and returns serialized bytes
 *
 * @param delta The delta to serialize
 * @param compression_level LZ4 compression level (1-9)
 * @param out_data Pointer to receive serialized data (must be freed with g_free)
 * @param out_size Pointer to receive serialized data size
 * @return TRUE on success, FALSE on error
 */
gboolean selection_undo_delta_serialize(
    SelectionUndoDelta* delta,
    gint compression_level,
    guint8** out_data,
    guint* out_size);

/**
 * Deserialize a selection undo delta from disk storage
 * Decompresses mask data
 *
 * @param data Serialized data
 * @param size Size of serialized data
 * @param out_delta Pointer to receive deserialized delta
 * @return TRUE on success, FALSE on error
 *         Caller must free result with selection_undo_delta_free()
 */
gboolean selection_undo_delta_deserialize(
    const guint8* data,
    guint size,
    SelectionUndoDelta** out_delta);

#endif /* SELECTION_UNDO_H */
