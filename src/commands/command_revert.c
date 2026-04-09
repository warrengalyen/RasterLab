/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "commands/command_revert.h"
#include "command.h"
#include "document.h"
#include <glib.h>

typedef struct {
    DocumentRevertDiff* diff;
    gchar* reload_path;
    Settings* settings;
} RevertCommandData;

static void revert_command_apply(Command* cmd, struct ImageDocument* doc) {
    RevertCommandData* d = (RevertCommandData*)cmd->user_data;

    if (!d || !d->reload_path || !doc) {
        return;
    }
    document_revert_reload_from_file(doc, d->reload_path, d->settings);
}

static void revert_command_revert(Command* cmd, struct ImageDocument* doc) {
    RevertCommandData* d = (RevertCommandData*)cmd->user_data;

    if (!d || !d->diff || !doc) {
        return;
    }
    document_revert_diff_apply_undo(doc, d->diff);
}

static void revert_command_destroy(Command* cmd) {
    RevertCommandData* d = (RevertCommandData*)cmd->user_data;

    if (!d) {
        return;
    }
    document_revert_diff_free(d->diff);
    g_free(d->reload_path);
    g_free(d);
}

Command* command_create_document_revert(struct ImageDocument* doc,
                                        DocumentRevertDiff* diff,
                                        const gchar* reload_path,
                                        Settings* settings) {
    Command* cmd;
    RevertCommandData* data;

    if (!doc || !diff || !reload_path || reload_path[0] == '\0') {
        return NULL;
    }

    data = (RevertCommandData*)g_malloc0(sizeof(RevertCommandData));
    if (!data) {
        return NULL;
    }
    data->diff = diff;
    data->reload_path = g_strdup(reload_path);
    data->settings = settings;
    if (!data->reload_path) {
        g_free(data);
        return NULL;
    }

    cmd = command_new("Revert", COMMAND_DOCUMENT_REVERT,
                      revert_command_apply, revert_command_revert, revert_command_destroy);
    if (!cmd) {
        g_free(data->reload_path);
        g_free(data);
        return NULL;
    }

    cmd->user_data = data;
    cmd->document = doc;
    return cmd;
}
