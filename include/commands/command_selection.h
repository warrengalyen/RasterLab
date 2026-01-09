#ifndef COMMAND_SELECTION_H
#define COMMAND_SELECTION_H

#include "command.h"

/**
 * Create a selection command
 * Used by selection undo system to wrap SelectionUndoDelta changes
 *
 * @param mask The selection mask
 * @param delta The undo delta (ownership transferred to command)
 * @param doc The document
 * @param name Command name (e.g., from command_get_name_string)
 * @return Newly created Command, or NULL on failure
 */
Command* command_create_selection(struct SelectionMask* mask,
                                  SelectionUndoDelta* delta,
                                  struct ImageDocument* doc,
                                  const gchar* name);

#endif /* COMMAND_SELECTION_H */