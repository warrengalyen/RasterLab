#include "undo/undo_disk.h"
#include "command.h"
#include "debug_logger.h"
#include "document.h"
#include "render/layer.h"
#include "render/tile.h"
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <lz4.h>
#include <lz4hc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

/**
 * Helper function to open a file with proper binary mode settings
 * Sets up proper buffering to avoid FILE* stream issues on Windows
 */
static FILE* open_binary_file(const char* path, const char* mode) {
    FILE* file = fopen(path, mode);
    if (!file) {
        return NULL;
    }

#ifdef _WIN32
    /* On Windows, ensure binary mode is set */
    _setmode(_fileno(file), _O_BINARY);
#endif

    /* Set up full buffering with a reasonable buffer size (64KB)
     * This helps avoid FILE* stream corruption issues, especially on Windows
     * where mixing read/write operations can cause buffering problems 
     * NULL lets the system allocate the buffer */
    if (setvbuf(file, NULL, _IOFBF, 65536) != 0) {
        /* If setvbuf fails, continue anyway - not critical */
        g_debug("setvbuf() failed for '%s', using default buffering", path);
    }

    return file;
}

/**
 * Calculate simple checksum for journal entry validation
 * Uses a simple additive checksum (CRC32 would be better but requires full table)
 */
static uint64_t compute_checksum(const uint8_t* data, size_t len) {
    uint64_t checksum = 0;
    for (size_t i = 0; i < len; i++) {
        checksum = (checksum * 31) + data[i]; /* Simple polynomial hash */
    }
    return checksum;
}

/**
 * Get default temporary directory
 */
gchar* undo_journal_get_default_temp_dir(void) {
    const gchar* tmp_dir = g_get_tmp_dir();
    if (tmp_dir) {
        return g_strdup(tmp_dir);
    }
    /* Fallback */
#ifdef _WIN32
    return g_strdup("C:\\Temp");
#else
    return g_strdup("/tmp");
#endif
}

/**
 * Generate journal file path for a document
 */
static gchar* generate_journal_path(struct ImageDocument* doc, const gchar* temp_dir) {
    gchar* journal_name;
    gchar* journal_path;

    if (!doc) {
        return NULL;
    }

    /* Generate unique journal filename based on document */
    if (doc->file_path && doc->file_path[0] != '\0') {
        /* Use document filename as base */
        gchar* basename = g_path_get_basename(doc->file_path);
        gchar* name_without_ext = g_strdup(basename);
        g_free(basename);

        /* Remove extension if present */
        gchar* dot = strrchr(name_without_ext, '.');
        if (dot) {
            *dot = '\0';
        }

        journal_name = g_strdup_printf("%s.undo", name_without_ext);
        g_free(name_without_ext);
    } else {
        /* Untitled document - use timestamp */
        time_t now = time(NULL);
        journal_name = g_strdup_printf("untitled_%lu.undo", (unsigned long)now);
    }

    /* Use settings temp_dir if non-empty; otherwise use system temp */
    if (temp_dir && temp_dir[0] != '\0') {
        journal_path = g_build_filename(temp_dir, journal_name, NULL);
    } else {
        gchar* default_temp = undo_journal_get_default_temp_dir();
        journal_path = g_build_filename(default_temp, journal_name, NULL);
        g_free(default_temp);
    }

    g_free(journal_name);
    return journal_path;
}

/**
 * Create a new undo journal
 */
UndoJournal* undo_journal_create(struct ImageDocument* doc,
                                 const gchar* temp_dir,
                                 gint compression_level) {
    UndoJournal* journal;
    gchar* journal_path;

    if (!doc) {
        return NULL;
    }

    /* Validate compression level */
    if (compression_level < 1) {
        compression_level = 1;
    } else if (compression_level > 9) {
        compression_level = 9;
    }

    journal = (UndoJournal*)g_malloc0(sizeof(UndoJournal));
    if (!journal) {
        return NULL;
    }

    /* Generate journal file path */
    journal_path = generate_journal_path(doc, temp_dir);
    if (!journal_path) {
        g_free(journal);
        return NULL;
    }

    journal->journal_path = journal_path;
    journal->next_entry_id = 1;
    journal->undo_indices = NULL;
    journal->redo_indices = NULL;
    journal->total_size = 0;
    journal->max_size_mb = 0; /* Unlimited by default */
    journal->max_entries = 0; /* Unlimited by default */
    journal->compression_level = compression_level;

    /* Ensure temporary directory exists */
    gchar* temp_dir_to_check = g_path_get_dirname(journal_path);
    if (!g_file_test(temp_dir_to_check, G_FILE_TEST_IS_DIR)) {
        g_warning("Temporary directory does not exist: %s", temp_dir_to_check);
        g_free(temp_dir_to_check);
        g_free(journal_path);
        g_free(journal);
        return NULL;
    }
    g_free(temp_dir_to_check);

    /* Open journal file in append mode (create if doesn't exist) */
    journal->journal_file = open_binary_file(journal_path, "ab+");
    if (!journal->journal_file) {
        g_warning("Failed to open undo journal file: %s (errno=%d: %s)",
                  journal_path, errno, strerror(errno));
        g_warning("Please check that the temporary directory exists and is writable");
        g_free(journal_path);
        g_free(journal);
        return NULL;
    }

    /* Get current file size */
    fseek(journal->journal_file, 0, SEEK_END);
    journal->total_size = ftell(journal->journal_file);
    fseek(journal->journal_file, 0, SEEK_SET);

    /* Validate and recover journal on startup */
    if (journal->total_size > 0) {
        if (!undo_journal_validate_and_recover(journal)) {
            g_warning("Journal validation failed, starting with empty journal");
            journal->total_size = 0;
            fseek(journal->journal_file, 0, SEEK_SET);
        }
    }

    return journal;
}

/**
 * Free an undo journal
 * This is called when a document is closed or when the application shuts down.
 * The journal file is deleted from disk to prevent accumulation of temporary files.
 */
