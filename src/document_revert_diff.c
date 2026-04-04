#include "document_revert_diff.h"
#include "debug_logger.h"

#include "app/settings.h"
#include "document.h"
#include "io/image_io.h"
#include "render/dirty.h"
#include "render/gpu_compositor.h"
#include "render/layer.h"
#include "render/tile.h"
#include "selection/selection_mask.h"
#include "undo/undo_disk.h"

#include <stdlib.h>
#include <string.h>

DocumentRevertDiff* document_revert_diff_build(ImageDocument* before_doc, ImageDocument* loaded_doc) {
    DocumentRevertDiff* diff;
    guint n_a, n_b, shared, i;

    if (!before_doc || !loaded_doc || before_doc->width == 0 || before_doc->height == 0) {
        return NULL;
    }

    diff = (DocumentRevertDiff*)g_malloc0(sizeof(DocumentRevertDiff));
    if (!diff) {
        return NULL;
    }

    n_a = g_list_length(before_doc->layers);
    n_b = g_list_length(loaded_doc->layers);
    shared = (n_a < n_b) ? n_a : n_b;

    diff->n_before_layers = n_a;
    diff->n_after_layers = n_b;
    diff->width = before_doc->width;
    diff->height = before_doc->height;
    diff->channels = before_doc->channels;
    diff->bit_depth = before_doc->bit_depth;
    diff->has_alpha = before_doc->has_alpha;
    diff->modified_flag = before_doc->modified;

    if (before_doc->original_icc_data && before_doc->original_icc_size > 0) {
        diff->original_icc_data = malloc(before_doc->original_icc_size);
        if (!diff->original_icc_data) {
            document_revert_diff_free(diff);
            return NULL;
        }
        memcpy(diff->original_icc_data, before_doc->original_icc_data, before_doc->original_icc_size);
        diff->original_icc_size = before_doc->original_icc_size;
    }

    if (before_doc->selected_layer && before_doc->layers) {
        gint pos = g_list_index(before_doc->layers, before_doc->selected_layer);
        diff->selected_layer_index = (pos >= 0) ? (guint)pos : 0;
    } else {
        guint n = g_list_length(before_doc->layers);
        diff->selected_layer_index = n > 0 ? n - 1 : 0;
    }

    if (before_doc->selection_mask) {
        diff->selection_mask = selection_mask_duplicate(before_doc->selection_mask);
        if (!diff->selection_mask) {
            document_revert_diff_free(diff);
            return NULL;
        }
    } else {
        diff->selection_mask = selection_mask_new((int)before_doc->width, (int)before_doc->height);
        if (!diff->selection_mask) {
            document_revert_diff_free(diff);
            return NULL;
        }
    }

    diff->slot_count = shared;
    if (shared > 0) {
        diff->slot_replacement = (ImageLayer**)g_malloc0(sizeof(ImageLayer*) * shared);
        if (!diff->slot_replacement) {
            document_revert_diff_free(diff);
            return NULL;
        }
        for (i = 0; i < shared; i++) {
            ImageLayer* la = document_get_layer(before_doc, i);
            ImageLayer* lb = document_get_layer(loaded_doc, i);
            if (!la || !lb) {
                document_revert_diff_free(diff);
                return NULL;
            }
            if (!layer_equal_content(la, lb)) {
                diff->slot_replacement[i] = layer_duplicate_deep(la, NULL);
                if (!diff->slot_replacement[i]) {
                    document_revert_diff_free(diff);
                    return NULL;
                }
            }
        }
    }

    if (n_a > n_b) {
        for (i = n_b; i < n_a; i++) {
            ImageLayer* la = document_get_layer(before_doc, i);
            ImageLayer* copy;
            if (!la) {
                document_revert_diff_free(diff);
                return NULL;
            }
            copy = layer_duplicate_deep(la, NULL);
            if (!copy) {
                document_revert_diff_free(diff);
                return NULL;
            }
            diff->layers_tail_before = g_list_append(diff->layers_tail_before, copy);
        }
    }

    return diff;
}

