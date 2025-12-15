#ifndef MIPMAP_H
#define MIPMAP_H

#include "render/tile.h"
#include <cairo/cairo.h>
#include <glib.h>

/**
 * Maximum number of mipmap levels
 * This limits memory usage and generation time
 */
#define MAX_MIPMAP_LEVELS 8

/**
 * Mipmap level structure
 * Each level contains a tile grid at a specific resolution
 */
typedef struct {
    gint level;               /* Mipmap level (0 = full resolution, 1 = 50%, etc.) */
    gdouble scale_factor;     /* Scale factor for this level (1.0, 0.5, 0.25, etc.) */
    guint width;              /* Width at this mipmap level */
    guint height;             /* Height at this mipmap level */
    TileGrid* tile_grid;      /* Tile grid for this mipmap level */
    gboolean dirty;           /* Whether this mipmap level needs regeneration */
    cairo_surface_t* surface; /* Full surface for this level (optional, for non-tiled rendering) */
} MipmapLevel;

/**
 * Mipmap pyramid structure
 * Contains all mipmap levels for a layer
 */
struct MipmapPyramid {
    guint magic;         /* Magic number to detect corruption (0xDEADBEEF) */
    MipmapLevel* levels; /* Array of mipmap levels */
    guint num_levels;    /* Number of mipmap levels */
    guint base_width;    /* Base layer width */
    guint base_height;   /* Base layer height */
};

/**
 * Create a new mipmap pyramid for a layer
 * @param base_width Base layer width
 * @param base_height Base layer height
 * @param tile_size Tile size for mipmap grids
 * @return New mipmap pyramid, or NULL on error. Caller must call mipmap_pyramid_free().
 */
MipmapPyramid* mipmap_pyramid_create(guint base_width, guint base_height, gint tile_size);

/**
 * Free a mipmap pyramid and all its levels
 * @param pyramid Mipmap pyramid to free
 */
void mipmap_pyramid_free(MipmapPyramid* pyramid);

/**
 * Generate a specific mipmap level from the base layer surface
 * @param pyramid Mipmap pyramid
 * @param level Level to generate (0 = full resolution)
 * @param base_surface Base layer surface (full resolution)
 * @return TRUE on success, FALSE on error
 */
gboolean mipmap_generate_level(MipmapPyramid* pyramid, guint level, cairo_surface_t* base_surface);

/**
 * Ensure a mipmap level is generated and up to date
 * @param pyramid Mipmap pyramid
 * @param level Level to ensure
 * @param base_surface Base layer surface
 * @return TRUE on success, FALSE on error
 */
gboolean mipmap_ensure_level(MipmapPyramid* pyramid, guint level, cairo_surface_t* base_surface);

/**
 * Select the appropriate mipmap level for a given zoom factor
 * @param pyramid Mipmap pyramid
 * @param zoom_factor Zoom factor (1.0 = 100%, 0.5 = 50%, etc.)
 * @return Mipmap level index, or 0 if zoom >= 1.0
 */
guint mipmap_select_level(MipmapPyramid* pyramid, gdouble zoom_factor);

/**
 * Get a mipmap level by index
 * @param pyramid Mipmap pyramid
 * @param level Level index
 * @return Mipmap level, or NULL if invalid
 */
MipmapLevel* mipmap_get_level(MipmapPyramid* pyramid, guint level);

/**
 * Mark a mipmap level as dirty
 * @param pyramid Mipmap pyramid
 * @param level Level index (or -1 to mark all levels dirty)
 */
void mipmap_mark_dirty(MipmapPyramid* pyramid, gint level);

/**
 * Calculate the number of mipmap levels needed for given dimensions
 * @param width Base width
 * @param height Base height
 * @param tile_size Tile size
 * @return Number of mipmap levels needed
 */
guint mipmap_calculate_level_count(guint width, guint height, gint tile_size);

#endif /* MIPMAP_H */