void undo_journal_free(UndoJournal* journal) {
    if (!journal) {
        return;
    }

    /* Close journal file - ensure it's flushed and closed before deletion */
    if (journal->journal_file) {
        /* Flush any pending writes */
        fflush(journal->journal_file);
        /* Close the file handle */
        fclose(journal->journal_file);
        journal->journal_file = NULL;
    }

    /* Delete journal file from disk */
    if (journal->journal_path) {
        /* Attempt to delete the journal file */
        if (g_file_test(journal->journal_path, G_FILE_TEST_EXISTS)) {
            if (g_unlink(journal->journal_path) != 0) {
                /* Failed to delete - log warning but continue cleanup */
                /* On Windows, files may be locked briefly after closing, but this is rare */
                g_warning("Failed to delete undo journal file: %s (%s)",
                          journal->journal_path, strerror(errno));
            }
        }
        g_free(journal->journal_path);
        journal->journal_path = NULL;
    }

    /* Free all entry indices */
    if (journal->undo_indices) {
        for (GList* iter = journal->undo_indices; iter; iter = iter->next) {
            undo_entry_index_free((UndoEntryIndex*)iter->data);
        }
        g_list_free(journal->undo_indices);
    }

    if (journal->redo_indices) {
        for (GList* iter = journal->redo_indices; iter; iter = iter->next) {
            undo_entry_index_free((UndoEntryIndex*)iter->data);
        }
        g_list_free(journal->redo_indices);
    }

    g_free(journal);
}

/**
 * Free an undo entry index
 */
void undo_entry_index_free(UndoEntryIndex* entry_index) {
    if (!entry_index) {
        return;
    }

    if (entry_index->command_name) {
        g_free(entry_index->command_name);
    }

    g_free(entry_index);
}

/**
 * Serialize tile deltas to a buffer
 * Returns allocated buffer and size, caller must free
 */
static gboolean serialize_tile_deltas(TileUndoCommandData* data,
                                      uint8_t** out_buffer,
                                      size_t* out_size) {
    uint8_t* buffer;
    size_t offset = 0;
    size_t total_size = 0;
    guint i;

    if (!data || !data->tile_deltas || data->tile_deltas->len == 0) {
        return FALSE;
    }

    /* Calculate total size needed */
    for (i = 0; i < data->tile_deltas->len; i++) {
        TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
        if (delta && delta->before && delta->after) {
            gint before_w = cairo_image_surface_get_width(delta->before);
            gint before_h = cairo_image_surface_get_height(delta->before);
            gint after_w = cairo_image_surface_get_width(delta->after);
            gint after_h = cairo_image_surface_get_height(delta->after);

            size_t before_size = before_w * before_h * 4; /* ARGB32 = 4 bytes per pixel */
            size_t after_size = after_w * after_h * 4;

            total_size += sizeof(SerializedTileDelta) + before_size + after_size;
        }
    }

    if (total_size == 0) {
        return FALSE;
    }

    /* Allocate buffer */
    buffer = (uint8_t*)g_malloc(total_size);
    if (!buffer) {
        return FALSE;
    }

    /* Serialize each tile delta */
    for (i = 0; i < data->tile_deltas->len; i++) {
        TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
        if (!delta || !delta->before || !delta->after) {
            continue;
        }

        gint before_w = cairo_image_surface_get_width(delta->before);
        gint before_h = cairo_image_surface_get_height(delta->before);
        gint after_w = cairo_image_surface_get_width(delta->after);
        gint after_h = cairo_image_surface_get_height(delta->after);

        size_t before_size = before_w * before_h * 4;
        size_t after_size = after_w * after_h * 4;

        /* Write SerializedTileDelta header */
        SerializedTileDelta* serialized = (SerializedTileDelta*)(buffer + offset);
        serialized->tile_x = delta->tile_x;
        serialized->tile_y = delta->tile_y;
        serialized->tile_width = (guint)before_w; /* Use before dimensions */
        serialized->tile_height = (guint)before_h;
        serialized->before_data_size = (guint)before_size;
        serialized->after_data_size = (guint)after_size;
        offset += sizeof(SerializedTileDelta);

        /* Write BEFORE pixel data */
        cairo_surface_flush(delta->before);
        uint8_t* before_data = cairo_image_surface_get_data(delta->before);
        memcpy(buffer + offset, before_data, before_size);
        offset += before_size;

        /* Write AFTER pixel data */
        cairo_surface_flush(delta->after);
        uint8_t* after_data = cairo_image_surface_get_data(delta->after);
        memcpy(buffer + offset, after_data, after_size);
        offset += after_size;
    }

    *out_buffer = buffer;
    *out_size = total_size;
    return TRUE;
}

/**
 * Write a tile-based undo command to disk
 */
