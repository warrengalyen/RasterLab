/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

/*
 * GIMP Gradient (.ggr) reader and writer.
 *
 * File layout (plain ASCII):
 *   Line 1:   "GIMP Gradient"
 *   Line 2:   "Name: <gradient name>"
 *   Line 3:   <N>   (integer segment count)
 *   Lines 4…N+3:  one segment per line, 13 or 15 whitespace-separated values:
 *     [0]  left_pos   float [0,1]
 *     [1]  midpoint   float [0,1]
 *     [2]  right_pos  float [0,1]
 *     [3]  left_r     float [0,1]
 *     [4]  left_g     float [0,1]
 *     [5]  left_b     float [0,1]
 *     [6]  left_a     float [0,1]
 *     [7]  right_r    float [0,1]
 *     [8]  right_g    float [0,1]
 *     [9]  right_b    float [0,1]
 *     [10] right_a    float [0,1]
 *     [11] blend_mode int  (0=linear,1=ease,2=sine,3=sphere_inc,4=sphere_dec,5=step)
 *     [12] color_space int (0=RGB,1=HSV_CCW,2=HSV_CW)
 *     [13] left_type  int  (0=fixed,1=fg,2=fg_transparent,3=bg,4=bg_transparent) [GIMP>=2.3.11]
 *     [14] right_type int  [GIMP>=2.3.11]
 *
 * Fields 13 and 14 were added in GIMP 2.3.11; older files have only 13 fields.
 * The parser accepts both; the writer always emits 15 fields.
 */

#include "io/gradient_ggr.h"
#include "debug_logger.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GGR_MAGIC     "GIMP Gradient"
#define GGR_NAME_PFX  "Name: "
#define GGR_LINE_MAX  1024

/* -------------------------------------------------------------------------
 * Loader
 * ---------------------------------------------------------------------- */

GradientSet* gradient_ggr_load(const char* filename, GradientGgrError* error_out) {
    FILE* fp       = NULL;
    GradientSet*  set = NULL;
    GradientDef*  def = NULL;
    char line[GGR_LINE_MAX];
    int  n_segs    = 0;
    int  seg_idx   = 0;
    char* name_buf = NULL;

#define SET_ERR(e) do { if (error_out) *error_out = (e); } while (0)
#define FAIL(e)    do { SET_ERR(e); goto cleanup; } while (0)

    SET_ERR(GRADIENT_GGR_ERROR_NONE);

    if (!filename) FAIL(GRADIENT_GGR_ERROR_INVALID_PARAMETERS);

    fp = g_fopen(filename, "r");
    if (!fp) {
        debug_log("WRN", "gradient_ggr_load: cannot open '%s'", filename);
        FAIL(GRADIENT_GGR_ERROR_FILE_NOT_FOUND);
    }

    /* Line 1: magic */
    if (!fgets(line, sizeof(line), fp)) FAIL(GRADIENT_GGR_ERROR_CORRUPT_FILE);
    /* Strip trailing newline */
    line[strcspn(line, "\r\n")] = '\0';
    if (strcmp(line, GGR_MAGIC) != 0) {
        debug_log("WRN", "gradient_ggr_load: bad magic in '%s'", filename);
        FAIL(GRADIENT_GGR_ERROR_CORRUPT_FILE);
    }

    /* Line 2: "Name: <name>" */
    if (!fgets(line, sizeof(line), fp)) FAIL(GRADIENT_GGR_ERROR_CORRUPT_FILE);
    line[strcspn(line, "\r\n")] = '\0';
    if (strncmp(line, GGR_NAME_PFX, strlen(GGR_NAME_PFX)) == 0) {
        name_buf = g_strdup(line + strlen(GGR_NAME_PFX));
    } else {
        /* Missing or malformed name prefix — use a default */
        name_buf = g_strdup("Unnamed Gradient");
    }

    /* Line 3: segment count */
    if (!fgets(line, sizeof(line), fp)) FAIL(GRADIENT_GGR_ERROR_CORRUPT_FILE);
    if (sscanf(line, "%d", &n_segs) != 1 || n_segs <= 0) {
        debug_log("WRN", "gradient_ggr_load: invalid segment count in '%s'", filename);
        FAIL(GRADIENT_GGR_ERROR_CORRUPT_FILE);
    }

    set = gradient_set_new(1);
    if (!set) FAIL(GRADIENT_GGR_ERROR_OUT_OF_MEMORY);

    def = &set->gradients[0];
    def->name     = name_buf; name_buf = NULL;
    def->segments = calloc((size_t)n_segs, sizeof(GradientSegment));
    if (!def->segments) FAIL(GRADIENT_GGR_ERROR_OUT_OF_MEMORY);
    def->num_segments = n_segs;

    /* Lines 4…N+3: segments */
    for (seg_idx = 0; seg_idx < n_segs; seg_idx++) {
        if (!fgets(line, sizeof(line), fp)) {
            debug_log("WRN", "gradient_ggr_load: unexpected EOF at segment %d in '%s'",
                      seg_idx, filename);
            FAIL(GRADIENT_GGR_ERROR_CORRUPT_FILE);
        }

        GradientSegment* s = &def->segments[seg_idx];
        int blend = 0, colorsp = 0, ltype = 0, rtype = 0;

        int n = sscanf(line,
                       "%lf %lf %lf "
                       "%lf %lf %lf %lf "
                       "%lf %lf %lf %lf "
                       "%d %d %d %d",
                       &s->left_pos, &s->midpoint, &s->right_pos,
                       &s->left_r, &s->left_g, &s->left_b, &s->left_a,
                       &s->right_r, &s->right_g, &s->right_b, &s->right_a,
                       &blend, &colorsp, &ltype, &rtype);

        if (n < 13) {
            debug_log("WRN", "gradient_ggr_load: malformed segment line %d in '%s'",
                      seg_idx + 1, filename);
            FAIL(GRADIENT_GGR_ERROR_CORRUPT_FILE);
        }

        s->blend_mode  = (GradientBlendMode)blend;
        s->color_space = (GradientColorSpace)colorsp;

        /* Fields 13 and 14 are optional (older GIMP files) */
        if (n >= 15) {
            s->left_type  = (GradientEndpointType)ltype;
            s->right_type = (GradientEndpointType)rtype;
        } else {
            s->left_type  = GRADIENT_ENDPOINT_FIXED;
            s->right_type = GRADIENT_ENDPOINT_FIXED;
        }
    }

    fclose(fp);
    debug_log("DBG", "gradient_ggr_load: loaded '%s' (%d segments)", filename, n_segs);
    return set;

cleanup:
    if (fp)      fclose(fp);
    if (name_buf) g_free(name_buf);
    if (set)     gradient_set_free(set);
    return NULL;

#undef SET_ERR
#undef FAIL
}

