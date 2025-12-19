#include "app/autosave.h"
#include "document.h"
#include "render/layer.h"
#include "render/tile.h"
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Autosave configuration */
#define AUTOSAVE_INTERVAL_SECONDS 45 /* Auto-save every 45 seconds */
#define AUTOSAVE_MAGIC "IMGAUTOSAVE" /* Magic string to identify autosave files */
#define AUTOSAVE_VERSION 1           /* File format version */

/* Internal storage */
static GHashTable* g_document_map = NULL; /* Map ImageDocument* -> autosave_id */
static GHashTable* g_id_map = NULL;       /* Map autosave_id -> ImageDocument* */
static guint g_autosave_timer_id = 0;     /* GTK timer ID */
static gboolean g_initialized = FALSE;

/**
 * File format structure:
 *
 * Header (fixed size):
 *   - Magic string: "IMGAUTOSAVE" (11 bytes + null terminator = 12 bytes)
 *   - Version: uint32_t (4 bytes)
 *   - Timestamp: time_t (8 bytes)
 *   - Document width: uint32_t (4 bytes)
 *   - Document height: uint32_t (4 bytes)
 *   - Original file path length: uint32_t (4 bytes)
 *   - Original file path: (variable, UTF-8)
 *   - Layer count: uint32_t (4 bytes)
 *
 * For each layer:
 *   - Layer name length: uint32_t (4 bytes)
 *   - Layer name: (variable, UTF-8)
 *   - Layer width: uint32_t (4 bytes)
 *   - Layer height: uint32_t (4 bytes)
 *   - Layer offset_x: int32_t (4 bytes)
 *   - Layer offset_y: int32_t (4 bytes)
 *   - Layer opacity: double (8 bytes)
 *   - Layer visible: uint8_t (1 byte)
 *   - Layer blend_mode: uint32_t (4 bytes)
 *   - Pixel data size: uint32_t (4 bytes) - width * height * 4 (ARGB32)
 *   - Pixel data: (variable, ARGB32 format, BGRA in memory)
 */

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
        g_warning("Failed to create autosave directory: %s", autosave_dir);
        success = FALSE;
    }

    g_free(autosave_dir);
    return success;
}

/**
 * Get autosave file path for a document ID
 */
static gchar* autosave_get_file_path(const gchar* autosave_id) {
    gchar* autosave_dir = autosave_get_directory();
    gchar* filename = g_strdup_printf("autosave_%s.imgtmp", autosave_id);
    gchar* file_path = g_build_filename(autosave_dir, filename, NULL);

    g_free(autosave_dir);
    g_free(filename);

    return file_path;
}

/**
 * Write a string to file (length-prefixed)
 */
static gboolean write_string(FILE* file, const gchar* str) {
    guint32 len = str ? strlen(str) : 0;

    if (fwrite(&len, sizeof(guint32), 1, file) != 1) {
        return FALSE;
    }

    if (len > 0 && fwrite(str, 1, len, file) != len) {
        return FALSE;
    }

    return TRUE;
}

/**
 * Read a string from file (length-prefixed)
 */
static gchar* read_string(FILE* file) {
    guint32 len;

    if (fread(&len, sizeof(guint32), 1, file) != 1) {
        return NULL;
    }

    if (len == 0) {
        return g_strdup("");
    }

    gchar* str = (gchar*)g_malloc(len + 1);
    if (fread(str, 1, len, file) != len) {
        g_free(str);
        return NULL;
    }

    str[len] = '\0';
    return str;
}

/**
 * Save a layer to file
 */