gboolean undo_journal_write_tile_command(UndoJournal* journal, Command* cmd) {
    TileUndoCommandData* data;
    UndoEntryHeader header;
    uint8_t* uncompressed_buffer = NULL;
    size_t uncompressed_size = 0;
    int compressed_bound;
    uint8_t* compressed_buffer = NULL;
    int compressed_size;
    uint64_t file_offset;
    UndoEntryIndex* entry_index;
    gint min_tile_x = INT_MAX, min_tile_y = INT_MAX;
    gint max_tile_x = INT_MIN, max_tile_y = INT_MIN;
    guint i;

    if (!journal || !journal->journal_file || !cmd || !cmd->user_data) {
        return FALSE;
    }

    /* Verify file handle is valid - reopen if necessary */
    if (ferror(journal->journal_file)) {
        g_warning("Journal file '%s' is in error state, reopening file",
                  journal->journal_path ? journal->journal_path : "<unknown>");

        /* Close the bad file handle */
        fclose(journal->journal_file);
        journal->journal_file = NULL;
    }

    /* If file handle is NULL or file doesn't exist, (re)open it */
    if (!journal->journal_file) {
        /* Check if file exists */
        gboolean file_exists = g_file_test(journal->journal_path, G_FILE_TEST_EXISTS);

        /* Reopen in append mode */
        journal->journal_file = open_binary_file(journal->journal_path, "ab+");
        if (!journal->journal_file) {
            g_warning("Failed to reopen journal file '%s': %s (errno=%d)",
                      journal->journal_path, strerror(errno), errno);
            g_warning("File exists: %s", file_exists ? "yes" : "no");
            return FALSE;
        }

        /* Seek to end to continue appending */
        fseek(journal->journal_file, 0, SEEK_END);

        if (file_exists) {
            debug_log("DBG", "Successfully reopened existing journal file '%s'", journal->journal_path);
        } else {
            debug_log("DBG", "Created new journal file '%s' (previous file was deleted)", journal->journal_path);
        }
    }

    data = (TileUndoCommandData*)cmd->user_data;
    if (!data->tile_deltas || data->tile_deltas->len == 0) {
        return FALSE;
    }

    /* Calculate tile bounds */
    gboolean found_tile = FALSE;
    for (i = 0; i < data->tile_deltas->len; i++) {
        TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
        if (delta) {
            found_tile = TRUE;
            if (delta->tile_x < min_tile_x)
                min_tile_x = delta->tile_x;
            if (delta->tile_x > max_tile_x)
                max_tile_x = delta->tile_x;
            if (delta->tile_y < min_tile_y)
                min_tile_y = delta->tile_y;
            if (delta->tile_y > max_tile_y)
                max_tile_y = delta->tile_y;
        }
    }

    if (!found_tile) {
        /* No valid tiles found */
        min_tile_x = min_tile_y = max_tile_x = max_tile_y = 0;
    }

    /* Serialize tile deltas */
    if (!serialize_tile_deltas(data, &uncompressed_buffer, &uncompressed_size)) {
        return FALSE;
    }

    /* Calculate compressed size bound */
    compressed_bound = LZ4_compressBound((int)uncompressed_size);
    compressed_buffer = (uint8_t*)g_malloc(compressed_bound);
    if (!compressed_buffer) {
        g_free(uncompressed_buffer);
        return FALSE;
    }

    /* Compress using LZ4 */
    if (journal->compression_level == 1) {
        /* Fast compression */
        compressed_size = LZ4_compress_default((const char*)uncompressed_buffer,
                                               (char*)compressed_buffer,
                                               (int)uncompressed_size,
                                               compressed_bound);
    } else {
        /* High compression */
        compressed_size = LZ4_compress_HC((const char*)uncompressed_buffer,
                                          (char*)compressed_buffer,
                                          (int)uncompressed_size,
                                          compressed_bound,
                                          journal->compression_level);
    }

    if (compressed_size <= 0) {
        g_warning("LZ4 compression failed");
        g_free(uncompressed_buffer);
        g_free(compressed_buffer);
        return FALSE;
    }

    /* Get current file offset */
    file_offset = ftell(journal->journal_file);
    if (file_offset < 0) {
        g_warning("Failed to get file position for journal '%s': %s (errno=%d)",
                  journal->journal_path, strerror(errno), errno);
        g_free(uncompressed_buffer);
        g_free(compressed_buffer);
        return FALSE;
    }

    /* Prepare header */
    memset(&header, 0, sizeof(header));
    header.magic = UNDO_JOURNAL_MAGIC;
    header.version = UNDO_JOURNAL_VERSION;
    header.entry_id = journal->next_entry_id++;
    header.timestamp = (uint64_t)time(NULL);
    header.operation_type = UNDO_OP_PAINT;
    header.tile_count = data->tile_deltas->len;
    header.compressed_size = (uint32_t)compressed_size;
    header.uncompressed_size = (uint32_t)uncompressed_size;
    header.checksum = compute_checksum(compressed_buffer, compressed_size);

    /* Write header */
    size_t header_written = fwrite(&header, sizeof(header), 1, journal->journal_file);
    int write_errno = errno;
    int file_error = ferror(journal->journal_file);

    if (header_written != 1) {
        g_warning("Failed to write undo entry header to '%s': fwrite returned %zu, errno=%d (%s), ferror=%d, file_offset=%lld",
                  journal->journal_path ? journal->journal_path : "<unknown>",
                  header_written, write_errno, strerror(write_errno), file_error,
                  (long long)file_offset);
        g_free(uncompressed_buffer);
        g_free(compressed_buffer);
        return FALSE;
    }

    /* Write compressed payload */
    size_t payload_written = fwrite(compressed_buffer, compressed_size, 1, journal->journal_file);
    write_errno = errno;
    file_error = ferror(journal->journal_file);

    if (payload_written != 1) {
        g_warning("Failed to write compressed undo entry data to '%s': fwrite returned %zu, errno=%d (%s), ferror=%d",
                  journal->journal_path ? journal->journal_path : "<unknown>",
                  payload_written, write_errno, strerror(write_errno), file_error);
        g_free(uncompressed_buffer);
        g_free(compressed_buffer);
        return FALSE;
    }

    /* Flush to disk for crash safety */
    if (fflush(journal->journal_file) != 0) {
        g_warning("Failed to flush journal file '%s': %s (errno=%d)",
                  journal->journal_path, strerror(errno), errno);
    }

    /* Update journal size - check for errors */
    long new_pos = ftell(journal->journal_file);
    if (new_pos < 0) {
        g_warning("ftell() failed for journal '%s': %s (errno=%d)",
                  journal->journal_path, strerror(errno), errno);
        /* Try to continue anyway, using old size + what we just wrote */
        journal->total_size = file_offset + sizeof(header) + compressed_size;
    } else {
        journal->total_size = (uint64_t)new_pos;
    }

    /* Create entry index */
    entry_index = (UndoEntryIndex*)g_malloc0(sizeof(UndoEntryIndex));
    entry_index->entry_id = header.entry_id;
    entry_index->file_offset = file_offset;
    entry_index->compressed_size = header.compressed_size;
    entry_index->operation_type = header.operation_type;
    entry_index->command_name = g_strdup(cmd->name);
    entry_index->timestamp = header.timestamp;
    entry_index->layer = data->layer;
    entry_index->min_tile_x = min_tile_x;
    entry_index->min_tile_y = min_tile_y;
    entry_index->max_tile_x = max_tile_x;
    entry_index->max_tile_y = max_tile_y;

    /* Free temporary buffers */
    g_free(uncompressed_buffer);
    g_free(compressed_buffer);

    /* Store entry_index in command's user_data */
    TileUndoCommandData* cmd_data = (TileUndoCommandData*)cmd->user_data;
    if (cmd_data) {
        cmd_data->entry_index = entry_index;
        /* Also push to journal's undo_indices for recovery/validation purposes */
        undo_journal_push_undo(journal, entry_index);
    } else {
        /* Command data not found, free entry_index */
        undo_entry_index_free(entry_index);
        return FALSE;
    }

    /* Check if compaction is needed */
    undo_journal_compact_if_needed(journal);

    return TRUE;
}

/**
 * Deserialize tile deltas from a buffer
 */