void document_revert_diff_free(DocumentRevertDiff* diff) {
    guint i;

    if (!diff) {
        return;
    }

    if (diff->slot_replacement) {
        for (i = 0; i < diff->slot_count; i++) {
            if (diff->slot_replacement[i]) {
                layer_free(diff->slot_replacement[i]);
            }
        }
        g_free(diff->slot_replacement);
    }

    for (GList* iter = diff->layers_tail_before; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(diff->layers_tail_before);

    if (diff->selection_mask) {
        selection_mask_free(diff->selection_mask);
    }

    if (diff->original_icc_data) {
        free(diff->original_icc_data);
    }

    g_free(diff);
}

gboolean document_revert_diff_apply_undo(ImageDocument* doc, const DocumentRevertDiff* diff) {
    GList* iter;
    GList* new_layers = NULL;
    ImageLayer* lyr;
    guint n_layers;
    SelectionMask* new_sel = NULL;
    guint shared;
    guint i;

    if (!doc || !diff) {
        return FALSE;
    }

    document_clear_content_replace_caches(doc);

    shared = diff->slot_count;

    while (g_list_length(doc->layers) > diff->n_before_layers) {
        GList* last = g_list_last(doc->layers);
        ImageLayer* rem = (ImageLayer*)last->data;
        if (doc->tile_grid && rem) {
            tile_grid_invalidate_layer_cache(doc->tile_grid, rem);
        }
        doc->layers = g_list_remove_link(doc->layers, last);
        g_list_free_1(last);
        layer_free(rem);
    }

    for (i = 0; i < shared; i++) {
        ImageLayer* cur = document_get_layer(doc, i);
        ImageLayer* dup;
        if (!cur) {
            goto fail_layers;
        }
        if (diff->slot_replacement && diff->slot_replacement[i]) {
            dup = layer_duplicate_deep(diff->slot_replacement[i], NULL);
        } else {
            dup = layer_duplicate_deep(cur, NULL);
        }
        if (!dup) {
            goto fail_layers;
        }
        new_layers = g_list_append(new_layers, dup);
    }

    for (iter = diff->layers_tail_before; iter; iter = iter->next) {
        lyr = layer_duplicate_deep((ImageLayer*)iter->data, NULL);
        if (!lyr) {
            goto fail_layers;
        }
        new_layers = g_list_append(new_layers, lyr);
    }

    new_sel = selection_mask_duplicate(diff->selection_mask);
    if (!new_sel) {
        goto fail_layers;
    }

    for (iter = doc->layers; iter; iter = iter->next) {
        lyr = (ImageLayer*)iter->data;
        if (doc->tile_grid && lyr) {
            tile_grid_invalidate_layer_cache(doc->tile_grid, lyr);
        }
    }

    for (iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = new_layers;
    new_layers = NULL;

    doc->width = diff->width;
    doc->height = diff->height;
    doc->channels = diff->channels;
    doc->bit_depth = diff->bit_depth;
    doc->has_alpha = diff->has_alpha;

    if (doc->original_icc_data) {
        free(doc->original_icc_data);
        doc->original_icc_data = NULL;
        doc->original_icc_size = 0;
    }
    if (diff->original_icc_data && diff->original_icc_size > 0) {
        doc->original_icc_data = malloc(diff->original_icc_size);
        if (doc->original_icc_data) {
            memcpy(doc->original_icc_data, diff->original_icc_data, diff->original_icc_size);
            doc->original_icc_size = diff->original_icc_size;
        }
    }

    if (doc->selection_mask) {
        selection_mask_free(doc->selection_mask);
    }
    doc->selection_mask = new_sel;
    new_sel = NULL;

    n_layers = g_list_length(doc->layers);
    if (n_layers > 0 && diff->selected_layer_index < n_layers) {
        doc->selected_layer = (ImageLayer*)g_list_nth_data(doc->layers, diff->selected_layer_index);
    } else if (n_layers > 0) {
        doc->selected_layer = (ImageLayer*)g_list_nth_data(doc->layers, n_layers - 1);
    } else {
        doc->selected_layer = NULL;
    }

    if (doc->composite_surface) {
        cairo_surface_flush(doc->composite_surface);
        cairo_surface_destroy(doc->composite_surface);
        doc->composite_surface = NULL;
    }
    doc->composite_dirty = TRUE;
    dirty_rect_init(&doc->dirty_region);
    if (doc->dirty_region_list) {
        dirty_region_list_clear(doc->dirty_region_list);
    }

    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(doc->width, doc->height, 128);
    if (!doc->tile_grid) {
        debug_log("WRN", "document_revert_diff_apply_undo: failed to create tile grid");
        return FALSE;
    }

    for (iter = doc->layers; iter; iter = iter->next) {
        tile_grid_invalidate_layer_cache(doc->tile_grid, (ImageLayer*)iter->data);
    }

    if (doc->undo_journal) {
        undo_journal_clear_all(doc->undo_journal);
    }

    doc->modified = diff->modified_flag;

    document_invalidate_composite(doc);

    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    if (doc->ruler_h && gtk_widget_get_visible(doc->ruler_h)) {
        gtk_widget_queue_draw(doc->ruler_h);
    }
    if (doc->ruler_v && gtk_widget_get_visible(doc->ruler_v)) {
        gtk_widget_queue_draw(doc->ruler_v);
    }

    return TRUE;

fail_layers:
    for (iter = new_layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(new_layers);
    if (new_sel) {
        selection_mask_free(new_sel);
    }
    return FALSE;
}

gboolean document_revert_reload_from_file(ImageDocument* doc, const gchar* path, const Settings* settings) {
    GList* iter;
    ImageLayer* lyr;

    if (!doc || !path) {
        return FALSE;
    }

    document_clear_content_replace_caches(doc);

    for (iter = doc->layers; iter; iter = iter->next) {
        lyr = (ImageLayer*)iter->data;
        if (doc->tile_grid && lyr) {
            tile_grid_invalidate_layer_cache(doc->tile_grid, lyr);
        }
    }

    {
        PluginError load_error = PLUGIN_ERROR_NONE;
        if (!image_io_load(doc, path, &load_error, settings)) {
            return FALSE;
        }
    }

    if (!document_init_rendering_structures(doc)) {
        return FALSE;
    }

    {
        ImageLayer* l0 = document_get_layer(doc, 0);
        if (l0) {
            document_set_selected_layer(doc, l0);
        }
    }

    for (iter = doc->layers; iter; iter = iter->next) {
        lyr = (ImageLayer*)iter->data;
        if (doc->tile_grid && lyr) {
            tile_grid_invalidate_layer_cache(doc->tile_grid, lyr);
        }
    }

    document_invalidate_composite(doc);

    if (doc->composite_surface) {
        cairo_surface_flush(doc->composite_surface);
        cairo_surface_destroy(doc->composite_surface);
        doc->composite_surface = NULL;
    }
    doc->composite_dirty = TRUE;
    dirty_rect_init(&doc->dirty_region);
    if (doc->dirty_region_list) {
        dirty_region_list_clear(doc->dirty_region_list);
    }

    if (doc->undo_journal) {
        undo_journal_clear_all(doc->undo_journal);
    }

    doc->modified = FALSE;

    document_invalidate_composite(doc);

    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    if (doc->ruler_h && gtk_widget_get_visible(doc->ruler_h)) {
        gtk_widget_queue_draw(doc->ruler_h);
    }
    if (doc->ruler_v && gtk_widget_get_visible(doc->ruler_v)) {
        gtk_widget_queue_draw(doc->ruler_v);
    }

    return TRUE;
}

gboolean document_revert_apply_loaded_document(ImageDocument* doc, ImageDocument* loaded) {
    GList* iter;
    TileGrid* new_grid;
    guint n_layers;
    ImageLayer* lyr;

    if (!doc || !loaded) {
        return FALSE;
    }

    new_grid = tile_grid_create(loaded->width, loaded->height, 128);
    if (!new_grid) {
        debug_log("WRN", "document_revert_apply_loaded_document: failed to create tile grid");
        return FALSE;
    }

    document_clear_content_replace_caches(doc);

    for (iter = doc->layers; iter; iter = iter->next) {
        lyr = (ImageLayer*)iter->data;
        if (doc->tile_grid && lyr) {
            tile_grid_invalidate_layer_cache(doc->tile_grid, lyr);
        }
    }

    for (iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    doc->layers = loaded->layers;
    loaded->layers = NULL;

    doc->width = loaded->width;
    doc->height = loaded->height;
    doc->channels = loaded->channels;
    doc->bit_depth = loaded->bit_depth;
    doc->has_alpha = loaded->has_alpha;

    if (doc->original_icc_data) {
        free(doc->original_icc_data);
        doc->original_icc_data = NULL;
        doc->original_icc_size = 0;
    }
    if (loaded->original_icc_data && loaded->original_icc_size > 0) {
        doc->original_icc_data = loaded->original_icc_data;
        doc->original_icc_size = loaded->original_icc_size;
        loaded->original_icc_data = NULL;
        loaded->original_icc_size = 0;
    }

    if (doc->selection_mask) {
        selection_mask_free(doc->selection_mask);
    }
    doc->selection_mask = loaded->selection_mask;
    loaded->selection_mask = NULL;

    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    if (loaded->tile_grid) {
        tile_grid_free(loaded->tile_grid);
        loaded->tile_grid = NULL;
    }
    doc->tile_grid = new_grid;

    for (iter = doc->layers; iter; iter = iter->next) {
        tile_grid_invalidate_layer_cache(doc->tile_grid, (ImageLayer*)iter->data);
    }

    if (doc->composite_surface) {
        cairo_surface_flush(doc->composite_surface);
        cairo_surface_destroy(doc->composite_surface);
        doc->composite_surface = NULL;
    }
    doc->composite_dirty = TRUE;
    dirty_rect_init(&doc->dirty_region);
    if (doc->dirty_region_list) {
        dirty_region_list_clear(doc->dirty_region_list);
    }

    n_layers = g_list_length(doc->layers);
    if (n_layers > 0) {
        ImageLayer* l0 = document_get_layer(doc, 0);
        if (l0) {
            document_set_selected_layer(doc, l0);
        }
    } else {
        doc->selected_layer = NULL;
    }

    if (doc->undo_journal) {
        undo_journal_clear_all(doc->undo_journal);
    }

    doc->modified = FALSE;

    document_invalidate_composite(doc);

    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    if (doc->ruler_h && gtk_widget_get_visible(doc->ruler_h)) {
        gtk_widget_queue_draw(doc->ruler_h);
    }
    if (doc->ruler_v && gtk_widget_get_visible(doc->ruler_v)) {
        gtk_widget_queue_draw(doc->ruler_v);
    }

    document_free(loaded);
    return TRUE;
}
