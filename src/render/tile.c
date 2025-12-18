#include "render/tile.h"
#include "render/compositor.h"
#include "render/layer.h"
#include <stdlib.h>
#include <string.h>

/**
 * Create a new tile grid for a document
 */
TileGrid* tile_grid_create(gint width, gint height, gint tile_size) {
    TileGrid* grid;
    gint x, y;

    if (width <= 0 || height <= 0 || tile_size <= 0) {
        return NULL;
    }

    grid = (TileGrid*)g_malloc(sizeof(TileGrid));
    grid->tile_size = tile_size;

    /* Calculate number of tiles needed */
    grid->tiles_x = (width + tile_size - 1) / tile_size;  /* Round up */
    grid->tiles_y = (height + tile_size - 1) / tile_size; /* Round up */

    /* Allocate 2D array: tiles[y][x] */
    grid->tiles = (Tile**)g_malloc(sizeof(Tile*) * grid->tiles_y);

    for (y = 0; y < grid->tiles_y; y++) {
        grid->tiles[y] = (Tile*)g_malloc(sizeof(Tile) * grid->tiles_x);

        for (x = 0; x < grid->tiles_x; x++) {
            Tile* tile = &grid->tiles[y][x];

            /* Initialize tile */
            tile->x = x;
            tile->y = y;
            tile->px = x * tile_size;
            tile->py = y * tile_size;

            /* Calculate actual tile size (edge tiles may be smaller) */
            tile->w = (tile->px + tile_size > width) ? (width - tile->px) : tile_size;
            tile->h = (tile->py + tile_size > height) ? (height - tile->py) : tile_size;

            /* Allocate pixel buffer for worker threads */
            tile->stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, tile->w);
            tile->pixel_buffer = (uint8_t*)g_malloc0(tile->stride * tile->h);

            if (!tile->pixel_buffer) {
                g_warning("Failed to allocate pixel buffer for tile at (%d, %d)", x, y);
            }

            /* Create tile surface with Cairo's recommended stride */
            tile->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tile->w, tile->h);

            if (cairo_surface_status(tile->surface) != CAIRO_STATUS_SUCCESS) {
                g_warning("Failed to create tile surface at (%d, %d)", x, y);
                tile->surface = NULL;
            }

            /* Initialize to transparent */
            if (tile->surface) {
                cairo_t* cr = cairo_create(tile->surface);
                cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
                cairo_paint(cr);
                cairo_destroy(cr);
            }

            tile->dirty = TRUE; /* All tiles start dirty and need initial compositing */
            tile->state = TILE_CLEAN;
            tile->generation_id = 0;
            tile->pending_upload = FALSE;
        }
    }

    return grid;
}

/**
 * Free a tile grid and all its tiles
 */
void tile_grid_free(TileGrid* grid) {
    gint x, y;

    if (!grid) {
        return;
    }

    /* Free all tile resources */
    if (grid->tiles) {
        for (y = 0; y < grid->tiles_y; y++) {
            if (grid->tiles[y]) {
                for (x = 0; x < grid->tiles_x; x++) {
                    Tile* tile = &grid->tiles[y][x];

                    /* Free Cairo surface */
                    if (tile->surface) {
                        cairo_surface_t* surface = tile->surface;
                        tile->surface = NULL;
                        cairo_surface_destroy(surface);
                    }

                    /* Free pixel buffer (allocated by worker pool or init) */
                    if (tile->pixel_buffer) {
                        g_free(tile->pixel_buffer);
                        tile->pixel_buffer = NULL;
                    }
                }
                g_free(grid->tiles[y]);
                grid->tiles[y] = NULL; /* Set to NULL after freeing */
            }
        }
        g_free(grid->tiles);
        grid->tiles = NULL; /* Set to NULL after freeing */
    }

    g_free(grid);
}

/**
 * Mark a specific tile as dirty
 */
void tile_mark_dirty(Tile* tile) {
    if (tile) {
        tile->dirty = TRUE;
    }
}

/**
 * Mark all tiles intersecting a rectangle as dirty
 */