static gboolean deserialize_tile_deltas(const uint8_t* buffer,
                                        size_t buffer_size,
                                        TileUndoCommandData* data,
                                        gboolean use_before) {
    size_t offset = 0;

    if (!buffer || !data || !data->tile_deltas) {
        return FALSE;
    }

    /* Clear existing deltas (we'll recreate them) */
    for (guint i = 0; i < data->tile_deltas->len; i++) {
        TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
        if (delta) {
            if (delta->before)
                cairo_surface_destroy(delta->before);
            if (delta->after)
                cairo_surface_destroy(delta->after);
            g_free(delta);
        }
    }
    g_ptr_array_set_size(data->tile_deltas, 0);

    /* Deserialize each tile delta until buffer is exhausted */
    while (offset < buffer_size) {
        if (offset + sizeof(SerializedTileDelta) > buffer_size) {
            break;
        }

        SerializedTileDelta* serialized = (SerializedTileDelta*)(buffer + offset);
        offset += sizeof(SerializedTileDelta);

        if (offset + serialized->before_data_size + serialized->after_data_size > buffer_size) {
            break;
        }

        /* Create tile delta */
        TileUndoDelta* delta = (TileUndoDelta*)g_malloc0(sizeof(TileUndoDelta));
        delta->tile_x = serialized->tile_x;
        delta->tile_y = serialized->tile_y;

        /* Create surface for the data we need (before or after) */
        size_t needed_size = use_before ? serialized->before_data_size : serialized->after_data_size;
        const uint8_t* needed_data = buffer + offset + (use_before ? 0 : serialized->before_data_size);

        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                              (int)serialized->tile_width,
                                                              (int)serialized->tile_height);
        if (surface) {
            uint8_t* surface_data = cairo_image_surface_get_data(surface);
            memcpy(surface_data, needed_data, needed_size);
            cairo_surface_mark_dirty(surface);
            cairo_surface_flush(surface);

            if (use_before) {
                delta->before = surface;
                delta->after = NULL;
            } else {
                delta->before = NULL;
                delta->after = surface;
            }
        }

        offset += serialized->before_data_size + serialized->after_data_size;
        g_ptr_array_add(data->tile_deltas, delta);
    }

    return data->tile_deltas->len > 0;
}

/**
 * Read and apply an undo entry from disk
 */
gboolean undo_journal_read_undo(UndoJournal* journal,
                                UndoEntryIndex* entry_index,
                                struct ImageDocument* doc) {
    UndoEntryHeader header;
    uint8_t* compressed_buffer = NULL;
    uint8_t* uncompressed_buffer = NULL;
    int uncompressed_size;
    TileUndoCommandData* data;
    Command* cmd;

    if (!journal || !journal->journal_file || !entry_index || !doc) {
        return FALSE;
    }

    /* Seek to entry */
    if (fseek(journal->journal_file, (long)entry_index->file_offset, SEEK_SET) != 0) {
        return FALSE;
    }

    /* Read header */
    if (fread(&header, sizeof(header), 1, journal->journal_file) != 1) {
        return FALSE;
    }

    /* Validate header */
    if (header.magic != UNDO_JOURNAL_MAGIC || header.version != UNDO_JOURNAL_VERSION) {
        return FALSE;
    }

    if (header.operation_type != UNDO_OP_PAINT) {
        /* Non-pixel operations handled differently */
        return FALSE;
    }

    /* Allocate compressed buffer */
    compressed_buffer = (uint8_t*)g_malloc(header.compressed_size);
    if (!compressed_buffer) {
        return FALSE;
    }

    /* Read compressed data */
    if (fread(compressed_buffer, header.compressed_size, 1, journal->journal_file) != 1) {
        g_free(compressed_buffer);
        return FALSE;
    }

    /* Validate checksum */
    uint64_t computed_checksum = compute_checksum(compressed_buffer, header.compressed_size);
    if (computed_checksum != header.checksum) {
        g_warning("Undo entry checksum mismatch");
        g_free(compressed_buffer);
        return FALSE;
    }

    /* Allocate uncompressed buffer */
    uncompressed_buffer = (uint8_t*)g_malloc(header.uncompressed_size);
    if (!uncompressed_buffer) {
        g_free(compressed_buffer);
        return FALSE;
    }

    /* Decompress */
    uncompressed_size = LZ4_decompress_safe((const char*)compressed_buffer,
                                            (char*)uncompressed_buffer,
                                            (int)header.compressed_size,
                                            (int)header.uncompressed_size);

    if (uncompressed_size != (int)header.uncompressed_size) {
        g_warning("LZ4 decompression failed or size mismatch");
        g_free(compressed_buffer);
        g_free(uncompressed_buffer);
        return FALSE;
    }

    /* Find the layer (may have been deleted, so we need to search) */
    struct ImageLayer* layer = NULL;
    if (entry_index->layer && doc->layers) {
        for (GList* iter = doc->layers; iter; iter = iter->next) {
            if (iter->data == entry_index->layer) {
                layer = entry_index->layer;
                break;
            }
        }
    }

    if (!layer) {
        g_warning("Layer for undo entry no longer exists");
        g_free(compressed_buffer);
        g_free(uncompressed_buffer);
        return FALSE;
    }

    /* Create temporary command data structure */
    data = (TileUndoCommandData*)g_malloc0(sizeof(TileUndoCommandData));
    data->layer = layer;
    data->tile_size = doc->tile_grid ? doc->tile_grid->tile_size : 128;
    data->tile_deltas = g_ptr_array_new();

    /* Deserialize tile deltas (use BEFORE for undo) */
    if (!deserialize_tile_deltas(uncompressed_buffer, header.uncompressed_size, data, TRUE)) {
        g_free(compressed_buffer);
        g_free(uncompressed_buffer);
        g_ptr_array_free(data->tile_deltas, TRUE);
        g_free(data);
        return FALSE;
    }

    /* Apply BEFORE snapshots to layer */
    for (guint i = 0; i < data->tile_deltas->len; i++) {
        TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
        if (delta && delta->before) {
            tile_snapshot_apply(layer->surface,
                                delta->before,
                                delta->tile_x,
                                delta->tile_y,
                                data->tile_size,
                                layer->width,
                                layer->height);
        }
    }

    /* Mark layer cache as dirty */
    layer_invalidate_cache(layer);

    /* Mark composite as dirty and invalidate affected tiles */
    if (doc->tile_grid && layer) {
        for (guint i = 0; i < data->tile_deltas->len; i++) {
            TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
            if (delta) {
                gint doc_x = layer->offset_x + (delta->tile_x * data->tile_size);
                gint doc_y = layer->offset_y + (delta->tile_y * data->tile_size);
                tile_grid_mark_rect_dirty(doc->tile_grid, doc_x, doc_y,
                                          data->tile_size, data->tile_size);
            }
        }
        doc->composite_dirty = TRUE;
    }

    /* Free temporary data */
    for (guint i = 0; i < data->tile_deltas->len; i++) {
        TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
        if (delta) {
            if (delta->before)
                cairo_surface_destroy(delta->before);
            if (delta->after)
                cairo_surface_destroy(delta->after);
            g_free(delta);
        }
    }
    g_ptr_array_free(data->tile_deltas, TRUE);
    g_free(data);

    g_free(compressed_buffer);
    g_free(uncompressed_buffer);

    /* CRITICAL: Seek back to EOF for subsequent appends
     * After reading from the middle of the file, the file pointer is not at EOF.
     * We must restore it to EOF so that subsequent writes append correctly. */
    if (fseek(journal->journal_file, 0, SEEK_END) != 0) {
        g_warning("Failed to seek back to EOF after undo read: %s (errno=%d)",
                  strerror(errno), errno);
        /* Try to recover by clearing the error state */
        clearerr(journal->journal_file);
        fseek(journal->journal_file, 0, SEEK_END);
    }

    return TRUE;
}

