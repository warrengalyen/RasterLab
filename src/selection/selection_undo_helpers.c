#include "selection/selection_undo_helpers.h"
#include "ui.h"
#include <glib.h>

/**
 * Commit a completed selection operation to the undo stack
 */
gboolean selection_undo_commit_operation(ImageDocument* doc, Command* cmd) {
    if (!doc || !cmd || !doc->undo_stack) {
        return FALSE;
    }

    /* Push to undo stack */
    command_stack_push(doc->undo_stack, cmd);

    /* Clear redo stack (new operation branch) */
    if (doc->redo_stack) {
        command_stack_clear(doc->redo_stack);
    }

    /* Clear redo stack in undo journal if present */
    if (doc->undo_journal) {
        extern void undo_journal_clear_redo(void*);
        undo_journal_clear_redo(doc->undo_journal);
    }

    return TRUE;
}

/**
 * Dispatch undo for a selection command
 */
gboolean selection_undo_dispatch_undo(ImageDocument* doc, Command* cmd) {
    if (!doc || !cmd) {
        return FALSE;
    }

    /* Execute the revert callback to apply "before" state */
    command_undo(cmd, doc);

    return TRUE;
}

/**
 * Dispatch redo for a selection command
 */
gboolean selection_undo_dispatch_redo(ImageDocument* doc, Command* cmd) {
    if (!doc || !cmd) {
        return FALSE;
    }

    /* Execute the apply callback to restore "after" state */
    command_execute(cmd, doc);

    return TRUE;
}

/**
 * Helper: Complete a selection transaction and add to undo stack
 */
gboolean selection_undo_complete_transaction(
    SelectionUndoTransaction* transaction,
    ImageDocument* doc,
    gpointer ctx) {
    Command* cmd;

    if (!transaction || !doc) {
        return FALSE;
    }

    /* Commit the transaction to create a command */
    cmd = selection_undo_transaction_commit(transaction);

    if (!cmd) {
        /* No change in selection, transaction was freed by commit */
        return TRUE;
    }

    /* Push to undo stack */
    if (!selection_undo_commit_operation(doc, cmd)) {
        command_free(cmd);
        return FALSE;
    }

    /* Update UI if context available */
    if (ctx) {
        AppContext* app_ctx = (AppContext*)ctx;
        ui_update_menu_and_button_states(app_ctx);
        ui_update_window_title(app_ctx, NULL);
    }

    return TRUE;
}

/**
 * Helper: Cancel a selection transaction
 */
void selection_undo_abort_transaction(SelectionUndoTransaction* transaction) {
    if (!transaction) {
        return;
    }

    selection_undo_transaction_cancel(transaction);
}
