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
 * SVG gradient interchange: linearGradient + stop elements (SVG 1.1 / 2).
 *
 * Load: walks the tree for svg:linearGradient, resolves xlink:href when there
 * are no local stops, sorts stops, builds contiguous GRADIENT_BLEND_LINEAR
 * RGB segments. radialGradient is ignored.
 *
 * Save: writes a minimal SVG document; emits one <stop> per segment endpoint
 * (merged at shared positions), using gradient_evaluate() so alpha matches the
 * editor (including GRD transparency overlays).
 */

#include "io/gradient_svg.h"
#include "debug_logger.h"
#include <glib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlwriter.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SVG_NS BAD_CAST "http://www.w3.org/2000/svg"
#define XLINK_NS BAD_CAST "http://www.w3.org/1999/xlink"
#define INKSCAPE_NS BAD_CAST "http://www.inkscape.org/namespaces/inkscape"

#define SVG_HREF_MAX_DEPTH 32
#define SVG_POS_EPS 1e-9

typedef struct {
    double pos;
    double r, g, b, a;
} SvgStop;

static double clamp01(double v) {
    if (v < 0.0)
        return 0.0;
    if (v > 1.0)
        return 1.0;
    return v;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

static gboolean parse_hex_color(const char* s, double* r, double* g, double* b) {
    int len = (int)strlen(s);
    int hi[6], n = 0;
    for (int i = 0; i < len && n < 8; i++) {
        int v = hex_val(s[i]);
        if (v < 0)
            continue;
        hi[n++] = v;
    }
    if (n == 3) {
        *r = (hi[0] * 16 + hi[0]) / 255.0;
        *g = (hi[1] * 16 + hi[1]) / 255.0;
        *b = (hi[2] * 16 + hi[2]) / 255.0;
        return TRUE;
    }
    if (n >= 6) {
        *r = (hi[0] * 16 + hi[1]) / 255.0;
        *g = (hi[2] * 16 + hi[3]) / 255.0;
        *b = (hi[4] * 16 + hi[5]) / 255.0;
        return TRUE;
    }
    return FALSE;
}

static gboolean parse_rgb_function(const char* s, double* r, double* g, double* b) {
    const char* p = s;
    while (*p && *p != '(')
        p++;
    if (*p != '(')
        return FALSE;
    p++;
    int rv, gv, bv;
    if (sscanf(p, " %d , %d , %d", &rv, &gv, &bv) == 3 ||
        sscanf(p, " %d%*[, ]%d%*[, ]%d", &rv, &gv, &bv) == 3) {
        *r = clamp01(rv / 255.0);
        *g = clamp01(gv / 255.0);
        *b = clamp01(bv / 255.0);
        return TRUE;
    }
    double rf, gf, bf;
    if (sscanf(p, " %lf%% , %lf%% , %lf%%", &rf, &gf, &bf) == 3) {
        *r = clamp01(rf / 100.0);
        *g = clamp01(gf / 100.0);
        *b = clamp01(bf / 100.0);
        return TRUE;
    }
    return FALSE;
}

static gboolean parse_color_string(const char* s, double* r, double* g, double* b) {
    if (!s)
        return FALSE;
    while (*s == ' ' || *s == '\t')
        s++;
    if (!*s)
        return FALSE;
    if (*s == '#')
        return parse_hex_color(s + 1, r, g, b);
    if (g_ascii_strncasecmp(s, "rgb(", 4) == 0)
        return parse_rgb_function(s, r, g, b);
    if (g_ascii_strcasecmp(s, "currentColor") == 0) {
        *r = *g = *b = 0.0;
        return TRUE;
    }
    if (g_ascii_strcasecmp(s, "black") == 0) {
        *r = *g = *b = 0.0;
        return TRUE;
    }
    if (g_ascii_strcasecmp(s, "white") == 0) {
        *r = *g = *b = 1.0;
        return TRUE;
    }
    if (g_ascii_strcasecmp(s, "red") == 0) {
        *r = 1.0;
        *g = *b = 0.0;
        return TRUE;
    }
    if (g_ascii_strcasecmp(s, "lime") == 0) {
        *g = 1.0;
        *r = *b = 0.0;
        return TRUE;
    }
    if (g_ascii_strcasecmp(s, "blue") == 0) {
        *b = 1.0;
        *r = *g = 0.0;
        return TRUE;
    }
    return FALSE;
}

static void apply_style_props(const char* style, double* r, double* g, double* b, double* a) {
    if (!style)
        return;
    const char* p = style;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ';')
            p++;
        if (!*p)
            break;
        if (g_ascii_strncasecmp(p, "stop-color:", 11) == 0) {
            p += 11;
            while (*p == ' ')
                p++;
            const char* semi = strchr(p, ';');
            size_t len = semi ? (size_t)(semi - p) : strlen(p);
            while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t'))
                len--;
            if (len > 0 && len < 256) {
                char buf[256];
                memcpy(buf, p, len);
                buf[len] = '\0';
                parse_color_string(buf, r, g, b);
            }
            p = semi ? semi + 1 : p + strlen(p);
        } else if (g_ascii_strncasecmp(p, "stop-opacity:", 13) == 0) {
            p += 13;
            while (*p == ' ')
                p++;
            *a = clamp01(g_ascii_strtod(p, NULL));
            const char* semi = strchr(p, ';');
            p = semi ? semi + 1 : p + strlen(p);
        } else {
            const char* semi = strchr(p, ';');
            p = semi ? semi + 1 : p + strlen(p);
        }
    }
}