static gboolean save_layer(FILE* file, ImageLayer* layer) {
    gint width, height, stride;
    guchar* surface_data;

    if (!layer || !layer->surface) {
        return FALSE;
    }

    width = cairo_image_surface_get_width(layer->surface);
    height = cairo_image_surface_get_height(layer->surface);
    stride = cairo_image_surface_get_stride(layer->surface);

    /* Flush surface to ensure all drawing is complete */
    cairo_surface_flush(layer->surface);
    surface_data = cairo_image_surface_get_data(layer->surface);

    if (!surface_data) {
        return FALSE;
    }

    /* Write layer metadata */
    if (!write_string(file, layer->name)) {
        return FALSE;
    }

    guint32 w = (guint32)width;
    guint32 h = (guint32)height;
    gint32 offset_x = (gint32)layer->offset_x;
    gint32 offset_y = (gint32)layer->offset_y;
    gdouble opacity = layer->opacity;
    guint8 visible = layer->visible ? 1 : 0;
    guint32 blend_mode = (guint32)layer->blend_mode;

    if (fwrite(&w, sizeof(guint32), 1, file) != 1)
        return FALSE;
    if (fwrite(&h, sizeof(guint32), 1, file) != 1)
        return FALSE;
    if (fwrite(&offset_x, sizeof(gint32), 1, file) != 1)
        return FALSE;
    if (fwrite(&offset_y, sizeof(gint32), 1, file) != 1)
        return FALSE;
    if (fwrite(&opacity, sizeof(gdouble), 1, file) != 1)
        return FALSE;
    if (fwrite(&visible, sizeof(guint8), 1, file) != 1)
        return FALSE;
    if (fwrite(&blend_mode, sizeof(guint32), 1, file) != 1)
        return FALSE;

    /* Write pixel data */
    guint32 data_size = width * height * 4; /* ARGB32 = 4 bytes per pixel */
    if (fwrite(&data_size, sizeof(guint32), 1, file) != 1) {
        return FALSE;
    }

    /* Copy pixel data row by row */
    for (gint y = 0; y < height; y++) {
        guchar* row = surface_data + y * stride;
        if (fwrite(row, width * 4, 1, file) != 1) {
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * Load a layer from file
 */
static ImageLayer* load_layer(FILE* file) {
    gchar* name = read_string(file);
    if (!name) {
        return NULL;
    }

    guint32 width, height;
    gint32 offset_x, offset_y;
    gdouble opacity;
    guint8 visible;
    guint32 blend_mode;
    guint32 data_size;

    if (fread(&width, sizeof(guint32), 1, file) != 1) {
        g_free(name);
        return NULL;
    }
    if (fread(&height, sizeof(guint32), 1, file) != 1) {
        g_free(name);
        return NULL;
    }
    if (fread(&offset_x, sizeof(gint32), 1, file) != 1) {
        g_free(name);
        return NULL;
    }
    if (fread(&offset_y, sizeof(gint32), 1, file) != 1) {
        g_free(name);
        return NULL;
    }
    if (fread(&opacity, sizeof(gdouble), 1, file) != 1) {
        g_free(name);
        return NULL;
    }
    if (fread(&visible, sizeof(guint8), 1, file) != 1) {
        g_free(name);
        return NULL;
    }
    if (fread(&blend_mode, sizeof(guint32), 1, file) != 1) {
        g_free(name);
        return NULL;
    }
    if (fread(&data_size, sizeof(guint32), 1, file) != 1) {
        g_free(name);
        return NULL;
    }

    /* Validate data size */
    if (data_size != width * height * 4) {
        g_warning("Invalid layer data size in autosave file");
        g_free(name);
        return NULL;
    }

    /* Create layer */
    ImageLayer* layer = layer_new(name, width, height, TRUE,
                                  LAYER_BACKGROUND_TRANSPARENT, LAYER_POSITION_ABOVE_CURRENT, NULL);
    g_free(name);

    if (!layer) {
        return NULL;
    }

    /* Set layer properties */
    layer->offset_x = offset_x;
    layer->offset_y = offset_y;
    layer->opacity = opacity;
    layer->visible = (visible != 0);
    layer->blend_mode = (BlendMode)blend_mode;

    /* Read pixel data */
    cairo_surface_flush(layer->surface);
    guchar* surface_data = cairo_image_surface_get_data(layer->surface);
    gint stride = cairo_image_surface_get_stride(layer->surface);

    if (!surface_data) {
        layer_free(layer);
        return NULL;
    }

    /* Copy pixel data row by row */
    for (guint y = 0; y < height; y++) {
        guchar* row = surface_data + y * stride;
        if (fread(row, width * 4, 1, file) != 1) {
            layer_free(layer);
            return NULL;
        }
    }

    cairo_surface_mark_dirty(layer->surface);

    return layer;
}

/**
 * Save document to autosave file
 */
gboolean autosave_save_document(ImageDocument* doc) {
    if (!doc || !g_initialized) {
        return FALSE;
    }

    /* Only save if document is dirty */
    if (!doc->modified) {
        return TRUE; /* Not an error, just nothing to save */
    }

    /* Get autosave ID */
    gchar* autosave_id = (gchar*)g_hash_table_lookup(g_document_map, doc);
    if (!autosave_id) {
        return FALSE;
    }

    /* Get file path */
    gchar* file_path = autosave_get_file_path(autosave_id);
    gchar* temp_path = g_strdup_printf("%s.tmp", file_path);

    /* Ensure directory exists */
    if (!autosave_ensure_directory()) {
        g_free(file_path);
        g_free(temp_path);
        return FALSE;
    }

    /* Open temporary file */
    FILE* file = g_fopen(temp_path, "wb");
    if (!file) {
        g_warning("Failed to open autosave file for writing: %s", temp_path);
        g_free(file_path);
        g_free(temp_path);
        return FALSE;
    }

    /* Write header */
    gchar magic[12] = AUTOSAVE_MAGIC;
    magic[11] = '\0';
    if (fwrite(magic, 12, 1, file) != 1) {
        fclose(file);
        g_unlink(temp_path);
        g_free(file_path);
        g_free(temp_path);
        return FALSE;
    }

    guint32 version = AUTOSAVE_VERSION;
    time_t timestamp = time(NULL);
    guint32 width = (guint32)doc->width;
    guint32 height = (guint32)doc->height;

    if (fwrite(&version, sizeof(guint32), 1, file) != 1) {
        fclose(file);
        g_unlink(temp_path);
        g_free(file_path);
        g_free(temp_path);
        return FALSE;
    }

    if (fwrite(&timestamp, sizeof(time_t), 1, file) != 1) {
        fclose(file);
        g_unlink(temp_path);
        g_free(file_path);
        g_free(temp_path);
        return FALSE;
    }

    if (fwrite(&width, sizeof(guint32), 1, file) != 1) {
        fclose(file);
        g_unlink(temp_path);
        g_free(file_path);
        g_free(temp_path);
        return FALSE;
    }

    if (fwrite(&height, sizeof(guint32), 1, file) != 1) {
        fclose(file);
        g_unlink(temp_path);
        g_free(file_path);
        g_free(temp_path);
        return FALSE;
    }

    /* Write original file path */
    const gchar* original_path = doc->file_path ? doc->file_path : "";
    if (!write_string(file, original_path)) {
        fclose(file);
        g_unlink(temp_path);
        g_free(file_path);
        g_free(temp_path);
        return FALSE;
    }

    /* Write layer count */
    guint layer_count = g_list_length(doc->layers);
    guint32 layer_count_u32 = (guint32)layer_count;
    if (fwrite(&layer_count_u32, sizeof(guint32), 1, file) != 1) {
        fclose(file);
        g_unlink(temp_path);
        g_free(file_path);
        g_free(temp_path);
        return FALSE;
    }

    /* Write each layer */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        ImageLayer* layer = (ImageLayer*)iter->data;
        if (!save_layer(file, layer)) {
            fclose(file);
            g_unlink(temp_path);
            g_free(file_path);
            g_free(temp_path);
            return FALSE;
        }
    }

    fclose(file);

    /* Atomically rename temp file to final file */
    if (g_rename(temp_path, file_path) != 0) {
        g_warning("Failed to rename autosave file: %s", file_path);
        g_unlink(temp_path);
        g_free(file_path);
        g_free(temp_path);
        return FALSE;
    }

    g_free(file_path);
    g_free(temp_path);

    return TRUE;
}

/**
 * Load document from autosave file
 */
ImageDocument* autosave_load_document(const gchar* autosave_path) {
    FILE* file = g_fopen(autosave_path, "rb");
    if (!file) {
        return NULL;
    }

    /* Read and verify magic */
    gchar magic[12];
    if (fread(magic, 12, 1, file) != 1) {
        fclose(file);
        return NULL;
    }

    if (strcmp(magic, AUTOSAVE_MAGIC) != 0) {
        g_warning("Invalid autosave file magic");
        fclose(file);
        return NULL;
    }

    /* Read version */
    guint32 version;
    if (fread(&version, sizeof(guint32), 1, file) != 1) {
        fclose(file);
        return NULL;
    }

    if (version != AUTOSAVE_VERSION) {
        g_warning("Unsupported autosave file version: %u", version);
        fclose(file);
        return NULL;
    }

    /* Read timestamp */
    time_t timestamp;
    if (fread(&timestamp, sizeof(time_t), 1, file) != 1) {
        fclose(file);
        return NULL;
    }

    /* Read document dimensions */
    guint32 width, height;
    if (fread(&width, sizeof(guint32), 1, file) != 1) {
        fclose(file);
        return NULL;
    }
    if (fread(&height, sizeof(guint32), 1, file) != 1) {
        fclose(file);
        return NULL;
    }

    /* Read original file path */
    gchar* original_path = read_string(file);
    if (!original_path) {
        fclose(file);
        return NULL;
    }

    /* Read layer count */
    guint32 layer_count;
    if (fread(&layer_count, sizeof(guint32), 1, file) != 1) {
        g_free(original_path);
        fclose(file);
        return NULL;
    }

    /* Create document */
    gchar* filename = g_path_get_basename(original_path);
    if (!filename || strlen(filename) == 0) {
        filename = g_strdup("Recovered");
    }

    ImageDocument* doc = document_new(filename);
    g_free(filename);

    if (!doc) {
        g_free(original_path);
        fclose(file);
        return NULL;
    }

    /* Set document properties */
    doc->width = width;
    doc->height = height;
    doc->has_alpha = TRUE; /* Assume alpha for recovered documents */
    doc->channels = 4;
    doc->bit_depth = 8;

    if (original_path && strlen(original_path) > 0) {
        doc->file_path = g_strdup(original_path);
    }
    g_free(original_path);

    /* Create tile grid */
    doc->tile_grid = tile_grid_create(doc->width, doc->height, 128);

    /* Load layers */
    for (guint32 i = 0; i < layer_count; i++) {
        ImageLayer* layer = load_layer(file);
        if (!layer) {
            /* Failed to load layer - free document and abort */
            document_free(doc);
            fclose(file);
            return NULL;
        }

        doc->layers = g_list_append(doc->layers, layer);
    }

    fclose(file);

    /* Set selected layer to first layer */
    if (doc->layers) {
        ImageLayer* first_layer = (ImageLayer*)g_list_first(doc->layers)->data;
        document_set_selected_layer(doc, first_layer);
    }

    /* Mark as modified (unsaved) */
    doc->modified = TRUE;

    /* Mark composite as dirty */
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

    /* Start periodic autosave timer */
    g_autosave_timer_id = g_timeout_add_seconds(AUTOSAVE_INTERVAL_SECONDS,
                                                autosave_timer_callback,
                                                NULL);

    g_initialized = TRUE;
}

/**
 * Shutdown autosave system
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

    /* Save all documents one final time */
    if (g_id_map) {
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init(&iter, g_id_map);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            ImageDocument* doc = (ImageDocument*)value;
            if (doc && doc->modified) {
                autosave_save_document(doc);
            }
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
 * Unregister a document from autosave tracking
 */
void autosave_unregister_document(ImageDocument* doc) {
    if (!doc || !g_initialized) {
        return;
    }

    gchar* autosave_id = (gchar*)g_hash_table_lookup(g_document_map, doc);
    if (autosave_id) {
        /* Remove from both maps */
        g_hash_table_remove(g_document_map, doc);
        g_hash_table_remove(g_id_map, autosave_id);
    }
}

/**
 * Delete an autosave file
 */
void autosave_delete_file(const gchar* autosave_path) {
    if (autosave_path && g_file_test(autosave_path, G_FILE_TEST_EXISTS)) {
        g_unlink(autosave_path);
    }
}

/**
 * Scan autosave directory for recovery files
 */
GList* autosave_scan_recovery_files(void) {
    GList* recovery_list = NULL;
    gchar* autosave_dir = autosave_get_directory();
    GDir* dir;
    const gchar* entry;
    gchar magic[12];
    FILE* file;

    /* Check if directory exists */
    if (!g_file_test(autosave_dir, G_FILE_TEST_IS_DIR)) {
        g_free(autosave_dir);
        return NULL;
    }

    dir = g_dir_open(autosave_dir, 0, NULL);
    if (!dir) {
        g_free(autosave_dir);
        return NULL;
    }

    while ((entry = g_dir_read_name(dir)) != NULL) {
        /* Check if file matches autosave pattern */
        if (!g_str_has_prefix(entry, "autosave_") || !g_str_has_suffix(entry, ".imgtmp")) {
            continue;
        }

        gchar* file_path = g_build_filename(autosave_dir, entry, NULL);

        /* Try to read header to validate file */
        file = g_fopen(file_path, "rb");
        if (!file) {
            g_free(file_path);
            continue;
        }

        /* Read magic */
        if (fread(magic, 12, 1, file) != 1) {
            fclose(file);
            g_free(file_path);
            continue;
        }

        if (strcmp(magic, AUTOSAVE_MAGIC) != 0) {
            fclose(file);
            g_free(file_path);
            continue;
        }

        /* Read version */
        guint32 version;
        if (fread(&version, sizeof(guint32), 1, file) != 1) {
            fclose(file);
            g_free(file_path);
            continue;
        }

        if (version != AUTOSAVE_VERSION) {
            fclose(file);
            g_free(file_path);
            continue;
        }

        /* Read timestamp */
        time_t timestamp;
        if (fread(&timestamp, sizeof(time_t), 1, file) != 1) {
            fclose(file);
            g_free(file_path);
            continue;
        }

        /* Read dimensions */
        guint32 width, height;
        if (fread(&width, sizeof(guint32), 1, file) != 1) {
            fclose(file);
            g_free(file_path);
            continue;
        }
        if (fread(&height, sizeof(guint32), 1, file) != 1) {
            fclose(file);
            g_free(file_path);
            continue;
        }

        /* Read original path */
        gchar* original_path = read_string(file);
        fclose(file);

        /* Create recovery entry */
        AutosaveRecoveryEntry* entry = g_malloc(sizeof(AutosaveRecoveryEntry));
        entry->autosave_path = file_path;
        entry->original_path = original_path ? original_path : g_strdup("");
        entry->timestamp = timestamp;
        entry->width = width;
        entry->height = height;

        /* Count layers by reading through file */
        file = g_fopen(file_path, "rb");
        if (file) {
            /* Skip header */
            fseek(file, 12 + sizeof(guint32) + sizeof(time_t) + sizeof(guint32) * 2, SEEK_SET);

            /* Skip original path */
            guint32 path_len;
            if (fread(&path_len, sizeof(guint32), 1, file) == 1 && path_len > 0) {
                fseek(file, path_len, SEEK_CUR);
            }

            /* Read layer count */
            guint32 layer_count = 0;
            if (fread(&layer_count, sizeof(guint32), 1, file) == 1) {
                entry->layer_count = layer_count;
            } else {
                entry->layer_count = 0;
            }

            fclose(file);
        } else {
            entry->layer_count = 0;
        }

        recovery_list = g_list_prepend(recovery_list, entry);
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
