/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "app/autosave.h"
#include "document.h"
#include "render/layer.h"
#include "render/tile.h"
#include "io/image_io.h"
#include "plugins/plugin_rli.h"
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "debug_logger.h"

/* Autosave configuration */
#define AUTOSAVE_INTERVAL_MIN 30
#define AUTOSAVE_INTERVAL_MAX 2700
#define AUTOSAVE_INTERVAL_DEFAULT 300   /* Default interval when not set from settings */

/* Internal storage */
static GHashTable* g_document_map = NULL;   /* Map ImageDocument* -> autosave_id */
static GHashTable* g_id_map = NULL;         /* Map autosave_id -> ImageDocument* */
static guint g_autosave_timer_id = 0;       /* GTK timer ID */
static guint g_autosave_interval_seconds = AUTOSAVE_INTERVAL_DEFAULT; /* Current interval (30-2700) */
static gboolean g_initialized = FALSE;

/**
 * Generate a unique autosave ID for a document
 */
static gchar* generate_autosave_id(ImageDocument* doc) {
    gchar* id_str;
    guint hash = 0;
    const gchar* path = doc->file_path ? doc->file_path : doc->filename;

    /* Generate hash from document pointer and path */
    if (path) {
        hash = g_str_hash(path);
    }
    hash ^= GPOINTER_TO_UINT(doc);

    /* Use timestamp for uniqueness */
    time_t now = time(NULL);

    id_str = g_strdup_printf("%08x_%ld", hash, (long)now);
    return id_str;
}

/**
 * Get the autosave directory path
 */
static gchar* autosave_get_directory(void) {
    gchar* app_dir = g_get_current_dir();
    gchar* autosave_dir = g_build_filename(app_dir, "autosave", NULL);
    g_free(app_dir);
    return autosave_dir;
}

/**
 * Ensure autosave directory exists
 */
static gboolean autosave_ensure_directory(void) {
    gchar* autosave_dir = autosave_get_directory();
    gboolean success = TRUE;

    if (g_mkdir_with_parents(autosave_dir, 0755) != 0) {
        debug_log("WRN", "Failed to create autosave directory: %s", autosave_dir);
        success = FALSE;
    }

    g_free(autosave_dir);
    return success;
}

/**
 * Get autosave file path (.rli) for a document ID
 */
static gchar* autosave_get_file_path(const gchar* autosave_id) {
    gchar* autosave_dir = autosave_get_directory();
    gchar* filename = g_strdup_printf("autosave_%s.rli", autosave_id);
    gchar* file_path = g_build_filename(autosave_dir, filename, NULL);

    g_free(autosave_dir);
    g_free(filename);

    return file_path;
}

/**
 * Get autosave sidecar metadata path (.meta) for a document ID.
 * The .meta file stores the original file path and other display metadata.
 */
static gchar* autosave_get_meta_path(const gchar* autosave_id) {
    gchar* autosave_dir = autosave_get_directory();
    gchar* filename = g_strdup_printf("autosave_%s.meta", autosave_id);
    gchar* file_path = g_build_filename(autosave_dir, filename, NULL);

    g_free(autosave_dir);
    g_free(filename);

    return file_path;
}

/**
 * Save document to autosave file using the RLI format.
 * A small .meta sidecar file stores the original file path for display in the
 * recovery dialog.
 */
gboolean autosave_save_document(ImageDocument* doc) {
    if (!doc || !g_initialized) {
        return FALSE;
    }

    if (!doc->modified) {
        return TRUE; /* Not an error, just nothing to save */
    }

    gchar* autosave_id = (gchar*)g_hash_table_lookup(g_document_map, doc);
    if (!autosave_id) {
        return FALSE;
    }

    if (!autosave_ensure_directory()) {
        return FALSE;
    }

    gchar* rli_path  = autosave_get_file_path(autosave_id);
    gchar* meta_path = autosave_get_meta_path(autosave_id);

    /* Write .meta sidecar: one "key=value" line per entry */
    const gchar* orig = doc->file_path ? doc->file_path : "";
    gchar* meta_content = g_strdup_printf("original_path=%s\n", orig);
    if (!g_file_set_contents(meta_path, meta_content, -1, NULL)) {
        debug_log("WRN", "autosave: failed to write meta file: %s", meta_path);
    }
    g_free(meta_content);
    g_free(meta_path);

    /* Save document data as RLI */
    PluginError err = PLUGIN_ERROR_NONE;
    gboolean ok = image_io_save(doc, rli_path, NULL, &err);
    if (!ok) {
        debug_log("WRN", "autosave: image_io_save failed (error %d) for %s", (int)err, rli_path);
    }

    g_free(rli_path);
    return ok;
}

