#include "commands/command_revert.h"
#include "command.h"
#include "document.h"
#include <glib.h>

typedef struct {
    DocumentContentSnapshot* before_snap;
    DocumentContentSnapshot* after_snap;
} RevertCommandData;

static void revert_command_apply(Command* cmd, struct ImageDocument* doc) {
    RevertCommandData* d = (RevertCommandData*)cmd->user_data;

    if (!d || !d->after_snap || !doc) {
        return;
    }
    document_content_snapshot_apply(doc, d->after_snap);
}

static void revert_command_revert(Command* cmd, struct ImageDocument* doc) {
    RevertCommandData* d = (RevertCommandData*)cmd->user_data;

    if (!d || !d->before_snap || !doc) {
        return;
    }
    document_content_snapshot_apply(doc, d->before_snap);
}

static void revert_command_destroy(Command* cmd) {
    RevertCommandData* d = (RevertCommandData*)cmd->user_data;

    if (!d) {
        return;
    }
    document_content_snapshot_free(d->before_snap);
    document_content_snapshot_free(d->after_snap);
    g_free(d);
}

Command* command_create_document_revert(struct ImageDocument* doc,
                                        DocumentContentSnapshot* before_snap,
                                        DocumentContentSnapshot* after_snap) {
    Command* cmd;
    RevertCommandData* data;

    if (!doc || !before_snap || !after_snap) {
        return NULL;
    }

    data = (RevertCommandData*)g_malloc0(sizeof(RevertCommandData));
    if (!data) {
        return NULL;
    }
    data->before_snap = before_snap;
    data->after_snap = after_snap;

    cmd = command_new("Revert", COMMAND_DOCUMENT_REVERT,
                      revert_command_apply, revert_command_revert, revert_command_destroy);
    if (!cmd) {
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;
    cmd->document = doc;
    return cmd;
}