/* -------------------------------------------------------------------------
 * Writer
 * ---------------------------------------------------------------------- */

gboolean gradient_ggr_save(const GradientSet* set, const char* filename,
                             GradientGgrError* error_out) {
    FILE* fp = NULL;
    const GradientDef* def = NULL;

#define SET_ERR(e) do { if (error_out) *error_out = (e); } while (0)
#define FAIL(e)    do { SET_ERR(e); goto cleanup; } while (0)

    SET_ERR(GRADIENT_GGR_ERROR_NONE);

    if (!set || !filename) FAIL(GRADIENT_GGR_ERROR_INVALID_PARAMETERS);
    if (set->num_gradients == 0 || !set->gradients) FAIL(GRADIENT_GGR_ERROR_INVALID_PARAMETERS);

    def = &set->gradients[0];

    fp = g_fopen(filename, "w");
    if (!fp) {
        debug_log("WRN", "gradient_ggr_save: cannot open '%s' for writing", filename);
        FAIL(GRADIENT_GGR_ERROR_FILE_WRITE_ERROR);
    }

    /* Header */
    if (fprintf(fp, "%s\n", GGR_MAGIC) < 0) FAIL(GRADIENT_GGR_ERROR_FILE_WRITE_ERROR);
    if (fprintf(fp, "%s%s\n", GGR_NAME_PFX,
                def->name ? def->name : "Unnamed Gradient") < 0)
        FAIL(GRADIENT_GGR_ERROR_FILE_WRITE_ERROR);
    if (fprintf(fp, "%d\n", def->num_segments) < 0) FAIL(GRADIENT_GGR_ERROR_FILE_WRITE_ERROR);

    /* Segments — emit all 15 fields */
    for (int i = 0; i < def->num_segments; i++) {
        const GradientSegment* s = &def->segments[i];
        int written = fprintf(fp,
                              "%.10g %.10g %.10g "
                              "%.10g %.10g %.10g %.10g "
                              "%.10g %.10g %.10g %.10g "
                              "%d %d %d %d\n",
                              s->left_pos, s->midpoint, s->right_pos,
                              s->left_r, s->left_g, s->left_b, s->left_a,
                              s->right_r, s->right_g, s->right_b, s->right_a,
                              (int)s->blend_mode, (int)s->color_space,
                              (int)s->left_type, (int)s->right_type);
        if (written < 0) FAIL(GRADIENT_GGR_ERROR_FILE_WRITE_ERROR);
    }

    fclose(fp);
    debug_log("DBG", "gradient_ggr_save: saved '%s' (%d segments)", filename, def->num_segments);
    return TRUE;

cleanup:
    if (fp) fclose(fp);
    return FALSE;

#undef SET_ERR
#undef FAIL
}
