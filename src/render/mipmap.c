#include "render/mipmap.h"
#include "render/tile.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * Calculate the number of mipmap levels needed
 */
guint mipmap_calculate_level_count(guint width, guint height, gint tile_size) {
    guint levels = 1; /* Level 0 is always present */
    guint min_dim = (width < height) ? width : height;
    guint current_width = width;
    guint current_height = height;

    /* Generate levels until minimum dimension is less than tile size */
    while (min_dim > (guint)tile_size && levels < MAX_MIPMAP_LEVELS) {
        current_width = (current_width + 1) / 2; /* Round up */
        current_height = (current_height + 1) / 2;
        min_dim = (current_width < current_height) ? current_width : current_height;
        levels++;
    }

    return levels;
}

/**
 * Create a new mipmap pyramid
 */
MipmapPyramid* mipmap_pyramid_create(guint base_width, guint base_height, gint tile_size) {
    MipmapPyramid* pyramid;
    guint num_levels;
    guint i;
    guint current_width, current_height;
    gdouble scale_factor;

    if (base_width == 0 || base_height == 0 || tile_size <= 0) {
        return NULL;
    }

    pyramid = (MipmapPyramid*)g_malloc(sizeof(MipmapPyramid));
    pyramid->magic = 0xDEADBEEF; /* Magic number to detect corruption */
    pyramid->base_width = base_width;
    pyramid->base_height = base_height;

    /* Calculate number of levels needed */
    num_levels = mipmap_calculate_level_count(base_width, base_height, tile_size);
    /* Safety check: clamp num_levels to prevent buffer overrun */
    if (num_levels > MAX_MIPMAP_LEVELS) {
        num_levels = MAX_MIPMAP_LEVELS;
    }
    pyramid->num_levels = num_levels;

    /* Allocate levels array */
    pyramid->levels = (MipmapLevel*)g_malloc(sizeof(MipmapLevel) * num_levels);
    if (!pyramid->levels) {
        g_free(pyramid);
        return NULL;
    }

    /* Initialize each level */
    current_width = base_width;
    current_height = base_height;
    scale_factor = 1.0;

    for (i = 0; i < num_levels; i++) {
        MipmapLevel* level = &pyramid->levels[i];

        level->level = i;
        level->scale_factor = scale_factor;
        level->width = current_width;
        level->height = current_height;
        level->dirty = TRUE;
        level->surface = NULL;

        /* Create tile grid for this level */
        level->tile_grid = tile_grid_create(current_width, current_height, tile_size);

        if (!level->tile_grid) {
            /* Failed to create tile grid - free what we've created so far */
            for (guint j = 0; j < i; j++) {
                if (pyramid->levels[j].tile_grid) {
                    tile_grid_free(pyramid->levels[j].tile_grid);
                }
            }
            g_free(pyramid->levels);
            g_free(pyramid);
            return NULL;
        }

        /* Calculate next level dimensions */
        if (i < num_levels - 1) {
            current_width = (current_width + 1) / 2;
            current_height = (current_height + 1) / 2;
            scale_factor *= 0.5;
        }
    }

    return pyramid;
}

/**
 * Free a mipmap pyramid
 */