/**
 * Read and apply a redo entry from disk
 */
gboolean undo_journal_read_redo(UndoJournal* journal,
                                UndoEntryIndex* entry_index,
                                struct ImageDocument* doc) {
    /* Same as read_undo but uses AFTER snapshots */
    UndoEntryHeader header;
    uint8_t* compressed_buffer = NULL;
    uint8_t* uncompressed_buffer = NULL;
    int uncompressed_size;
    TileUndoCommandData* data;

    if (!journal || !journal->journal_file || !entry_index || !doc) {
        return FALSE;
    }

    /* Seek to entry */
    if (fseek(journal->journal_file, (long)entry_index->file_offset, SEEK_SET) != 0) {
        return FALSE;
    }

    /* Read header */
    if (fread(&header, sizeof(header), 1, journal->journal_file) != 1) {
        return FALSE;
    }

    /* Validate header */
    if (header.magic != UNDO_JOURNAL_MAGIC || header.version != UNDO_JOURNAL_VERSION) {
        return FALSE;
    }

    if (header.operation_type != UNDO_OP_PAINT) {
        return FALSE;
    }

    /* Allocate compressed buffer */
    compressed_buffer = (uint8_t*)g_malloc(header.compressed_size);
    if (!compressed_buffer) {
        return FALSE;
    }

    /* Read compressed data */
    if (fread(compressed_buffer, header.compressed_size, 1, journal->journal_file) != 1) {
        g_free(compressed_buffer);
        return FALSE;
    }

    /* Validate checksum */
    uint64_t computed_checksum = compute_checksum(compressed_buffer, header.compressed_size);
    if (computed_checksum != header.checksum) {
        g_warning("Redo entry checksum mismatch");
        g_free(compressed_buffer);
        return FALSE;
    }

    /* Allocate uncompressed buffer */
    uncompressed_buffer = (uint8_t*)g_malloc(header.uncompressed_size);
    if (!uncompressed_buffer) {
        g_free(compressed_buffer);
        return FALSE;
    }

    /* Decompress */
    uncompressed_size = LZ4_decompress_safe((const char*)compressed_buffer,
                                            (char*)uncompressed_buffer,
                                            (int)header.compressed_size,
                                            (int)header.uncompressed_size);

    if (uncompressed_size != (int)header.uncompressed_size) {
        g_warning("LZ4 decompression failed or size mismatch");
        g_free(compressed_buffer);
        g_free(uncompressed_buffer);
        return FALSE;
    }

    /* Find the layer */
    struct ImageLayer* layer = NULL;
    if (entry_index->layer && doc->layers) {
        for (GList* iter = doc->layers; iter; iter = iter->next) {
            if (iter->data == entry_index->layer) {
                layer = entry_index->layer;
                break;
            }
        }
    }

    if (!layer) {
        g_warning("Layer for redo entry no longer exists");
        g_free(compressed_buffer);
        g_free(uncompressed_buffer);
        return FALSE;
    }

    /* Create temporary command data structure */
    data = (TileUndoCommandData*)g_malloc0(sizeof(TileUndoCommandData));
    data->layer = layer;
    data->tile_size = doc->tile_grid ? doc->tile_grid->tile_size : 128;
    data->tile_deltas = g_ptr_array_new();

    /* Deserialize tile deltas (use AFTER for redo) */
    if (!deserialize_tile_deltas(uncompressed_buffer, header.uncompressed_size, data, FALSE)) {
        g_free(compressed_buffer);
        g_free(uncompressed_buffer);
        g_ptr_array_free(data->tile_deltas, TRUE);
        g_free(data);
        return FALSE;
    }

    /* Apply AFTER snapshots to layer */
    for (guint i = 0; i < data->tile_deltas->len; i++) {
        TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
        if (delta && delta->after) {
            tile_snapshot_apply(layer->surface,
                                delta->after,
                                delta->tile_x,
                                delta->tile_y,
                                data->tile_size,
                                layer->width,
                                layer->height);
        }
    }

    /* Mark layer cache as dirty */
    layer_invalidate_cache(layer);

    /* Mark composite as dirty and invalidate affected tiles */
    if (doc->tile_grid && layer) {
        for (guint i = 0; i < data->tile_deltas->len; i++) {
            TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
            if (delta) {
                gint doc_x = layer->offset_x + (delta->tile_x * data->tile_size);
                gint doc_y = layer->offset_y + (delta->tile_y * data->tile_size);
                tile_grid_mark_rect_dirty(doc->tile_grid, doc_x, doc_y,
                                          data->tile_size, data->tile_size);
            }
        }
        doc->composite_dirty = TRUE;
    }

    /* Free temporary data */
    for (guint i = 0; i < data->tile_deltas->len; i++) {
        TileUndoDelta* delta = (TileUndoDelta*)g_ptr_array_index(data->tile_deltas, i);
        if (delta) {
            if (delta->before)
                cairo_surface_destroy(delta->before);
            if (delta->after)
                cairo_surface_destroy(delta->after);
            g_free(delta);
        }
    }
    g_ptr_array_free(data->tile_deltas, TRUE);
    g_free(data);

    g_free(compressed_buffer);
    g_free(uncompressed_buffer);

    /* CRITICAL: Seek back to EOF for subsequent appends
     * After reading from the middle of the file, the file pointer is not at EOF.
     * We must restore it to EOF so that subsequent writes append correctly. */
    if (fseek(journal->journal_file, 0, SEEK_END) != 0) {
        g_warning("Failed to seek back to EOF after redo read: %s (errno=%d)",
                  strerror(errno), errno);
        /* Try to recover by clearing the error state */
        clearerr(journal->journal_file);
        fseek(journal->journal_file, 0, SEEK_END);
    }

    return TRUE;
}