void tile_grid_mark_rect_dirty(TileGrid* grid, gint x, gint y, gint w, gint h) {
    gint start_tile_x, start_tile_y;
    gint end_tile_x, end_tile_y;
    gint tx, ty;

    if (!grid || w <= 0 || h <= 0) {
        return;
    }

    /* Calculate which tiles intersect the rectangle */
    start_tile_x = x / grid->tile_size;
    start_tile_y = y / grid->tile_size;
    end_tile_x = (x + w - 1) / grid->tile_size;
    end_tile_y = (y + h - 1) / grid->tile_size;

    /* Clamp to grid bounds */
    if (start_tile_x < 0)
        start_tile_x = 0;
    if (start_tile_y < 0)
        start_tile_y = 0;
    if (end_tile_x >= grid->tiles_x)
        end_tile_x = grid->tiles_x - 1;
    if (end_tile_y >= grid->tiles_y)
        end_tile_y = grid->tiles_y - 1;

    /* Mark all intersecting tiles as dirty */
    for (ty = start_tile_y; ty <= end_tile_y; ty++) {
        for (tx = start_tile_x; tx <= end_tile_x; tx++) {
            tile_mark_dirty(&grid->tiles[ty][tx]);
        }
    }
}

/**
 * Get a tile at specific grid coordinates
 */
Tile* tile_grid_get_tile(TileGrid* grid, gint tile_x, gint tile_y) {
    if (!grid || !grid->tiles) {
        return NULL;
    }

    if (tile_x < 0 || tile_x >= grid->tiles_x ||
        tile_y < 0 || tile_y >= grid->tiles_y) {
        return NULL;
    }

    return &grid->tiles[tile_y][tile_x];
}

/**
 * Get tile grid coordinates from document pixel coordinates
 */
void tile_grid_pixel_to_tile(TileGrid* grid, gint px, gint py, gint* tile_x, gint* tile_y) {
    if (!grid || !tile_x || !tile_y) {
        return;
    }

    *tile_x = px / grid->tile_size;
    *tile_y = py / grid->tile_size;
}

/**
 * Composite a single tile by rendering all layers that intersect it
 * This replaces the old full-surface compositing for this tile region
 */
