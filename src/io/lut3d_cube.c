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
 * Adobe/ASC CDL .cube 3D LUT (ASCII) reader and writer.
 * Data order: for blue 0..n-1, green 0..n-1, red 0..n-1 (red varies fastest), each
 * line is the output R G B for the corresponding input point.
 */

#include "io/lut3d_cube.h"
#include "debug_logger.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CUBE_LINE_MAX 4096

/* Collapse newlines to spaces for a single # comment line. Caller g_free. */
static gchar* cube_comment_one_line(const char* t) {
    gchar* p;
    if (!t || t[0] == '\0') {
        return NULL;
    }
    p = g_strdup(t);
    for (gchar* c = p; *c; c++) {
        if (*c == '\n' || *c == '\r') {
            *c = ' ';
        }
    }
    return p;
}

static int parse_data_row(char* s, double* a, double* b, double* c) {
    g_strchomp(s);
    {
        char* h = strchr(s, '#');
        if (h)
            *h = '\0';
    }
    g_strstrip(s);
    if (s[0] == '\0')
        return 0;
    return (sscanf(s, "%lf %lf %lf", a, b, c) == 3) ? 1 : 0;
}

/* @return 1 if the line was a known header key */
static int parse_cube_header_line(char* s, int* n3, int* l1d_flag, int* l1d_n, int* have3d, double* dmin, double* dmax, int* have_dmin, int* have_dmax, char** title) {
    char key[32];
    g_strstrip(s);
    if (s[0] == '\0' || s[0] == '#')
        return 0;
    if (sscanf(s, "%31s", key) != 1)
        return 0;

    if (g_ascii_strcasecmp(key, "LUT_3D_SIZE") == 0) {
        int v;
        if (sscanf(s, "%*s %d", &v) == 1 && v >= LUT3D_SIZE_MIN && v <= LUT3D_SIZE_MAX) {
            *n3 = v;
            *have3d = 1;
            return 1;
        }
    }
    if (g_ascii_strcasecmp(key, "LUT_1D_SIZE") == 0) {
        int v;
        if (sscanf(s, "%*s %d", &v) == 1) {
            *l1d_n = v;
            *l1d_flag = 1;
            return 1;
        }
    }
    if (g_ascii_strcasecmp(key, "DOMAIN_MIN") == 0) {
        if (sscanf(s, "%*s %lf %lf %lf", dmin, dmin + 1, dmin + 2) == 3) {
            *have_dmin = 1;
            return 1;
        }
    }
    if (g_ascii_strcasecmp(key, "DOMAIN_MAX") == 0) {
        if (sscanf(s, "%*s %lf %lf %lf", dmax, dmax + 1, dmax + 2) == 3) {
            *have_dmax = 1;
            return 1;
        }
    }
    if (g_ascii_strcasecmp(key, "TITLE") == 0) {
        const char* p = s;
        while (*p && !g_ascii_isspace((unsigned char)*p))
            p++;
        while (*p && g_ascii_isspace((unsigned char)*p))
            p++;
        g_free(*title);
        *title = NULL;
        if (*p == '\0' || *p == '#')
            return 1;
        if (p[0] == '"') {
            p++;
            {
                char* end = strrchr(p, '"');
                if (end)
                    *end = '\0';
            }
            *title = g_strdup(p);
        } else
            *title = g_strdup(p);
        if (*title)
            g_strchomp(*title);
        return 1;
    }
    return 0;
}

static void map_err(Lut3dCubeError* o, Lut3dCubeError v) {
    if (o)
        *o = v;
}