/**
 * Push an undo entry index to the undo stack
 */
void undo_journal_push_undo(UndoJournal* journal, UndoEntryIndex* entry_index) {
    if (!journal || !entry_index) {
        return;
    }

    journal->undo_indices = g_list_prepend(journal->undo_indices, entry_index);
}

/**
 * Pop an undo entry index from the undo stack
 */
UndoEntryIndex* undo_journal_pop_undo(UndoJournal* journal) {
    if (!journal || !journal->undo_indices) {
        return NULL;
    }

    UndoEntryIndex* entry_index = (UndoEntryIndex*)journal->undo_indices->data;
    journal->undo_indices = g_list_delete_link(journal->undo_indices, journal->undo_indices);
    return entry_index;
}

/**
 * Push an undo entry index to the redo stack
 */
void undo_journal_push_redo(UndoJournal* journal, UndoEntryIndex* entry_index) {
    if (!journal || !entry_index) {
        return;
    }

    journal->redo_indices = g_list_prepend(journal->redo_indices, entry_index);
}

/**
 * Pop an undo entry index from the redo stack
 */
UndoEntryIndex* undo_journal_pop_redo(UndoJournal* journal) {
    if (!journal || !journal->redo_indices) {
        return NULL;
    }

    UndoEntryIndex* entry_index = (UndoEntryIndex*)journal->redo_indices->data;
    journal->redo_indices = g_list_delete_link(journal->redo_indices, journal->redo_indices);
    return entry_index;
}

/**
 * Clear all entries from the redo stack
 */
void undo_journal_clear_redo(UndoJournal* journal) {
    if (!journal) {
        return;
    }

    if (journal->redo_indices) {
        for (GList* iter = journal->redo_indices; iter; iter = iter->next) {
            undo_entry_index_free((UndoEntryIndex*)iter->data);
        }
        g_list_free(journal->redo_indices);
        journal->redo_indices = NULL;
    }
}

void undo_journal_clear_all(UndoJournal* journal) {
    if (!journal) {
        return;
    }

    undo_journal_clear_redo(journal);

    if (journal->undo_indices) {
        for (GList* iter = journal->undo_indices; iter; iter = iter->next) {
            undo_entry_index_free((UndoEntryIndex*)iter->data);
        }
        g_list_free(journal->undo_indices);
        journal->undo_indices = NULL;
    }

    if (journal->journal_file) {
        fflush(journal->journal_file);
        fclose(journal->journal_file);
        journal->journal_file = NULL;
    }

    if (journal->journal_path) {
        journal->journal_file = open_binary_file(journal->journal_path, "wb+");
        if (journal->journal_file) {
            fclose(journal->journal_file);
        }
        journal->journal_file = open_binary_file(journal->journal_path, "ab+");
        if (!journal->journal_file) {
            g_warning("undo_journal_clear_all: failed to reopen journal %s", journal->journal_path);
        }
    }

    journal->total_size = 0;
    journal->next_entry_id = 1;
}

/**
 * Validate and recover journal file
 */
gboolean undo_journal_validate_and_recover(UndoJournal* journal) {
    UndoEntryHeader header;
    uint64_t file_offset = 0;
    GList* valid_indices = NULL;
    uint64_t last_valid_entry_id = 0;

    if (!journal || !journal->journal_file) {
        return FALSE;
    }

    /* Rewind to start */
    fseek(journal->journal_file, 0, SEEK_SET);

    /* Read entries sequentially */
    while (1) {
        file_offset = ftell(journal->journal_file);

        /* Read header */
        if (fread(&header, sizeof(header), 1, journal->journal_file) != 1) {
            /* End of file or read error */
            break;
        }

        /* Validate magic and version */
        if (header.magic != UNDO_JOURNAL_MAGIC || header.version != UNDO_JOURNAL_VERSION) {
            /* Invalid entry, truncate here */
            break;
        }

        /* Validate entry ID is increasing */
        if (header.entry_id <= last_valid_entry_id) {
            /* Invalid entry ID, truncate here */
            break;
        }

        /* Validate compressed size is reasonable */
        if (header.compressed_size == 0 || header.compressed_size > 100 * 1024 * 1024) {
            /* Suspicious size (0 or >100MB), truncate here */
            break;
        }

        /* Validate uncompressed size is reasonable */
        if (header.uncompressed_size == 0 || header.uncompressed_size > 500 * 1024 * 1024) {
            /* Suspicious size (0 or >500MB), truncate here */
            break;
        }

        /* Read and verify checksum of compressed payload */
        uint8_t* compressed_buffer = (uint8_t*)g_malloc(header.compressed_size);
        if (!compressed_buffer) {
            /* Out of memory, truncate here */
            break;
        }

        if (fread(compressed_buffer, header.compressed_size, 1, journal->journal_file) != 1) {
            /* Read failed, truncate here */
            g_free(compressed_buffer);
            break;
        }

        /* Verify checksum */
        uint64_t computed_checksum = compute_checksum(compressed_buffer, header.compressed_size);
        if (computed_checksum != header.checksum) {
            /* Checksum mismatch, truncate here */
            g_free(compressed_buffer);
            break;
        }

        g_free(compressed_buffer);

        /* Entry is valid, create index */
        UndoEntryIndex* entry_index = (UndoEntryIndex*)g_malloc0(sizeof(UndoEntryIndex));
        entry_index->entry_id = header.entry_id;
        entry_index->file_offset = file_offset;
        entry_index->compressed_size = header.compressed_size;
        entry_index->operation_type = header.operation_type;
        entry_index->timestamp = header.timestamp;
        entry_index->command_name = g_strdup("Undo Entry"); /* Will be updated if needed */

        valid_indices = g_list_prepend(valid_indices, entry_index);
        last_valid_entry_id = header.entry_id;
    }

    /* Truncate file at first invalid entry */
    if (file_offset > 0) {
        /* Close and reopen file in write mode to truncate */
        fclose(journal->journal_file);
        journal->journal_file = open_binary_file(journal->journal_path, "rb+");
#ifdef _WIN32
        if (journal->journal_file) {
            _chsize_s(_fileno(journal->journal_file), (long long)file_offset);
        }
#else
        if (journal->journal_file) {
            ftruncate(fileno(journal->journal_file), (off_t)file_offset);
        }
#endif

        /* Close and reopen in append mode for subsequent writes */
        if (journal->journal_file) {
            fclose(journal->journal_file);
        }
        journal->journal_file = open_binary_file(journal->journal_path, "ab+");
        if (!journal->journal_file) {
            g_warning("Failed to reopen journal in append mode after truncation");
            return FALSE;
        }
    }

    /* Update journal state */
    journal->total_size = file_offset;
    if (last_valid_entry_id > 0) {
        journal->next_entry_id = last_valid_entry_id + 1;
    }

    /* Reverse list to get chronological order */
    valid_indices = g_list_reverse(valid_indices);

    /* Free old indices and set new ones */
    if (journal->undo_indices) {
        for (GList* iter = journal->undo_indices; iter; iter = iter->next) {
            undo_entry_index_free((UndoEntryIndex*)iter->data);
        }
        g_list_free(journal->undo_indices);
    }
    journal->undo_indices = valid_indices;

    return TRUE;
}