static gboolean tile_composite(ImageDocument* doc, Tile* tile) {
    cairo_t* cr;
    GList* iter;
    ImageLayer* layer;
    gint layer_x, layer_y, layer_right, layer_bottom;
    gint tile_right, tile_bottom;
    gint intersect_left, intersect_top, intersect_right, intersect_bottom;
    gboolean is_first_visible_layer = TRUE;

    if (!doc || !tile || !tile->surface) {
        return FALSE;
    }

    /* Clear tile to transparent */
    cr = cairo_create(tile->surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    tile_right = tile->px + tile->w;
    tile_bottom = tile->py + tile->h;

    /* Safety check: if document is being freed (layers is NULL), don't composite */
    if (!doc->layers) {
        cairo_destroy(cr);
        return FALSE;
    }

    /* Composite each visible layer that intersects this tile */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;

        if (!layer || !layer->visible || layer->opacity <= 0.0 || !layer->surface) {
            continue;
        }

        /* Calculate layer bounds in document coordinates */
        layer_x = layer->offset_x;
        layer_y = layer->offset_y;
        layer_right = layer_x + layer->width;
        layer_bottom = layer_y + layer->height;

        /* Check if layer intersects tile */
        intersect_left = (layer_x > tile->px) ? layer_x : tile->px;
        intersect_top = (layer_y > tile->py) ? layer_y : tile->py;
        intersect_right = (layer_right < tile_right) ? layer_right : tile_right;
        intersect_bottom = (layer_bottom < tile_bottom) ? layer_bottom : tile_bottom;

        if (intersect_left >= intersect_right || intersect_top >= intersect_bottom) {
            continue; /* No intersection */
        }

        /* Calculate intersection in tile-local coordinates */
        gint tile_local_x = intersect_left - tile->px;
        gint tile_local_y = intersect_top - tile->py;
        gint tile_local_w = intersect_right - intersect_left;
        gint tile_local_h = intersect_bottom - intersect_top;

        /* Clip to tile bounds */
        cairo_save(cr);
        cairo_rectangle(cr, tile_local_x, tile_local_y, tile_local_w, tile_local_h);
        cairo_clip(cr);

        /* Set operator based on layer's blend mode
           First visible layer always uses OVER to establish the base */
        cairo_operator_t op;
        if (is_first_visible_layer) {
            op = CAIRO_OPERATOR_OVER;
            is_first_visible_layer = FALSE;
        } else {
            op = blend_mode_to_cairo_operator(layer->blend_mode);
        }
        cairo_set_operator(cr, op);

        /* OPTIMIZATION: For large layers with dirty cache, use source directly
           with opacity applied on-the-fly instead of regenerating entire cache.
           This is much faster for frequent updates during drawing. */
        guint layer_area = layer->width * layer->height;
        const guint LARGE_LAYER_THRESHOLD = 1500 * 1500; /* ~2.25 million pixels */

        if (layer->cache_dirty && layer_area > LARGE_LAYER_THRESHOLD) {
            /* Use source surface directly with opacity - no cache needed for this render */
            /* Translate to position layer correctly relative to tile */
            cairo_set_source_surface(cr, layer->surface,
                                     layer_x - tile->px,
                                     layer_y - tile->py);
            /* Set nearest filter to prevent edge artifacts at tile boundaries */
            // cairo_pattern_t* pattern = cairo_get_source(cr);
            // cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
            if (layer->opacity < 1.0) {
                cairo_paint_with_alpha(cr, layer->opacity);
            } else {
                cairo_paint(cr);
            }
        } else {
            /* Use cached surface (either valid or small enough to regenerate quickly) */
            if (!layer_ensure_cache(layer)) {
                cairo_restore(cr);
                continue;
            }
            /* Translate to position cached layer correctly relative to tile */
            cairo_set_source_surface(cr, layer->cache_surface,
                                     layer_x - tile->px,
                                     layer_y - tile->py);
            /* Set nearest filter to prevent edge artifacts at tile boundaries */
            // cairo_pattern_t* pattern = cairo_get_source(cr);
            // cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
        }

        cairo_restore(cr);
    }

    /* Finish all Cairo operations and flush the surface */
    cairo_surface_flush(tile->surface);
    cairo_destroy(cr);
    tile->dirty = FALSE;

    return TRUE;
}

/**
 * Composite all dirty tiles in the grid
 * This replaces document_render_composite() - instead of rendering the entire
 * document surface, we only render tiles that are marked dirty.
 */
gboolean tile_grid_composite(ImageDocument* doc, TileGrid* grid) {
    gint x, y;
    Tile* tile;
    gint dirty_count = 0;

    if (!doc || !grid) {
        return FALSE;
    }

    /* Composite each dirty tile */
    for (y = 0; y < grid->tiles_y; y++) {
        for (x = 0; x < grid->tiles_x; x++) {
            tile = &grid->tiles[y][x];

            if (tile->dirty && tile->surface) {
                if (tile_composite(doc, tile)) {
                    dirty_count++;
                } else {
                    g_warning("Failed to composite tile at (%d, %d)", x, y);
                }
            }
        }
    }

    return TRUE;
}

/**
 * Mark a tile as needing recomposition via thread pool
 * Increments generation_id to invalidate any pending work
 */
void tile_mark_dirty_for_thread_pool(Tile* tile) {
    if (!tile) {
        return;
    }

    tile->dirty = TRUE;
    tile->generation_id++;    /* Invalidate any pending work */
    tile->state = TILE_CLEAN; /* Reset state for new work */
}

/**
 * Apply completed tile result from worker thread to main tile cache
 * Should only be called from GTK main thread
 * Returns TRUE if result was applied, FALSE if stale
 */
gboolean tile_apply_completed_result(Tile* tile, cairo_surface_t* new_surface, guint generation_id) {
    if (!tile || !new_surface) {
        return FALSE;
    }

    /* Check if this result is stale (newer work has been queued) */
    if (generation_id != tile->generation_id) {
        return FALSE; /* Stale result, discard */
    }

    /* Swap surfaces */
    if (tile->surface) {
        cairo_surface_destroy(tile->surface);
    }

    tile->surface = new_surface;
    tile->dirty = FALSE;
    tile->state = TILE_CLEAN;

    return TRUE;
}
