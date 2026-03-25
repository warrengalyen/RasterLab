#ifndef COMMAND_REVERT_H
#define COMMAND_REVERT_H

#include "document.h"

struct ImageDocument;
typedef struct _Command Command;

/**
 * Create an undoable revert command. Takes ownership of both snapshots on success.
 */
Command* command_create_document_revert(struct ImageDocument* doc,
                                        DocumentContentSnapshot* before_snap,
                                        DocumentContentSnapshot* after_snap);

#endif /* COMMAND_REVERT_H */