static double parse_offset_attr(const xmlChar* xs) {
    if (!xs)
        return 0.0;
    const char* s = (const char*)xs;
    char* end = NULL;
    double v = g_ascii_strtod(s, &end);
    if (end && *end == '%')
        v /= 100.0;
    return clamp01(v);
}

static xmlNode* find_element_by_id(xmlNode* node, const char* id) {
    if (!node || !id)
        return NULL;
    if (node->type == XML_ELEMENT_NODE) {
        xmlChar* idv = xmlGetProp(node, BAD_CAST "id");
        if (idv) {
            gboolean match = (xmlStrcmp(idv, BAD_CAST id) == 0);
            xmlFree(idv);
            if (match)
                return node;
        }
    }
    for (xmlNode* c = node->children; c; c = c->next) {
        xmlNode* f = find_element_by_id(c, id);
        if (f)
            return f;
    }
    return NULL;
}

static gboolean is_svg_linear_gradient(xmlNode* node) {
    if (!node || node->type != XML_ELEMENT_NODE || !node->name)
        return FALSE;
    if (xmlStrcmp(node->name, BAD_CAST "linearGradient") != 0)
        return FALSE;
    if (node->ns && node->ns->href && xmlStrcmp(node->ns->href, SVG_NS) != 0)
        return FALSE;
    return TRUE;
}

static gboolean is_stop_element(xmlNode* node) {
    if (!node || node->type != XML_ELEMENT_NODE || !node->name)
        return FALSE;
    if (xmlStrcmp(node->name, BAD_CAST "stop") != 0)
        return FALSE;
    if (node->ns && node->ns->href && xmlStrcmp(node->ns->href, SVG_NS) != 0)
        return FALSE;
    return TRUE;
}

static int cmp_stop_pos(const void* a, const void* b) {
    const SvgStop* sa = (const SvgStop*)a;
    const SvgStop* sb = (const SvgStop*)b;
    if (sa->pos < sb->pos)
        return -1;
    if (sa->pos > sb->pos)
        return 1;
    return 0;
}

/* Ensure [0,1] coverage like SVG pad spread (implicit stops at 0 and 1). */
static gboolean svg_stops_pad_01(SvgStop** pstops, int* pn) {
    SvgStop* s = *pstops;
    int n = *pn;
    if (!s || n <= 0)
        return FALSE;

    int pre = (s[0].pos > 1e-9) ? 1 : 0;
    int post = (s[n - 1].pos < 1.0 - 1e-9) ? 1 : 0;
    if (pre == 0 && post == 0)
        return TRUE;

    SvgStop* ns = g_malloc((size_t)(n + pre + post) * sizeof(SvgStop));
    if (!ns)
        return FALSE;

    int w = 0;
    if (pre) {
        ns[w] = s[0];
        ns[w].pos = 0.0;
        w++;
    }
    memcpy(ns + w, s, (size_t)n * sizeof(SvgStop));
    w += n;
    if (post) {
        ns[w] = s[n - 1];
        ns[w].pos = 1.0;
        w++;
    }
    g_free(s);
    *pstops = ns;
    *pn = w;
    qsort(*pstops, (size_t)*pn, sizeof(SvgStop), cmp_stop_pos);
    return TRUE;
}

