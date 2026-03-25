#ifndef DOCUMENT_REVERT_DIFF_H
#define DOCUMENT_REVERT_DIFF_H

#include "app/settings.h"
#include "document.h"

#include <glib.h>

/**
 * Minimal undo payload for "Revert to Saved": enough to reconstruct the
 * pre-revert document from the post-revert (reloaded) state without storing a
 * full "after" snapshot.
 */
typedef struct DocumentRevertDiff {
    guint n_before_layers;
    guint n_after_layers;
    guint width, height, channels, bit_depth;
    gboolean has_alpha;
    gboolean modified_flag;
    void* original_icc_data;
    size_t original_icc_size;
    guint selected_layer_index;
    SelectionMask* selection_mask;

    guint slot_count;
    /** Length slot_count (shared prefix); NULL means slot matches on-disk (copy from current doc on undo). */
    ImageLayer** slot_replacement;

    /** Layers that existed only in the pre-revert stack (bottom to top). */
    GList* layers_tail_before;
} DocumentRevertDiff;

DocumentRevertDiff* document_revert_diff_build(ImageDocument* before_doc, ImageDocument* loaded_doc);
void document_revert_diff_free(DocumentRevertDiff* diff);
gboolean document_revert_diff_apply_undo(ImageDocument* doc, const DocumentRevertDiff* diff);

/** Reload file into @a doc (same path as revert redo). */
gboolean document_revert_reload_from_file(ImageDocument* doc, const gchar* path, const Settings* settings);

/**
 * Replace @a doc content with @a loaded (transfers ownership of layers, ICC blob, selection).
 * Frees @a loaded. Does not change @a doc file_path / filename.
 */
gboolean document_revert_apply_loaded_document(ImageDocument* doc, ImageDocument* loaded);

#endif /* DOCUMENT_REVERT_DIFF_H */