/**
 * Load a document from an autosave .rli file.
 * The original file path is restored from the companion .meta sidecar if present.
 */
ImageDocument* autosave_load_document(const gchar* autosave_path) {
    if (!autosave_path) {
        return NULL;
    }

    /* Create a minimal document shell; RLI loader fills width/height/layers */
    gchar* basename = g_path_get_basename(autosave_path);
    ImageDocument* doc = document_new(basename, TRUE, 10);
    g_free(basename);

    if (!doc) {
        return NULL;
    }

    /* Load RLI data */
    PluginError err = PLUGIN_ERROR_NONE;
    if (!image_io_load(doc, autosave_path, &err, NULL)) {
        debug_log("WRN", "autosave: failed to load %s (error %d)", autosave_path, (int)err);
        document_free(doc);
        return NULL;
    }

    /* Restore original file path from .meta sidecar */
    if (g_str_has_suffix(autosave_path, ".rli")) {
        gsize base_len = strlen(autosave_path) - 4; /* strip ".rli" */
        gchar* base    = g_strndup(autosave_path, base_len);
        gchar* meta_path = g_strdup_printf("%s.meta", base);
        g_free(base);

        gchar* meta_content = NULL;
        if (g_file_get_contents(meta_path, &meta_content, NULL, NULL)) {
            if (g_str_has_prefix(meta_content, "original_path=")) {
                gchar* orig = meta_content + strlen("original_path=");
                g_strchomp(orig);
                if (*orig != '\0') {
                    g_free(doc->file_path);
                    doc->file_path = g_strdup(orig);
                }
            }
            g_free(meta_content);
        }
        g_free(meta_path);
    }

    /* Ensure tile grid is present (RLI loader does not create it) */
    if (!doc->tile_grid && doc->width > 0 && doc->height > 0) {
        doc->tile_grid = tile_grid_create(doc->width, doc->height, 128);
    }

    /* Select first layer */
    if (doc->layers) {
        document_set_selected_layer(doc, (ImageLayer*)doc->layers->data);
    }

    doc->modified = TRUE;
    document_invalidate_composite(doc);

    return doc;
}

/**
 * Timer callback for periodic autosave
 */
static gboolean autosave_timer_callback(gpointer user_data) {
    (void)user_data; /* Unused */ 

    if (!g_initialized || !g_id_map) {
        return TRUE; /* Continue timer */
    }

    /* Save all dirty documents */
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, g_id_map);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ImageDocument* doc = (ImageDocument*)value;
        if (doc && doc->modified) {
            autosave_save_document(doc);
        }
    }

    return TRUE; /* Continue timer */
}

/**
 * Start or restart the autosave timer with the current interval
 */
static void autosave_restart_timer(void) {
    if (g_autosave_timer_id != 0) {
        g_source_remove(g_autosave_timer_id);
        g_autosave_timer_id = 0;
    }
    g_autosave_timer_id = g_timeout_add_seconds(g_autosave_interval_seconds,
                                               autosave_timer_callback,
                                               NULL);
}

/**
 * Set the file recovery save interval in seconds (30-2700)
 */
void autosave_set_interval(guint seconds) {
    if (seconds < AUTOSAVE_INTERVAL_MIN) {
        seconds = AUTOSAVE_INTERVAL_MIN;
    } else if (seconds > AUTOSAVE_INTERVAL_MAX) {
        seconds = AUTOSAVE_INTERVAL_MAX;
    }
    g_autosave_interval_seconds = seconds;
    if (g_initialized && g_autosave_timer_id != 0) {
        autosave_restart_timer();
    }
}

/**
 * Initialize autosave system
 */
void autosave_init(void) {
    if (g_initialized) {
        return;
    }

    g_document_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_id_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* Ensure autosave directory exists */
    autosave_ensure_directory();

    /* Start periodic autosave timer with current interval */
    autosave_restart_timer();

    g_initialized = TRUE;
}

/**
 * Shutdown autosave system.
 * On a clean shutdown the app is not crashing, so there is nothing to recover
 * on the next launch.  Delete all autosave files so the recovery dialog does
 * not appear unnecessarily.
 */
