#ifndef COMMAND_REVERT_H
#define COMMAND_REVERT_H

#include "app/settings.h"
#include "document_revert_diff.h"

struct ImageDocument;
typedef struct _Command Command;

/**
 * Create an undoable revert command. Takes ownership of @a diff on success.
 * @a reload_path is copied; @a settings is stored by pointer (must outlive the command).
 */
Command* command_create_document_revert(struct ImageDocument* doc,
                                        DocumentRevertDiff* diff,
                                        const gchar* reload_path,
                                        Settings* settings);

#endif /* COMMAND_REVERT_H */