/**
 * Compact journal file if size limits exceeded
 * Removes oldest entries and rewrites journal file with updated offsets
 */
gboolean undo_journal_compact_if_needed(UndoJournal* journal) {
    if (!journal || !journal->journal_file || !journal->journal_path) {
        return FALSE;
    }

    guint64 max_size_bytes = journal->max_size_mb * 1024ULL * 1024ULL;
    guint entry_count = g_list_length(journal->undo_indices);
    gboolean needs_compaction = FALSE;
    guint entries_to_keep = 0;

    /* Check if compaction is needed */
    if (journal->max_size_mb > 0 && journal->total_size > max_size_bytes) {
        needs_compaction = TRUE;
        /* Keep entries until we're under 80% of max size */
        guint64 target_size = (max_size_bytes * 80) / 100;
        /* Estimate: keep roughly proportional number of entries */
        entries_to_keep = (guint)((entry_count * target_size) / journal->total_size);
        if (entries_to_keep < 10) {
            entries_to_keep = 10; /* Always keep at least 10 entries */
        }
    } else if (journal->max_entries > 0 && entry_count > journal->max_entries) {
        needs_compaction = TRUE;
        /* Keep 80% of max entries */
        entries_to_keep = (journal->max_entries * 80) / 100;
        if (entries_to_keep < 10) {
            entries_to_keep = 10; /* Always keep at least 10 entries */
        }
    }

    if (!needs_compaction || entry_count <= entries_to_keep) {
        return TRUE; /* No compaction needed */
    }

    /* Calculate how many entries to remove */
    guint entries_to_remove = entry_count - entries_to_keep;

    /* Get the list of entries to keep (newest entries) */
    GList* entries_to_keep_list = NULL;
    GList* entries_to_remove_list = NULL;
    GList* current = journal->undo_indices;
    guint kept = 0;

    /* Keep the newest entries (they're in reverse chronological order in the list) */
    while (current && kept < entries_to_keep) {
        entries_to_keep_list = g_list_prepend(entries_to_keep_list, current->data);
        current = current->next;
        kept++;
    }

    /* Collect entries to remove */
    while (current) {
        GList* next = current->next;
        entries_to_remove_list = g_list_prepend(entries_to_remove_list, current->data);
        current = next;
    }

    /* Reverse to get chronological order (oldest first) */
    entries_to_keep_list = g_list_reverse(entries_to_keep_list);

    /* Note: We'll free entries_to_remove_list after compaction succeeds */

    /* Close current journal file */
    fclose(journal->journal_file);
    journal->journal_file = NULL;

    /* Create temporary journal file */
    gchar* temp_path = g_strdup_printf("%s.tmp", journal->journal_path);
    FILE* temp_file = open_binary_file(temp_path, "wb");
    if (!temp_file) {
        g_warning("Failed to create temporary journal file for compaction");
        g_free(temp_path);
        /* Restore original file */
        journal->journal_file = open_binary_file(journal->journal_path, "ab+");
        /* Restore old indices (merge back entries_to_keep_list and entries_to_remove_list) */
        journal->undo_indices = g_list_concat(entries_to_keep_list, entries_to_remove_list);
        return FALSE;
    }

    /* Open original file for reading */
    FILE* old_file = open_binary_file(journal->journal_path, "rb");
    if (!old_file) {
        g_warning("Failed to open journal file for reading during compaction");
        fclose(temp_file);
        g_unlink(temp_path);
        g_free(temp_path);
        journal->journal_file = open_binary_file(journal->journal_path, "ab+");
        g_list_free(entries_to_keep_list);
        return FALSE;
    }

    /* Rewrite entries to temporary file */
    uint64_t new_next_entry_id = journal->next_entry_id;
    GList* new_undo_indices = NULL;

    for (GList* iter = entries_to_keep_list; iter; iter = iter->next) {
        UndoEntryIndex* old_index = (UndoEntryIndex*)iter->data;
        UndoEntryHeader header;
        uint8_t* compressed_buffer = NULL;

        /* Seek to old entry */
        if (fseek(old_file, (long)old_index->file_offset, SEEK_SET) != 0) {
            g_warning("Failed to seek to entry during compaction");
            fclose(old_file);
            fclose(temp_file);
            g_unlink(temp_path);
            g_free(temp_path);
            journal->journal_file = open_binary_file(journal->journal_path, "ab+");
            g_list_free_full(new_undo_indices, (GDestroyNotify)undo_entry_index_free);
            /* Restore old indices */
            journal->undo_indices = g_list_concat(entries_to_keep_list, entries_to_remove_list);
            return FALSE;
        }

        /* Read header */
        if (fread(&header, sizeof(header), 1, old_file) != 1) {
            g_warning("Failed to read entry header during compaction");
            fclose(old_file);
            fclose(temp_file);
            g_unlink(temp_path);
            g_free(temp_path);
            journal->journal_file = open_binary_file(journal->journal_path, "ab+");
            g_list_free_full(new_undo_indices, (GDestroyNotify)undo_entry_index_free);
            /* Restore old indices */
            journal->undo_indices = g_list_concat(entries_to_keep_list, entries_to_remove_list);
            return FALSE;
        }

        /* Read compressed payload */
        compressed_buffer = (uint8_t*)g_malloc(header.compressed_size);
        if (!compressed_buffer) {
            g_warning("Out of memory during compaction");
            fclose(old_file);
            fclose(temp_file);
            g_unlink(temp_path);
            g_free(temp_path);
            journal->journal_file = open_binary_file(journal->journal_path, "ab+");
            g_list_free_full(new_undo_indices, (GDestroyNotify)undo_entry_index_free);
            /* Restore old indices */
            journal->undo_indices = g_list_concat(entries_to_keep_list, entries_to_remove_list);
            return FALSE;
        }

        if (fread(compressed_buffer, header.compressed_size, 1, old_file) != 1) {
            g_warning("Failed to read entry payload during compaction");
            g_free(compressed_buffer);
            fclose(old_file);
            fclose(temp_file);
            g_unlink(temp_path);
            g_free(temp_path);
            journal->journal_file = open_binary_file(journal->journal_path, "ab+");
            g_list_free_full(new_undo_indices, (GDestroyNotify)undo_entry_index_free);
            /* Restore old indices */
            journal->undo_indices = g_list_concat(entries_to_keep_list, entries_to_remove_list);
            return FALSE;
        }

        /* Write to new file at new offset */
        uint64_t new_offset = ftell(temp_file);
        if (fwrite(&header, sizeof(header), 1, temp_file) != 1) {
            g_free(compressed_buffer);
            fclose(old_file);
            fclose(temp_file);
            g_unlink(temp_path);
            g_free(temp_path);
            journal->journal_file = open_binary_file(journal->journal_path, "ab+");
            g_list_free_full(new_undo_indices, (GDestroyNotify)undo_entry_index_free);
            /* Restore old indices */
            journal->undo_indices = g_list_concat(entries_to_keep_list, entries_to_remove_list);
            return FALSE;
        }

        if (fwrite(compressed_buffer, header.compressed_size, 1, temp_file) != 1) {
            g_free(compressed_buffer);
            fclose(old_file);
            fclose(temp_file);
            g_unlink(temp_path);
            g_free(temp_path);
            journal->journal_file = open_binary_file(journal->journal_path, "ab+");
            g_list_free_full(new_undo_indices, (GDestroyNotify)undo_entry_index_free);
            /* Restore old indices */
            journal->undo_indices = g_list_concat(entries_to_keep_list, entries_to_remove_list);
            return FALSE;
        }

        g_free(compressed_buffer);

        /* Create new entry index with updated offset */
        UndoEntryIndex* new_index = (UndoEntryIndex*)g_malloc0(sizeof(UndoEntryIndex));
        new_index->entry_id = header.entry_id;
        new_index->file_offset = new_offset;
        new_index->compressed_size = header.compressed_size;
        new_index->operation_type = header.operation_type;
        new_index->command_name = g_strdup(old_index->command_name ? old_index->command_name : "Undo Entry");
        new_index->timestamp = header.timestamp;
        new_index->layer = old_index->layer;
        new_index->min_tile_x = old_index->min_tile_x;
        new_index->min_tile_y = old_index->min_tile_y;
        new_index->max_tile_x = old_index->max_tile_x;
        new_index->max_tile_y = old_index->max_tile_y;

        new_undo_indices = g_list_append(new_undo_indices, new_index);
    }

    /* Close old file */
    fclose(old_file);

    /* Flush and close temp file */
    fflush(temp_file);
    fclose(temp_file);

    /* Replace old file with new file */
    if (g_file_test(journal->journal_path, G_FILE_TEST_EXISTS)) {
        g_unlink(journal->journal_path);
    }

    if (g_rename(temp_path, journal->journal_path) != 0) {
        g_warning("Failed to replace journal file after compaction");
        g_unlink(temp_path);
        g_free(temp_path);
        journal->journal_file = open_binary_file(journal->journal_path, "ab+");
#ifdef _WIN32
        if (journal->journal_file) {
            _setmode(_fileno(journal->journal_file), _O_BINARY);
        }
#endif
        g_list_free_full(new_undo_indices, (GDestroyNotify)undo_entry_index_free);
        /* Restore old indices (merge back entries_to_keep_list and entries_to_remove_list) */
        journal->undo_indices = g_list_concat(entries_to_keep_list, entries_to_remove_list);
        return FALSE;
    }

    g_free(temp_path);

    /* Open new journal file */
    journal->journal_file = fopen(journal->journal_path, "ab+");
    if (!journal->journal_file) {
        g_warning("Failed to reopen journal file after compaction");
        g_list_free_full(new_undo_indices, (GDestroyNotify)undo_entry_index_free);
        /* Restore old indices (merge back entries_to_keep_list and entries_to_remove_list) */
        journal->undo_indices = g_list_concat(entries_to_keep_list, entries_to_remove_list);
        return FALSE;
    }

#ifdef _WIN32
    /* On Windows, ensure binary mode is set */
    _setmode(_fileno(journal->journal_file), _O_BINARY);
#endif

    /* Update journal state */
    journal->total_size = ftell(journal->journal_file);
    journal->next_entry_id = new_next_entry_id;

    /* Free old indices (we've created new ones with updated offsets) */
    g_list_free_full(entries_to_keep_list, (GDestroyNotify)undo_entry_index_free);
    g_list_free_full(entries_to_remove_list, (GDestroyNotify)undo_entry_index_free);

    /* Set new indices */
    journal->undo_indices = new_undo_indices;

    return TRUE;
}