void mipmap_pyramid_free(MipmapPyramid* pyramid) {
    guint i;
    MipmapLevel* levels = NULL;
    guint num_levels = 0;
    guint magic = 0;

    if (!pyramid) {
        return;
    }

    /* Check magic number to detect if pyramid is corrupted or already freed
     * If magic is wrong, the pyramid structure is likely corrupted or invalid.
     * WARNING: If pyramid points to invalid memory, accessing pyramid->magic
     * will segfault. We can't check if a pointer is valid in C.
     *
     * If the pyramid structure is completely invalid, this access will crash.
     * There's no way to prevent this in C without platform-specific code. */
    magic = pyramid->magic;
    if (magic != 0xDEADBEEF) {
        /* Pyramid structure is corrupted or already freed - don't try to free it
         * This prevents segfaults when accessing corrupted memory.
         * Note: If pyramid was already freed, magic will be garbage or 0.
         *
         * IMPORTANT: If we get here, the pyramid was either:
         * 1. Already freed (magic = 0 or garbage)
         * 2. Never properly initialized (magic = garbage)
         * 3. Corrupted (magic = garbage)
         *
         * In all cases, we should NOT try to free it, as it will likely
         * cause heap corruption or segfaults. */
        return;
    }

    /* Save pyramid members to local variables IMMEDIATELY
     * This prevents issues if pyramid is freed or corrupted during iteration.
     * We access pyramid->levels and pyramid->num_levels only once here.
     *
     * WARNING: If pyramid structure is corrupted, accessing these fields
     * may segfault or corrupt the heap. */
    num_levels = pyramid->num_levels;
    levels = pyramid->levels;

    /* Clear magic number and levels pointer to mark pyramid as being freed
     * This must happen AFTER we've saved the values to local variables,
     * but BEFORE we start freeing resources, to prevent double-free if
     * this function is called again (shouldn't happen, but be safe).
     *
     * WARNING: If pyramid structure is corrupted, these writes may corrupt
     * the heap. But if we got past the magic check, the structure should
     * be at least partially valid. */
    pyramid->magic = 0;
    pyramid->levels = NULL;

    /* Safety check: ensure levels pointer is valid before accessing */
    if (levels) {
        /* Additional safety: check for obviously invalid values */
        if (num_levels == 0 || num_levels > MAX_MIPMAP_LEVELS) {
            /* Invalid num_levels - clamp to prevent buffer overrun */
            if (num_levels > MAX_MIPMAP_LEVELS) {
                num_levels = MAX_MIPMAP_LEVELS;
            } else {
                /* num_levels is 0 or invalid - just free the levels array
                 * Note: If levels points to invalid memory, g_free() may crash,
                 * but we have no way to check if it's valid */
                g_free(levels);
                g_free(pyramid);
                return;
            }
        }

        /* Iterate through levels and free resources
         * Note: We use the saved local variables, not pyramid->levels
         * WARNING: If levels points to invalid memory, accessing levels[i] will segfault.
         * We can't check if memory is valid in C, so we just have to hope it's valid. */
        for (i = 0; i < num_levels; i++) {
            /* Calculate level pointer - this may segfault if levels is invalid */
            MipmapLevel* level = &levels[i];

            /* Free surface if present - accessing level->surface may segfault if level is invalid */
            if (level->surface) {
                /* Save surface pointer and destroy directly
                 * Skip flush/finish to avoid crashes on invalid surfaces */
                cairo_surface_t* surface = level->surface;
                level->surface = NULL;
                cairo_surface_destroy(surface);
            }

            /* Free tile grid if present - save pointer first to prevent heap corruption
             * if level points to invalid memory. Accessing level->tile_grid may segfault. */
            if (level->tile_grid) {
                TileGrid* tile_grid = level->tile_grid;
                level->tile_grid = NULL; /* Clear pointer first to prevent double-free */
                tile_grid_free(tile_grid);
            }
        }

        /* Free levels array - this may crash if levels points to invalid memory */
        g_free(levels);
    }

    /* Free pyramid structure - this may crash if pyramid was already freed or corrupted */
    g_free(pyramid);
}

/**
 * Generate a specific mipmap level from the base surface
 * Note: base_surface should be the cached surface (with opacity applied) for best results
 */
gboolean mipmap_generate_level(MipmapPyramid* pyramid, guint level, cairo_surface_t* base_surface) {
    MipmapLevel* mip_level;
    cairo_surface_t* scaled_surface;
    cairo_t* cr;
    gint x, y;
    Tile* tile;

    if (!pyramid || !pyramid->levels || !base_surface) {
        return FALSE;
    }

    /* Safety check: ensure level is within valid range */
    if (level >= pyramid->num_levels || pyramid->num_levels > MAX_MIPMAP_LEVELS) {
        return FALSE;
    }

    mip_level = &pyramid->levels[level];

    if (level == 0) {
        /* Level 0 is the base - just mark as clean */
        mip_level->dirty = FALSE;
        return TRUE;
    }

    /* Generate scaled surface for this level */
    scaled_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                mip_level->width,
                                                mip_level->height);

    if (cairo_surface_status(scaled_surface) != CAIRO_STATUS_SUCCESS) {
        return FALSE;
    }

    cr = cairo_create(scaled_surface);

    /* Set up scaling */
    gdouble scale_x = (gdouble)mip_level->width / (gdouble)pyramid->base_width;
    gdouble scale_y = (gdouble)mip_level->height / (gdouble)pyramid->base_height;

    cairo_scale(cr, scale_x, scale_y);

    /* Set filter for smooth downscaling */
    cairo_pattern_t* pattern = cairo_pattern_create_for_surface(base_surface);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_BILINEAR);
    cairo_set_source(cr, pattern);
    cairo_paint(cr);
    cairo_pattern_destroy(pattern);

    cairo_destroy(cr);
    cairo_surface_flush(scaled_surface);

    /* Copy scaled surface to tiles */
    if (mip_level->tile_grid) {
        guchar* scaled_data = cairo_image_surface_get_data(scaled_surface);
        gint scaled_stride = cairo_image_surface_get_stride(scaled_surface);

        for (y = 0; y < mip_level->tile_grid->tiles_y; y++) {
            for (x = 0; x < mip_level->tile_grid->tiles_x; x++) {
                tile = tile_grid_get_tile(mip_level->tile_grid, x, y);

                if (tile && tile->surface) {
                    guchar* tile_data = cairo_image_surface_get_data(tile->surface);
                    gint tile_stride = cairo_image_surface_get_stride(tile->surface);
                    gint src_y, dst_y;

                    /* Copy pixels from scaled surface to tile */
                    for (src_y = tile->py, dst_y = 0; src_y < tile->py + tile->h && src_y < mip_level->height; src_y++, dst_y++) {
                        gint src_x, dst_x;
                        for (src_x = tile->px, dst_x = 0; src_x < tile->px + tile->w && src_x < mip_level->width; src_x++, dst_x++) {
                            guchar* src_pixel = scaled_data + src_y * scaled_stride + src_x * 4;
                            guchar* dst_pixel = tile_data + dst_y * tile_stride + dst_x * 4;

                            /* Copy 4 bytes (ARGB32) */
                            dst_pixel[0] = src_pixel[0];
                            dst_pixel[1] = src_pixel[1];
                            dst_pixel[2] = src_pixel[2];
                            dst_pixel[3] = src_pixel[3];
                        }
                    }

                    cairo_surface_mark_dirty(tile->surface);
                    tile->dirty = FALSE;
                }
            }
        }
    }

    /* Store scaled surface for potential reuse
     * Important: Destroy old surface only after ensuring all Cairo operations are complete
     * and the surface is no longer referenced by any Cairo context or pattern */
    if (mip_level->surface) {
        /* Flush the old surface to ensure all operations are complete */
        cairo_surface_flush(mip_level->surface);
        /* Finish the surface to ensure no pending operations */
        cairo_surface_finish(mip_level->surface);
        cairo_surface_destroy(mip_level->surface);
        mip_level->surface = NULL;
    }
    mip_level->surface = scaled_surface;
    mip_level->dirty = FALSE;

    return TRUE;
}