ColorLut3D* lut3d_cube_load(const char* filename, Lut3dCubeError* error_out) {
    FILE* fp = g_fopen(filename, "r");
    char linebuf[CUBE_LINE_MAX];
    ColorLut3D* lut = NULL;
    int cube_n = 0;
    int l1d = 0, l1d_n = 0, have3d = 0;
    double dmin[3] = {0, 0, 0};
    double dmax[3] = {1, 1, 1};
    int have_dmin = 0, have_dmax = 0;
    char* title = NULL;
    int in_header = 1;
    int64_t idx = 0, n3expect = 0;
    double dr, dg, db;
    Lut3dCubeError err = LUT3D_CUBE_ERROR_NONE;

    map_err(error_out, LUT3D_CUBE_ERROR_NONE);
    if (!filename) {
        g_free(title);
        map_err(error_out, LUT3D_CUBE_ERROR_INVALID_PARAMETERS);
        return NULL;
    }
    if (!fp) {
        debug_log("WRN", "lut3d_cube_load: cannot open '%s'", filename);
        map_err(error_out, LUT3D_CUBE_ERROR_FILE_NOT_FOUND);
        g_free(title);
        return NULL;
    }

    while (fgets(linebuf, (int)sizeof(linebuf), fp)) {
        char* line = g_strdup(linebuf);
        g_strchomp(line);
        g_strstrip(line);
        if (in_header) {
            if (line[0] == '\0' || line[0] == '#') {
                g_free(line);
                continue;
            }
            if (parse_cube_header_line(line, &cube_n, &l1d, &l1d_n, &have3d, dmin, dmax, &have_dmin, &have_dmax, &title)) {
                g_free(line);
                continue;
            }
        } else {
            if (line[0] == '\0' || line[0] == '#') {
                g_free(line);
                continue;
            }
        }

        if (!parse_data_row(line, &dr, &dg, &db)) {
            err = LUT3D_CUBE_ERROR_CORRUPT_FILE;
            g_free(line);
            break;
        }

        if (in_header) {
            in_header = 0;
            if (l1d > 0 && have3d == 0) {
                err = LUT3D_CUBE_ERROR_UNSUPPORTED_FORMAT;
                g_free(line);
                break;
            }
            if (l1d > 0 && have3d > 0) {
                err = LUT3D_CUBE_ERROR_UNSUPPORTED_FORMAT;
                g_free(line);
                break;
            }
            if (!have3d || cube_n < LUT3D_SIZE_MIN) {
                err = LUT3D_CUBE_ERROR_CORRUPT_FILE;
                g_free(line);
                break;
            }
            n3expect = (int64_t)cube_n * cube_n * cube_n;
            lut = lut3d_new(cube_n);
            if (!lut) {
                err = LUT3D_CUBE_ERROR_OUT_OF_MEMORY;
                g_free(line);
                break;
            }
            lut->domain_min[0] = have_dmin ? dmin[0] : 0.0;
            lut->domain_min[1] = have_dmin ? dmin[1] : 0.0;
            lut->domain_min[2] = have_dmin ? dmin[2] : 0.0;
            lut->domain_max[0] = have_dmax ? dmax[0] : 1.0;
            lut->domain_max[1] = have_dmax ? dmax[1] : 1.0;
            lut->domain_max[2] = have_dmax ? dmax[2] : 1.0;
            if (title) {
                lut->title = g_strdup(title);
            }
        }

        if (lut && idx < n3expect) {
            lut->rgb[(size_t)idx * 3U + 0U] = (float)dr;
            lut->rgb[(size_t)idx * 3U + 1U] = (float)dg;
            lut->rgb[(size_t)idx * 3U + 2U] = (float)db;
            idx++;
        } else if (n3expect > 0) {
            err = LUT3D_CUBE_ERROR_CORRUPT_FILE;
        }
        g_free(line);
        if (err != LUT3D_CUBE_ERROR_NONE)
            break;
    }

    g_free(title);
    fclose(fp);

    if (err == LUT3D_CUBE_ERROR_NONE && in_header) {
        err = l1d > 0 && have3d == 0 ? LUT3D_CUBE_ERROR_UNSUPPORTED_FORMAT : LUT3D_CUBE_ERROR_CORRUPT_FILE;
    }
    if (err == LUT3D_CUBE_ERROR_NONE && lut) {
        if (idx != n3expect)
            err = LUT3D_CUBE_ERROR_CORRUPT_FILE;
    } else if (err == LUT3D_CUBE_ERROR_NONE && !lut)
        err = LUT3D_CUBE_ERROR_CORRUPT_FILE;

    if (err != LUT3D_CUBE_ERROR_NONE) {
        lut3d_free(lut);
        map_err(error_out, err);
        return NULL;
    }
    map_err(error_out, LUT3D_CUBE_ERROR_NONE);
    return lut;
}

gboolean lut3d_cube_save(const ColorLut3D* lut, const char* filename, Lut3dCubeError* error_out) {
    int n, total_cells, k;
    FILE* fp = NULL;

    map_err(error_out, LUT3D_CUBE_ERROR_NONE);
    if (!lut || !filename || !lut->rgb) {
        map_err(error_out, LUT3D_CUBE_ERROR_INVALID_PARAMETERS);
        return FALSE;
    }
    n = lut->size;
    if (n < LUT3D_SIZE_MIN || n > LUT3D_SIZE_MAX) {
        map_err(error_out, LUT3D_CUBE_ERROR_INVALID_PARAMETERS);
        return FALSE;
    }
    total_cells = n * n * n;
    fp = g_fopen(filename, "w");
    if (!fp) {
        debug_log("WRN", "lut3d_cube_save: cannot open '%s'", filename);
        map_err(error_out, LUT3D_CUBE_ERROR_FILE_WRITE_ERROR);
        return FALSE;
    }

    {
        const char* tline;
        if (lut->title) {
            tline = lut->title; /* may be "" for no visible title */
        } else {
            tline = "RasterLab 3D LUT";
        }
        if (fprintf(fp, "TITLE \"%s\"\n", tline) < 0) {
            goto wfail;
        }
    }
    if (lut->copyright && lut->copyright[0]) {
        gchar* cc = cube_comment_one_line(lut->copyright);
        if (cc) {
            if (fprintf(fp, "# Copyright: %s\n", cc) < 0) {
                g_free(cc);
                goto wfail;
            }
            g_free(cc);
        }
    }
    if (fprintf(fp, "DOMAIN_MIN %.9g %.9g %.9g\n", lut->domain_min[0], lut->domain_min[1], lut->domain_min[2]) < 0)
        goto wfail;
    if (fprintf(fp, "DOMAIN_MAX %.9g %.9g %.9g\n", lut->domain_max[0], lut->domain_max[1], lut->domain_max[2]) < 0)
        goto wfail;
    if (fprintf(fp, "LUT_3D_SIZE %d\n", n) < 0)
        goto wfail;

    for (k = 0; k < total_cells; k++) {
        float ar, ag, ab;
        ar = lut->rgb[(size_t)k * 3U + 0U];
        ag = lut->rgb[(size_t)k * 3U + 1U];
        ab = lut->rgb[(size_t)k * 3U + 2U];
        if (fprintf(fp, "%.9g %.9g %.9g\n", (double)ar, (double)ag, (double)ab) < 0)
            goto wfail;
    }
    fclose(fp);
    map_err(error_out, LUT3D_CUBE_ERROR_NONE);
    return TRUE;
wfail:
    fclose(fp);
    map_err(error_out, LUT3D_CUBE_ERROR_FILE_WRITE_ERROR);
    return FALSE;
}