static gboolean collect_stops_from_element(xmlDoc* doc, xmlNode* grad, SvgStop** out_stops,
                                           int* out_count, int href_depth) {
    if (!doc || !grad || !out_stops || !out_count)
        return FALSE;
    *out_stops = NULL;
    *out_count = 0;

    if (href_depth > SVG_HREF_MAX_DEPTH)
        return FALSE;

    GArray* arr = g_array_new(FALSE, FALSE, sizeof(SvgStop));

    for (xmlNode* c = grad->children; c; c = c->next) {
        if (!is_stop_element(c))
            continue;

        double r = 0, g = 0, b = 0, a = 1.0;

        xmlChar* sc = xmlGetProp(c, BAD_CAST "stop-color");
        if (sc) {
            parse_color_string((const char*)sc, &r, &g, &b);
            xmlFree(sc);
        }
        xmlChar* so = xmlGetProp(c, BAD_CAST "stop-opacity");
        if (so) {
            a = clamp01(g_ascii_strtod((const char*)so, NULL));
            xmlFree(so);
        }
        xmlChar* st = xmlGetProp(c, BAD_CAST "style");
        if (st) {
            apply_style_props((const char*)st, &r, &g, &b, &a);
            xmlFree(st);
        }

        xmlChar* off = xmlGetProp(c, BAD_CAST "offset");
        double pos = parse_offset_attr(off);
        if (off)
            xmlFree(off);

        SvgStop s;
        s.pos = pos;
        s.r = clamp01(r);
        s.g = clamp01(g);
        s.b = clamp01(b);
        s.a = clamp01(a);
        g_array_append_val(arr, s);
    }

    if (arr->len == 0) {
        xmlChar* href = xmlGetProp(grad, BAD_CAST "href");
        if (!href)
            href = xmlGetNsProp(grad, BAD_CAST "href", XLINK_NS);
        if (href) {
            const char* h = (const char*)href;
            if (*h == '#')
                h++;
            xmlNode* root = xmlDocGetRootElement(doc);
            xmlNode* ref = root ? find_element_by_id(root, h) : NULL;
            xmlFree(href);
            if (ref && is_svg_linear_gradient(ref)) {
                gboolean ok = collect_stops_from_element(doc, ref, out_stops, out_count, href_depth + 1);
                g_array_free(arr, TRUE);
                return ok;
            }
        }
        g_array_free(arr, TRUE);
        *out_stops = NULL;
        *out_count = 0;
        return FALSE;
    }

    g_array_sort(arr, cmp_stop_pos);
    int n = (int)arr->len;
    SvgStop* stops = g_malloc((size_t)n * sizeof(SvgStop));
    if (!stops) {
        g_array_free(arr, TRUE);
        return FALSE;
    }
    memcpy(stops, arr->data, (size_t)n * sizeof(SvgStop));
    g_array_free(arr, TRUE);
    *out_stops = stops;
    *out_count = n;
    return TRUE;
}

static gboolean stops_to_gradient_def(SvgStop* stops, int n, GradientDef* def) {
    if (!stops || n <= 0 || !def)
        return FALSE;

    int nseg = (n <= 1) ? 1 : (n - 1);
    def->segments = calloc((size_t)nseg, sizeof(GradientSegment));
    if (!def->segments)
        return FALSE;
    def->num_segments = nseg;

    if (n == 1) {
        GradientSegment* s = &def->segments[0];
        s->left_pos = 0.0;
        s->right_pos = 1.0;
        s->midpoint = 0.5;
        s->left_r = s->right_r = stops[0].r;
        s->left_g = s->right_g = stops[0].g;
        s->left_b = s->right_b = stops[0].b;
        s->left_a = s->right_a = stops[0].a;
        s->blend_mode = GRADIENT_BLEND_LINEAR;
        s->color_space = GRADIENT_COLOR_RGB;
        s->left_type = s->right_type = GRADIENT_ENDPOINT_FIXED;
        return TRUE;
    }

    for (int i = 0; i < n - 1; i++) {
        GradientSegment* s = &def->segments[i];
        s->left_pos = stops[i].pos;
        s->right_pos = stops[i + 1].pos;
        double span = s->right_pos - s->left_pos;
        s->midpoint = s->left_pos + 0.5 * span;
        s->left_r = stops[i].r;
        s->left_g = stops[i].g;
        s->left_b = stops[i].b;
        s->left_a = stops[i].a;
        s->right_r = stops[i + 1].r;
        s->right_g = stops[i + 1].g;
        s->right_b = stops[i + 1].b;
        s->right_a = stops[i + 1].a;
        s->blend_mode = GRADIENT_BLEND_LINEAR;
        s->color_space = GRADIENT_COLOR_RGB;
        s->left_type = s->right_type = GRADIENT_ENDPOINT_FIXED;
        if (span <= 1e-12) {
            s->right_pos = s->left_pos + 1e-6;
            if (s->right_pos > 1.0) {
                s->right_pos = 1.0;
                s->left_pos = fmax(0.0, s->right_pos - 1e-6);
            }
        }
    }
    return TRUE;
}