void autosave_shutdown(void) {
    if (!g_initialized) {
        return;
    }

    /* Stop timer */
    if (g_autosave_timer_id != 0) {
        g_source_remove(g_autosave_timer_id);
        g_autosave_timer_id = 0;
    }

    /* Delete all autosave files — this is a clean exit, not a crash */
    if (g_id_map) {
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init(&iter, g_id_map);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            const gchar* autosave_id = (const gchar*)key;
            gchar* file_path = autosave_get_file_path(autosave_id);
            autosave_delete_file(file_path);
            g_free(file_path);
        }
    }

    /* Free hash tables */
    if (g_document_map) {
        g_hash_table_destroy(g_document_map);
        g_document_map = NULL;
    }

    if (g_id_map) {
        g_hash_table_destroy(g_id_map);
        g_id_map = NULL;
    }

    g_initialized = FALSE;
}

/**
 * Register a document for autosave tracking
 */
gchar* autosave_register_document(ImageDocument* doc) {
    if (!doc || !g_initialized) {
        return NULL;
    }

    /* Generate unique ID */
    gchar* autosave_id = generate_autosave_id(doc);

    /* Store mappings */
    g_hash_table_insert(g_document_map, doc, autosave_id);
    g_hash_table_insert(g_id_map, g_strdup(autosave_id), doc);

    return autosave_id;
}

/**
 * Unregister a document from autosave tracking and delete its autosave file.
 * Called when a document tab is closed cleanly — no crash-recovery file is needed.
 */
void autosave_unregister_document(ImageDocument* doc) {
    if (!doc || !g_initialized) {
        return;
    }

    gchar* autosave_id = (gchar*)g_hash_table_lookup(g_document_map, doc);
    if (autosave_id) {
        /* Delete the autosave file: the document was closed cleanly */
        gchar* file_path = autosave_get_file_path(autosave_id);
        autosave_delete_file(file_path);
        g_free(file_path);

        g_hash_table_remove(g_document_map, doc);
        g_hash_table_remove(g_id_map, autosave_id);
    }
}

/**
 * Delete an autosave file and its companion .meta sidecar (if any).
 */
void autosave_delete_file(const gchar* autosave_path) {
    if (!autosave_path) {
        return;
    }

    if (g_file_test(autosave_path, G_FILE_TEST_EXISTS)) {
        g_unlink(autosave_path);
    }

    /* Also remove the .meta sidecar */
    if (g_str_has_suffix(autosave_path, ".rli")) {
        gsize len    = strlen(autosave_path);
        gchar* base  = g_strndup(autosave_path, len - 4);
        gchar* meta  = g_strdup_printf("%s.meta", base);
        g_free(base);
        if (g_file_test(meta, G_FILE_TEST_EXISTS)) {
            g_unlink(meta);
        }
        g_free(meta);
    }
}

/**
 * Scan autosave directory for recovery files.
 * Looks for "autosave_<id>.rli" files with a valid RLI RLIB header.
 * Companion "autosave_<id>.meta" sidecars supply display metadata.
 */
