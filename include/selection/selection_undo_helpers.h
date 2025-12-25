#ifndef SELECTION_UNDO_HELPERS_H
#define SELECTION_UNDO_HELPERS_H

#include "command.h"
#include "document.h"
#include "selection/selection_mask.h"
#include "selection/selection_undo.h"
#include <glib.h>

/**
 * Integration helpers for selection undo system
 * These functions provide convenient ways to:
 * - Commit selection operations to the undo stack
 * - Dispatch undo/redo for selection commands
 * - Write/read selection undo entries to disk
 */

/* Forward declarations */
typedef struct _UndoJournal UndoJournal;

/**
 * Commit a completed selection operation to the undo stack
 * Pushes to undo stack and clears redo stack
 *
 * @param doc The document
 * @param cmd The selection command to commit
 * @return TRUE on success, FALSE on failure
 */
gboolean selection_undo_commit_operation(ImageDocument* doc, Command* cmd);

/**
 * Dispatch undo for a selection command
 * Handles both in-memory and disk-backed selection undo
 *
 * @param doc The document
 * @param cmd The command to undo
 * @return TRUE on success, FALSE on failure
 */
gboolean selection_undo_dispatch_undo(ImageDocument* doc, Command* cmd);

/**
 * Dispatch redo for a selection command
 * Handles both in-memory and disk-backed selection redo
 *
 * @param doc The document
 * @param cmd The command to redo
 * @return TRUE on success, FALSE on failure
 */
gboolean selection_undo_dispatch_redo(ImageDocument* doc, Command* cmd);

/**
 * Helper: Complete a selection transaction and add to undo stack
 * Convenience function that combines transaction commit + stack push + UI update
 *
 * @param transaction Active transaction
 * @param doc The document
 * @param ctx Optional UI context for update (can be NULL)
 * @return TRUE on success, FALSE on failure
 */
gboolean selection_undo_complete_transaction(
    SelectionUndoTransaction* transaction,
    ImageDocument* doc,
    gpointer ctx);

/**
 * Helper: Cancel a selection transaction and free resources
 * @param transaction Active transaction
 */
void selection_undo_abort_transaction(SelectionUndoTransaction* transaction);

#endif /* SELECTION_UNDO_HELPERS_H */