static void collect_linear_gradients(xmlNode* node, GPtrArray* list) {
    if (!node)
        return;
    if (is_svg_linear_gradient(node)) {
        g_ptr_array_add(list, node);
    }
    for (xmlNode* c = node->children; c; c = c->next) {
        collect_linear_gradients(c, list);
    }
}

static char* gradient_name_from_node(xmlNode* node, int index) {
    xmlChar* ink = xmlGetNsProp(node, BAD_CAST "label", INKSCAPE_NS);
    if (ink && ink[0]) {
        char* n = g_strdup((const char*)ink);
        xmlFree(ink);
        return n;
    }
    if (ink)
        xmlFree(ink);

    xmlChar* idv = xmlGetProp(node, BAD_CAST "id");
    if (idv && idv[0]) {
        char* n = g_strdup((const char*)idv);
        xmlFree(idv);
        return n;
    }
    if (idv)
        xmlFree(idv);

    return g_strdup_printf("gradient_%d", index);
}

GradientSet* gradient_svg_load(const char* filename, GradientSvgError* error_out) {
#define SET_ERR(e)            \
    do {                      \
        if (error_out)        \
            *error_out = (e); \
    } while (0)
#define FAIL(e)       \
    do {              \
        SET_ERR(e);   \
        goto cleanup; \
    } while (0)

    SET_ERR(GRADIENT_SVG_ERROR_NONE);
    if (!filename)
        FAIL(GRADIENT_SVG_ERROR_INVALID_PARAMETERS);

    xmlDoc* doc = xmlReadFile(filename, NULL, XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
    if (!doc) {
        if (!g_file_test(filename, G_FILE_TEST_EXISTS))
            FAIL(GRADIENT_SVG_ERROR_FILE_NOT_FOUND);
        debug_log("WRN", "gradient_svg_load: xmlReadFile failed for '%s'", filename);
        FAIL(GRADIENT_SVG_ERROR_FILE_READ_ERROR);
    }

    xmlNode* root = xmlDocGetRootElement(doc);
    if (!root)
        FAIL(GRADIENT_SVG_ERROR_CORRUPT_FILE);

    GPtrArray* list = g_ptr_array_new();
    collect_linear_gradients(root, list);

    if (list->len == 0) {
        debug_log("WRN", "gradient_svg_load: no linearGradient in '%s'", filename);
        g_ptr_array_free(list, TRUE);
        xmlFreeDoc(doc);
        FAIL(GRADIENT_SVG_ERROR_CORRUPT_FILE);
    }

    GradientSet* set = gradient_set_new((int)list->len);
    if (!set) {
        g_ptr_array_free(list, TRUE);
        xmlFreeDoc(doc);
        FAIL(GRADIENT_SVG_ERROR_OUT_OF_MEMORY);
    }

    for (guint i = 0; i < list->len; i++) {
        xmlNode* gn = (xmlNode*)g_ptr_array_index(list, i);
        SvgStop* stops = NULL;
        int nst = 0;
        if (!collect_stops_from_element(doc, gn, &stops, &nst, 0) || nst <= 0) {
            debug_log("WRN", "gradient_svg_load: gradient %u has no stops in '%s'", (unsigned)i, filename);
            g_free(stops);
            g_ptr_array_free(list, TRUE);
            xmlFreeDoc(doc);
            gradient_set_free(set);
            FAIL(GRADIENT_SVG_ERROR_CORRUPT_FILE);
        }

        if (!svg_stops_pad_01(&stops, &nst)) {
            g_free(stops);
            g_ptr_array_free(list, TRUE);
            xmlFreeDoc(doc);
            gradient_set_free(set);
            FAIL(GRADIENT_SVG_ERROR_OUT_OF_MEMORY);
        }

        GradientDef* def = &set->gradients[(int)i];
        def->name = gradient_name_from_node(gn, (int)i);
        if (!def->name) {
            g_free(stops);
            g_ptr_array_free(list, TRUE);
            xmlFreeDoc(doc);
            gradient_set_free(set);
            FAIL(GRADIENT_SVG_ERROR_OUT_OF_MEMORY);
        }
        if (!stops_to_gradient_def(stops, nst, def)) {
            g_free(stops);
            g_ptr_array_free(list, TRUE);
            xmlFreeDoc(doc);
            gradient_set_free(set);
            FAIL(GRADIENT_SVG_ERROR_OUT_OF_MEMORY);
        }
        g_free(stops);
    }

    g_ptr_array_free(list, TRUE);
    xmlFreeDoc(doc);
    debug_log("DBG", "gradient_svg_load: loaded %d linearGradient(s) from '%s'", set->num_gradients, filename);
    return set;

cleanup:
    return NULL;
#undef SET_ERR
#undef FAIL
}

/**
 * Build sorted stop list from segment left/right positions (typical count:
 * num_segments + 1 after merging shared boundaries).
 */
static SvgStop* gradient_def_build_svg_stops(const GradientDef* def, int* out_count) {
    *out_count = 0;
    if (!def || def->num_segments <= 0 || !def->segments)
        return NULL;

    GArray* raw = g_array_sized_new(FALSE, FALSE, sizeof(SvgStop), (guint)def->num_segments * 2u);
    if (!raw)
        return NULL;

    for (int i = 0; i < def->num_segments; i++) {
        const GradientSegment* s = &def->segments[i];
        SvgStop sl, sr;
        sl.pos = clamp01(s->left_pos);
        sr.pos = clamp01(s->right_pos);
        gradient_evaluate(def, sl.pos, &sl.r, &sl.g, &sl.b, &sl.a);
        gradient_evaluate(def, sr.pos, &sr.r, &sr.g, &sr.b, &sr.a);
        sl.r = clamp01(sl.r);
        sl.g = clamp01(sl.g);
        sl.b = clamp01(sl.b);
        sl.a = clamp01(sl.a);
        sr.r = clamp01(sr.r);
        sr.g = clamp01(sr.g);
        sr.b = clamp01(sr.b);
        sr.a = clamp01(sr.a);
        g_array_append_val(raw, sl);
        g_array_append_val(raw, sr);
    }

    g_array_sort(raw, cmp_stop_pos);

    GArray* out = g_array_new(FALSE, FALSE, sizeof(SvgStop));
    if (!out) {
        g_array_free(raw, TRUE);
        return NULL;
    }

    for (guint i = 0; i < raw->len; i++) {
        SvgStop st = g_array_index(raw, SvgStop, i);
        if (out->len > 0) {
            SvgStop* prev = &g_array_index(out, SvgStop, out->len - 1);
            if (fabs(prev->pos - st.pos) <= SVG_POS_EPS) {
                *prev = st;
                continue;
            }
        }
        g_array_append_val(out, st);
    }
    g_array_free(raw, TRUE);

    int count = (int)out->len;
    if (count <= 0) {
        g_array_free(out, TRUE);
        return NULL;
    }

    SvgStop* result = g_malloc((size_t)count * sizeof(SvgStop));
    if (!result) {
        g_array_free(out, TRUE);
        return NULL;
    }
    memcpy(result, out->data, (size_t)count * sizeof(SvgStop));
    g_array_free(out, TRUE);
    *out_count = count;
    return result;
}

gboolean gradient_svg_save(const GradientSet* set, const char* filename, GradientSvgError* error_out) {
#define SET_ERR(e)            \
    do {                      \
        if (error_out)        \
            *error_out = (e); \
    } while (0)
#define FAIL(e)       \
    do {              \
        SET_ERR(e);   \
        goto cleanup; \
    } while (0)

    SET_ERR(GRADIENT_SVG_ERROR_NONE);
    if (!filename)
        FAIL(GRADIENT_SVG_ERROR_INVALID_PARAMETERS);
    if (!set)
        FAIL(GRADIENT_SVG_ERROR_INVALID_PARAMETERS);

    xmlTextWriterPtr w = xmlNewTextWriterFilename(filename, 0);
    if (!w) {
        debug_log("WRN", "gradient_svg_save: cannot open '%s'", filename);
        FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
    }

    if (xmlTextWriterSetIndent(w, 1) < 0 || xmlTextWriterSetIndentString(w, BAD_CAST "  ") < 0) { /* ignore */
    }

    if (xmlTextWriterStartDocument(w, NULL, "UTF-8", NULL) < 0)
        FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);

    if (xmlTextWriterStartElement(w, BAD_CAST "svg") < 0)
        FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);

    if (xmlTextWriterStartElement(w, BAD_CAST "defs") < 0)
        FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);

    for (int gi = 0; gi < set->num_gradients; gi++) {
        const GradientDef* def = &set->gradients[gi];
        if (!def->segments || def->num_segments <= 0)
            continue;

        gchar* gid = (def->name && def->name[0]) ? g_strdup(def->name) : g_strdup_printf("gradient_%d", gi);
        if (!gid)
            FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
        if (!gid[0]) {
            g_free(gid);
            gid = g_strdup_printf("rasterlab_gradient_%d", gi);
            if (!gid)
                FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
        }
        for (gchar* p = gid; *p; p++) {
            if (g_ascii_isalnum(*p) || *p == '_' || *p == '-' || *p == '.' || *p == ':')
                continue;
            *p = '_';
        }

        if (xmlTextWriterStartElement(w, BAD_CAST "linearGradient") < 0) {
            g_free(gid);
            FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
        }
        if (xmlTextWriterWriteAttribute(w, BAD_CAST "id", BAD_CAST gid) < 0) {
            g_free(gid);
            FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
        }
        g_free(gid);

        if (xmlTextWriterWriteAttribute(w, BAD_CAST "gradientUnits", BAD_CAST "objectBoundingBox") < 0)
            FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
        if (xmlTextWriterWriteAttribute(w, BAD_CAST "x1", BAD_CAST "0") < 0)
            FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
        if (xmlTextWriterWriteAttribute(w, BAD_CAST "y1", BAD_CAST "0") < 0)
            FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
        if (xmlTextWriterWriteAttribute(w, BAD_CAST "x2", BAD_CAST "1") < 0)
            FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
        if (xmlTextWriterWriteAttribute(w, BAD_CAST "y2", BAD_CAST "0") < 0)
            FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
        if (xmlTextWriterWriteAttribute(w, BAD_CAST "spreadMethod", BAD_CAST "pad") < 0)
            FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);

        int n_export = 0;
        SvgStop* export_stops = gradient_def_build_svg_stops(def, &n_export);
        if (!export_stops || n_export <= 0) {
            g_free(export_stops);
            FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
        }

        for (int si = 0; si < n_export; si++) {
            const SvgStop* st = &export_stops[si];
            char offbuf[48];
            g_snprintf(offbuf, sizeof(offbuf), "%.6f", st->pos);
            if (xmlTextWriterStartElement(w, BAD_CAST "stop") < 0) {
                g_free(export_stops);
                FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
            }
            if (xmlTextWriterWriteAttribute(w, BAD_CAST "offset", BAD_CAST offbuf) < 0) {
                g_free(export_stops);
                FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
            }

            char hx[16];
            int ir = (int)floor(st->r * 255.0 + 0.5);
            int ig = (int)floor(st->g * 255.0 + 0.5);
            int ib = (int)floor(st->b * 255.0 + 0.5);
            ir = CLAMP(ir, 0, 255);
            ig = CLAMP(ig, 0, 255);
            ib = CLAMP(ib, 0, 255);
            g_snprintf(hx, sizeof(hx), "#%02x%02x%02x", ir, ig, ib);
            if (xmlTextWriterWriteAttribute(w, BAD_CAST "stop-color", BAD_CAST hx) < 0) {
                g_free(export_stops);
                FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
            }

            g_snprintf(offbuf, sizeof(offbuf), "%.6f", st->a);
            if (xmlTextWriterWriteAttribute(w, BAD_CAST "stop-opacity", BAD_CAST offbuf) < 0) {
                g_free(export_stops);
                FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
            }

            if (xmlTextWriterEndElement(w) < 0) {
                g_free(export_stops);
                FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
            }
        }
        g_free(export_stops);

        if (xmlTextWriterEndElement(w) < 0)
            FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
    }

    if (xmlTextWriterEndElement(w) < 0)
        FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
    if (xmlTextWriterEndElement(w) < 0)
        FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);
    if (xmlTextWriterEndDocument(w) < 0)
        FAIL(GRADIENT_SVG_ERROR_FILE_WRITE_ERROR);

    xmlFreeTextWriter(w);
    debug_log("DBG", "gradient_svg_save: wrote '%s'", filename);
    return TRUE;

cleanup:
    if (w)
        xmlFreeTextWriter(w);
    return FALSE;
#undef SET_ERR
#undef FAIL
}