GList* autosave_scan_recovery_files(void) {
    GList* recovery_list = NULL;
    gchar* autosave_dir = autosave_get_directory();

    if (!g_file_test(autosave_dir, G_FILE_TEST_IS_DIR)) {
        g_free(autosave_dir);
        return NULL;
    }

    GDir* dir = g_dir_open(autosave_dir, 0, NULL);
    if (!dir) {
        g_free(autosave_dir);
        return NULL;
    }

    const gchar* entry_name;
    while ((entry_name = g_dir_read_name(dir)) != NULL) {
        if (!g_str_has_prefix(entry_name, "autosave_") || !g_str_has_suffix(entry_name, ".rli")) {
            continue;
        }

        gchar* file_path = g_build_filename(autosave_dir, entry_name, NULL);

        /*
         * Validate the RLI file by reading the RLIB chunk header and extract
         * canvas dimensions.  Layout (all little-endian):
         *   [0..3]   chunk type  (uint32) = RLI_CHUNK_RLIB
         *   [4..11]  payload len (uint64)
         *   [12..13] version     (uint16)
         *   [14..15] flags       (uint16)
         *   [16..19] canvas_w    (uint32)
         *   [20..23] canvas_h    (uint32)
         *   [24..27] layer_count (uint32)
         */
        FILE* f = g_fopen(file_path, "rb");
        if (!f) {
            g_free(file_path);
            continue;
        }

        guint8 hdr[28];
        if (fread(hdr, sizeof(hdr), 1, f) != 1) {
            fclose(f);
            g_free(file_path);
            continue;
        }
        fclose(f);

        guint32 chunk_type = (guint32)hdr[0] | ((guint32)hdr[1] << 8)
                           | ((guint32)hdr[2] << 16) | ((guint32)hdr[3] << 24);
        if (chunk_type != RLI_CHUNK_RLIB) {
            g_free(file_path);
            continue;
        }

        guint32 canvas_w = (guint32)hdr[16] | ((guint32)hdr[17] << 8)
                         | ((guint32)hdr[18] << 16) | ((guint32)hdr[19] << 24);
        guint32 canvas_h = (guint32)hdr[20] | ((guint32)hdr[21] << 8)
                         | ((guint32)hdr[22] << 16) | ((guint32)hdr[23] << 24);
        guint32 layer_count = (guint32)hdr[24] | ((guint32)hdr[25] << 8)
                            | ((guint32)hdr[26] << 16) | ((guint32)hdr[27] << 24);

        if (canvas_w == 0 || canvas_h == 0) {
            g_free(file_path);
            continue;
        }

        /* Extract timestamp embedded in the autosave ID: "autosave_<hash>_<ts>.rli" */
        time_t timestamp = 0;
        {
            /* Strip "autosave_" prefix and ".rli" suffix to get the ID */
            gsize name_len = strlen(entry_name);
            gchar* id = g_strndup(entry_name + 9, name_len - 9 - 4);
            const gchar* us = strrchr(id, '_');
            if (us) {
                timestamp = (time_t)g_ascii_strtoll(us + 1, NULL, 10);
            }
            g_free(id);
        }

        /* Read original file path from companion .meta sidecar */
        gchar* original_path = g_strdup("");
        {
            gsize fp_len   = strlen(file_path);
            gchar* base    = g_strndup(file_path, fp_len - 4);
            gchar* meta_fp = g_strdup_printf("%s.meta", base);
            g_free(base);

            gchar* meta_content = NULL;
            if (g_file_get_contents(meta_fp, &meta_content, NULL, NULL)) {
                if (g_str_has_prefix(meta_content, "original_path=")) {
                    gchar* orig = meta_content + strlen("original_path=");
                    g_strchomp(orig);
                    g_free(original_path);
                    original_path = g_strdup(orig);
                }
                g_free(meta_content);
            }
            g_free(meta_fp);
        }

        AutosaveRecoveryEntry* rec = g_malloc(sizeof(AutosaveRecoveryEntry));
        rec->autosave_path = file_path;
        rec->original_path = original_path;
        rec->timestamp     = timestamp;
        rec->width         = canvas_w;
        rec->height        = canvas_h;
        rec->layer_count   = layer_count;

        recovery_list = g_list_prepend(recovery_list, rec);
    }

    g_dir_close(dir);
    g_free(autosave_dir);

    /* Sort by timestamp (most recent first) */
    recovery_list = g_list_sort(recovery_list, (GCompareFunc)autosave_compare_recovery_entries);
    return recovery_list;
}

/**
 * Compare function for sorting recovery entries by timestamp
 */
gint autosave_compare_recovery_entries(AutosaveRecoveryEntry* a, AutosaveRecoveryEntry* b) {
    if (a->timestamp > b->timestamp) {
        return -1;
    } else if (a->timestamp < b->timestamp) {
        return 1;
    }
    return 0;
}

/**
 * Free a recovery entry
 */
void autosave_free_recovery_entry(AutosaveRecoveryEntry* entry) {
    if (entry) {
        g_free(entry->autosave_path);
        g_free(entry->original_path);
        g_free(entry);
    }
}

/**
 * Free a list of recovery entries
 */
void autosave_free_recovery_list(GList* list) {
    g_list_free_full(list, (GDestroyNotify)autosave_free_recovery_entry);
}

/**
 * Get autosave path for a document
 */
const gchar* autosave_get_path(ImageDocument* doc) {
    if (!doc || !g_initialized) {
        return NULL;
    }

    gchar* autosave_id = (gchar*)g_hash_table_lookup(g_document_map, doc);
    if (!autosave_id) {
        return NULL;
    }

    /* Return static buffer - caller should copy if needed */
    static gchar* cached_path = NULL;
    if (cached_path) {
        g_free(cached_path);
    }
    cached_path = autosave_get_file_path(autosave_id);
    return cached_path;
}

/**
 * Mark document as saved (clean up autosave file)
 */
void autosave_mark_saved(ImageDocument* doc) {
    if (!doc || !g_initialized) {
        return;
    }

    const gchar* autosave_path = autosave_get_path(doc);
    if (autosave_path) {
        autosave_delete_file(autosave_path);
    }
}
