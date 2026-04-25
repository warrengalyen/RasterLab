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
 * Adobe SpeedGrade / Iridas .look (XML) 3D LUT.
 * Structure follows common community reverse-engineering (e.g. hex RGB triples
 * in <LUT><size>"n"</size><data>...</data></LUT>).
 */

#include "io/lut3d_look.h"
#include "debug_logger.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlwriter.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

/* ------------------------------------------------------------------ */
/* XML helpers */
/* ------------------------------------------------------------------ */

static xmlNode* find_element_depth_first(const xmlNode* n, const xmlChar* name) {
    for (; n; n = n->next) {
        if (n->type == XML_ELEMENT_NODE) {
            if (xmlStrcmp(n->name, name) == 0)
                return (xmlNode*)n;
            xmlNode* c = n->children ? find_element_depth_first(n->children, name) : NULL;
            if (c) return c;
        }
    }
    return NULL;
}

static xmlNode* find_first_lut_in_doc(xmlDoc* doc) {
    xmlNode* root = doc ? xmlDocGetRootElement(doc) : NULL;
    if (!root) return NULL;
    return find_element_depth_first(root, BAD_CAST "LUT");
}

static xmlNode* first_child_element_named(const xmlNode* parent, const char* name) {
    if (!parent) return NULL;
    for (xmlNode* c = parent->children; c; c = c->next) {
        if (c->type == XML_ELEMENT_NODE && xmlStrcmp(c->name, (const xmlChar*)name) == 0) return c;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* size / hex / f32 */
/* ------------------------------------------------------------------ */

static int parse_quoted_size(const char* t) {
    char*  s  = t ? g_strdup(t) : g_strdup("");
    gsize  z;
    g_strstrip(s);
    z = strlen(s);
    if (z >= 2 && s[0] == '"' && s[z - 1] == '"') { s[z - 1] = '\0'; memmove(s, s + 1, z - 1); }
    {
        int v = atoi(s);
        g_free(s);
        return v;
    }
}

/* Strip non-hex, pair hex digits, output raw bytes. */
static GByteArray* hex_decode_ascii(const char* t) {
    if (!t) return NULL;
    GString* hex = g_string_new(NULL);
    for (; *t; t++) {
        if (g_ascii_isxdigit((guchar)*t)) g_string_append_c(hex, *t);
    }
    if (hex->len < 2 || (hex->len % 2) != 0) {
        g_string_free(hex, TRUE);
        return NULL;
    }
    GByteArray* a = g_byte_array_sized_new((guint)(hex->len / 2));
    for (guint j = 0; j + 1U < hex->len; j += 2) {
        int       hi  = g_ascii_xdigit_value(hex->str[j]);
        int       lo  = g_ascii_xdigit_value(hex->str[j + 1]);
        guint8  byte = (guint8)((hi << 4) | lo);
        g_byte_array_append(a, &byte, 1);
    }
    g_string_free(hex, TRUE);
    return a;
}

static float read_f32le(const uint8_t* p) {
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float     f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static void write_f32le_to_string(GString* h, float f) {
    uint32_t u;
    uint8_t  p[4];
    memcpy(&u, &f, sizeof(u));
    p[0] = (guint8)(u & 0xFFU);
    p[1] = (guint8)((u >> 8) & 0xFFU);
    p[2] = (guint8)((u >> 16) & 0xFFU);
    p[3] = (guint8)((u >> 24) & 0xFFU);
    g_string_append_printf(h, "%02X%02X%02X%02X", (unsigned)p[0], (unsigned)p[1], (unsigned)p[2], (unsigned)p[3]);
}

/* ------------------------------------------------------------------ */
/* load */
/* ------------------------------------------------------------------ */

ColorLut3D* lut3d_look_load(const char* filename, Lut3dLookError* error_out) {
    if (error_out) *error_out = LUT3D_LOOK_ERROR_NONE;
    if (!filename) {
        if (error_out) *error_out = LUT3D_LOOK_ERROR_INVALID_PARAMETERS;
        return NULL;
    }
    xmlDoc* doc = xmlReadFile(filename, NULL, (int)(XML_PARSE_NONET | XML_PARSE_HUGE | XML_PARSE_NOWARNING | XML_PARSE_NOERROR));
    if (!doc) {
        if (g_file_test(filename, G_FILE_TEST_IS_REGULAR))
            if (error_out) *error_out = LUT3D_LOOK_ERROR_CORRUPT_FILE;
        else
            if (error_out) *error_out = LUT3D_LOOK_ERROR_FILE_NOT_FOUND;
        debug_log("WRN", "lut3d_look_load: xmlReadFile failed for '%s'", filename);
        return NULL;
    }
    ColorLut3D*  lut  = NULL;
    Lut3dLookError le = LUT3D_LOOK_ERROR_NONE;

    xmlNode* lute = find_first_lut_in_doc(doc);
    if (!lute) {
        le = LUT3D_LOOK_ERROR_CORRUPT_FILE;
        goto out;
    }
    xmlNode*     szn = first_child_element_named(lute, "size");
    xmlNode*     datn = first_child_element_named(lute, "data");
    if (!szn || !datn) {
        le = LUT3D_LOOK_ERROR_CORRUPT_FILE;
        goto out;
    }
    xmlChar* szt = xmlNodeGetContent(szn);
    int      edge  = parse_quoted_size(szt ? (const char*)szt : "0");
    xmlFree(szt);
    if (edge < LUT3D_SIZE_MIN || edge > LUT3D_SIZE_MAX) {
        le = LUT3D_LOOK_ERROR_CORRUPT_FILE;
        goto out;
    }
    int64_t cells  = (int64_t)edge * edge * edge;

    xmlChar*     dtext = xmlNodeGetContent(datn);
    GByteArray*  bytes  = hex_decode_ascii(dtext ? (const char*)dtext : "");
    if (dtext) xmlFree(dtext);
    if (!bytes) {
        le = LUT3D_LOOK_ERROR_CORRUPT_FILE;
        goto out;
    }
    size_t nb = bytes->len;
    if (nb < (size_t)cells * 12U) {
        g_byte_array_free(bytes, TRUE);
        le = LUT3D_LOOK_ERROR_CORRUPT_FILE;
        goto out;
    }
    if (nb % 12) {
        g_byte_array_free(bytes, TRUE);
        le = LUT3D_LOOK_ERROR_CORRUPT_FILE;
        goto out;
    }
    int64_t ntr = (int64_t)(nb / 12);
    if (ntr < cells) {
        g_byte_array_free(bytes, TRUE);
        le = LUT3D_LOOK_ERROR_CORRUPT_FILE;
        goto out;
    }
    if (ntr > cells) {
        if (ntr == cells + 1) {
            debug_log("DBG", "lut3d_look_load: discarding 1 extra RGB triplet from data");
        } else
            debug_log("WRN", "lut3d_look_load: using first %" PRId64 " of %" PRId64 " triples", (int64_t)cells, ntr);
    }

    lut = lut3d_new(edge);
    if (!lut) {
        g_byte_array_free(bytes, TRUE);
        le = LUT3D_LOOK_ERROR_OUT_OF_MEMORY;
        goto out;
    }
    const uint8_t* raw = bytes->data;
    for (int64_t i = 0; i < cells; i++) {
        lut->rgb[(size_t)i * 3U + 0U] = read_f32le(raw + (size_t)i * 12U);
        lut->rgb[(size_t)i * 3U + 1U] = read_f32le(raw + (size_t)i * 12U + 4U);
        lut->rgb[(size_t)i * 3U + 2U] = read_f32le(raw + (size_t)i * 12U + 8U);
    }
    g_byte_array_free(bytes, TRUE);

out:
    if (le != LUT3D_LOOK_ERROR_NONE) {
        lut3d_free(lut);
        lut = NULL;
    }
    if (error_out) *error_out = le;
    if (doc) { xmlFreeDoc(doc); }
    return lut;
}

/* ------------------------------------------------------------------ */
/* save */
/* ------------------------------------------------------------------ */

gboolean lut3d_look_save(const ColorLut3D* lut, const char* filename, Lut3dLookError* error_out) {
    if (error_out) *error_out = LUT3D_LOOK_ERROR_NONE;
    if (!lut || !filename || !lut->rgb) {
        if (error_out) *error_out = LUT3D_LOOK_ERROR_INVALID_PARAMETERS;
        return FALSE;
    }
    int n = lut->size;
    if (n < LUT3D_SIZE_MIN || n > LUT3D_SIZE_MAX) {
        if (error_out) *error_out = LUT3D_LOOK_ERROR_INVALID_PARAMETERS;
        return FALSE;
    }
    int      cells = n * n * n;
    GString* h    = g_string_sized_new((guint)cells * 8 + 64);
    for (int i = 0; i < cells; i++) {
        float fr, fg, fb;
        fr = lut->rgb[(size_t)i * 3U + 0U];
        fg = lut->rgb[(size_t)i * 3U + 1U];
        fb = lut->rgb[(size_t)i * 3U + 2U];
        write_f32le_to_string(h, fr);
        write_f32le_to_string(h, fg);
        write_f32le_to_string(h, fb);
    }

    xmlTextWriterPtr w   = xmlNewTextWriterFilename(filename, 0);
    gboolean         ok  = FALSE;
    if (!w) {
        g_string_free(h, TRUE);
        if (error_out) *error_out = LUT3D_LOOK_ERROR_FILE_WRITE_ERROR;
        return FALSE;
    }
    (void)xmlTextWriterSetIndent(w, 1);
    (void)xmlTextWriterSetIndentString(w, BAD_CAST "  ");
    do {
        if (xmlTextWriterStartDocument(w, NULL, "UTF-8", NULL) < 0) break;
        if (xmlTextWriterStartElement(w, BAD_CAST "Look") < 0) break;
        if (lut->title && lut->title[0]) {
            if (xmlTextWriterStartElement(w, BAD_CAST "Description") < 0) break;
            if (xmlTextWriterWriteString(w, (const xmlChar*)lut->title) < 0) break;
            if (xmlTextWriterEndElement(w) < 0) break;
        }
        if (lut->copyright && lut->copyright[0]) {
            if (xmlTextWriterStartElement(w, BAD_CAST "Copyright") < 0) break;
            if (xmlTextWriterWriteString(w, (const xmlChar*)lut->copyright) < 0) break;
            if (xmlTextWriterEndElement(w) < 0) break;
        }
        if (xmlTextWriterStartElement(w, BAD_CAST "LUT") < 0) break;
        if (xmlTextWriterStartElement(w, BAD_CAST "size") < 0) break;
        {
            gchar* sz = g_strdup_printf("\"%d\"", n);
            if (xmlTextWriterWriteString(w, (const xmlChar*)sz) < 0) { g_free(sz); break; }
            g_free(sz);
        }
        if (xmlTextWriterEndElement(w) < 0) break; /* size */
        if (xmlTextWriterStartElement(w, BAD_CAST "data") < 0) break;
        if (xmlTextWriterWriteString(w, (const xmlChar*)h->str) < 0) {
            g_string_free(h, TRUE);
            h = NULL;
            break;
        }
        g_string_free(h, TRUE);
        h = NULL;
        if (xmlTextWriterEndElement(w) < 0) break; /* data */
        if (xmlTextWriterEndElement(w) < 0) break; /* LUT */
        if (xmlTextWriterEndElement(w) < 0) break; /* Look */
        if (xmlTextWriterEndDocument(w) < 0) break;
        ok = TRUE;
    } while (0);

    if (h) g_string_free(h, TRUE);
    xmlFreeTextWriter(w);
    if (ok) {
        if (error_out) *error_out = LUT3D_LOOK_ERROR_NONE;
        return TRUE;
    }
    if (error_out) *error_out = LUT3D_LOOK_ERROR_FILE_WRITE_ERROR;
    return FALSE;
}