/**
 * Ensure a mipmap level is generated and up to date
 */
gboolean mipmap_ensure_level(MipmapPyramid* pyramid, guint level, cairo_surface_t* base_surface) {
    MipmapLevel* mip_level;

    if (!pyramid || !pyramid->levels || !base_surface) {
        return FALSE;
    }

    /* Safety check: ensure level is within valid range */
    if (level >= pyramid->num_levels || pyramid->num_levels > MAX_MIPMAP_LEVELS) {
        return FALSE;
    }

    mip_level = &pyramid->levels[level];

    /* If level is dirty, regenerate it */
    if (mip_level->dirty) {
        /* For levels > 0, we need to generate from base */
        if (level == 0) {
            mip_level->dirty = FALSE;
            return TRUE;
        } else {
            return mipmap_generate_level(pyramid, level, base_surface);
        }
    }

    return TRUE;
}

/**
 * Select the appropriate mipmap level for a given zoom factor
 */
guint mipmap_select_level(MipmapPyramid* pyramid, gdouble zoom_factor) {
    guint i;

    if (!pyramid || !pyramid->levels || zoom_factor >= 1.0) {
        return 0; /* Use full resolution for zoom >= 100% */
    }

    /* Safety check: ensure num_levels is valid */
    guint num_levels = pyramid->num_levels;
    if (num_levels == 0 || num_levels > MAX_MIPMAP_LEVELS) {
        return 0; /* Invalid pyramid, use level 0 */
    }

    /* Find the highest level where scale_factor >= zoom_factor */
    for (i = 0; i < num_levels; i++) {
        if (pyramid->levels[i].scale_factor >= zoom_factor) {
            return i;
        }
    }

    /* If zoom is very small, use the lowest level */
    return num_levels - 1;
}

/**
 * Get a mipmap level by index
 */
MipmapLevel* mipmap_get_level(MipmapPyramid* pyramid, guint level) {
    if (!pyramid || !pyramid->levels) {
        return NULL;
    }

    /* Safety check: ensure level is within valid range */
    if (level >= pyramid->num_levels || pyramid->num_levels > MAX_MIPMAP_LEVELS) {
        return NULL;
    }

    return &pyramid->levels[level];
}

/**
 * Mark a mipmap level as dirty
 */
void mipmap_mark_dirty(MipmapPyramid* pyramid, gint level) {
    guint i;

    if (!pyramid || !pyramid->levels) {
        return;
    }

    /* Safety check: ensure num_levels is valid */
    guint num_levels = pyramid->num_levels;
    if (num_levels == 0 || num_levels > MAX_MIPMAP_LEVELS) {
        return; /* Invalid pyramid */
    }

    if (level < 0) {
        /* Mark all levels dirty */
        for (i = 0; i < num_levels; i++) {
            pyramid->levels[i].dirty = TRUE;
        }
    } else if ((guint)level < num_levels) {
        /* Mark specific level and all lower levels dirty */
        for (i = (guint)level; i < num_levels; i++) {
            pyramid->levels[i].dirty = TRUE;
        }
    }
}
