#ifndef UNDO_DISK_H
#define UNDO_DISK_H

#include "command.h"
#include "document.h"
#include <glib.h>
#include <stdint.h>

/**
 * Disk-backed undo/redo system
 *
 * This system extends the in-memory undo system to persist undo history
 * to disk using LZ4 compression. Only metadata remains in memory, while
 * tile pixel deltas are stored compressed on disk.
 */

/* Undo journal magic number */
#define UNDO_JOURNAL_MAGIC 0x554E444F /* 'UNDO' in ASCII */
#define UNDO_JOURNAL_VERSION 1

/* Undo operation types */
typedef enum {
    UNDO_OP_PAINT = 0,  /* Brush, eraser, fill operations */
    UNDO_OP_FILTER = 1, /* Filter operations (still use full snapshots) */
    UNDO_OP_MOVE = 2,   /* Move tool (offset-only, no compression) */
    UNDO_OP_LAYER_ADD = 3,
    UNDO_OP_LAYER_DELETE = 4,
    UNDO_OP_LAYER_DUPLICATE = 5,
    UNDO_OP_LAYER_MOVE_UP = 6,
    UNDO_OP_LAYER_MOVE_DOWN = 7,
    UNDO_OP_CANVAS_RESIZE = 8,
    UNDO_OP_FLIP_HORIZONTAL = 9,
    UNDO_OP_FLIP_VERTICAL = 10,
    UNDO_OP_TRANSPOSE = 11,
    UNDO_OP_FIT_ACTIVE_LAYER = 12,
    UNDO_OP_FIT_ALL_LAYERS = 13,
    UNDO_OP_MERGE_VISIBLE = 14,
    UNDO_OP_FLATTEN = 15
} UndoOperationType;

/**
 * Undo entry header (on disk)
 * This structure is written directly to the journal file
 */
typedef struct {
    uint32_t magic;             /* Magic number: 'UNDO' */
    uint32_t version;           /* Journal format version */
    uint64_t entry_id;          /* Unique entry ID (monotonically increasing) */
    uint64_t timestamp;         /* Unix timestamp when entry was created */
    uint32_t operation_type;    /* UndoOperationType */
    uint32_t tile_count;        /* Number of tile deltas (0 for non-pixel ops) */
    uint32_t compressed_size;   /* Size of compressed payload in bytes */
    uint32_t uncompressed_size; /* Size of uncompressed payload in bytes */
    uint32_t reserved[4];       /* Reserved for future use */
    uint64_t checksum;          /* CRC32 checksum of compressed payload */
} UndoEntryHeader;

/**
 * Tile delta data structure (serialized format)
 * This is what gets compressed and written to disk
 */
typedef struct {
    gint tile_x;            /* Tile X coordinate */
    gint tile_y;            /* Tile Y coordinate */
    guint tile_width;       /* Tile width in pixels */
    guint tile_height;      /* Tile height in pixels */
    guint before_data_size; /* Size of BEFORE pixel data in bytes */
    guint after_data_size;  /* Size of AFTER pixel data in bytes */
    /* Followed by: before_data, then after_data (ARGB32 pixel data) */
} SerializedTileDelta;

/**
 * In-memory undo entry index
 * Lightweight metadata kept in RAM
 */
typedef struct _UndoEntryIndex {
    uint64_t entry_id;           /* Entry ID */
    uint64_t file_offset;        /* Offset in journal file where entry starts */
    uint32_t compressed_size;    /* Size of compressed payload */
    uint32_t operation_type;     /* Operation type */
    gchar* command_name;         /* Human-readable command name */
    guint64 timestamp;           /* Timestamp */
    struct ImageLayer* layer;    /* Layer pointer (for validation, may be NULL if layer deleted) */
    gint min_tile_x, min_tile_y; /* Bounding box of affected tiles */
    gint max_tile_x, max_tile_y;
} UndoEntryIndex;

/**
 * Undo journal structure
 * Manages the disk-backed undo journal for a document
 */
typedef struct _UndoJournal {
    gchar* journal_path;    /* Path to journal file */
    FILE* journal_file;     /* File handle (NULL if not open) */
    uint64_t next_entry_id; /* Next entry ID to assign */
    GList* undo_indices;    /* List of UndoEntryIndex* (undo stack) */
    GList* redo_indices;    /* List of UndoEntryIndex* (redo stack) */
    guint64 total_size;     /* Total size of journal file in bytes */
    guint max_size_mb;      /* Maximum journal size in MB (0 = unlimited) */
    guint max_entries;      /* Maximum number of entries (0 = unlimited) */
    gint compression_level; /* LZ4 compression level (1-9, default 1) */
} UndoJournal;

/**
 * Create a new undo journal for a document
 * @param doc The document
 * @param temp_dir Root directory for undo files (NULL = system temp)
 * @param compression_level LZ4 compression level (1-9, default 1)
 * @return New UndoJournal, or NULL on error
 */
UndoJournal* undo_journal_create(struct ImageDocument* doc,
                                 const gchar* temp_dir,
                                 gint compression_level);

/**
 * Free an undo journal and close the journal file
 * @param journal The journal to free
 */
void undo_journal_free(UndoJournal* journal);

/**
 * Write a tile-based undo command to disk
 * Serializes tile deltas, compresses with LZ4, and appends to journal
 * @param journal The undo journal
 * @param cmd The command containing tile deltas
 * @return TRUE on success, FALSE on error
 */
gboolean undo_journal_write_tile_command(UndoJournal* journal, Command* cmd);

/**
 * Read and apply an undo entry from disk (for undo operation)
 * Decompresses entry and applies BEFORE tile states
 * @param journal The undo journal
 * @param entry_index The entry index to read
 * @param doc The document
 * @return TRUE on success, FALSE on error
 */
gboolean undo_journal_read_undo(UndoJournal* journal,
                                UndoEntryIndex* entry_index,
                                struct ImageDocument* doc);

/**
 * Read and apply an undo entry from disk (for redo operation)
 * Decompresses entry and applies AFTER tile states
 * @param journal The undo journal
 * @param entry_index The entry index to read
 * @param doc The document
 * @return TRUE on success, FALSE on error
 */
gboolean undo_journal_read_redo(UndoJournal* journal,
                                UndoEntryIndex* entry_index,
                                struct ImageDocument* doc);

/**
 * Push an undo entry index to the undo stack
 * @param journal The undo journal
 * @param entry_index The entry index to push (ownership transferred)
 */
void undo_journal_push_undo(UndoJournal* journal, UndoEntryIndex* entry_index);

/**
 * Pop an undo entry index from the undo stack
 * @param journal The undo journal
 * @return Entry index, or NULL if stack is empty (ownership transferred to caller)
 */
UndoEntryIndex* undo_journal_pop_undo(UndoJournal* journal);

/**
 * Push an undo entry index to the redo stack
 * @param journal The undo journal
 * @param entry_index The entry index to push (ownership transferred)
 */
void undo_journal_push_redo(UndoJournal* journal, UndoEntryIndex* entry_index);

/**
 * Pop an undo entry index from the redo stack
 * @param journal The undo journal
 * @return Entry index, or NULL if stack is empty (ownership transferred to caller)
 */
UndoEntryIndex* undo_journal_pop_redo(UndoJournal* journal);

/**
 * Clear all entries from the redo stack
 * @param journal The undo journal
 */
void undo_journal_clear_redo(UndoJournal* journal);

/**
 * Validate and recover journal file on startup
 * Scans journal file, validates entries, truncates at first invalid entry
 * @param journal The undo journal
 * @return TRUE on success, FALSE on error
 */
gboolean undo_journal_validate_and_recover(UndoJournal* journal);

/**
 * Compact journal file if size limits exceeded
 * Removes oldest entries and rewrites journal
 * @param journal The undo journal
 * @return TRUE on success, FALSE on error
 */
gboolean undo_journal_compact_if_needed(UndoJournal* journal);

/**
 * Get the default temporary directory for undo files
 * Returns system temp directory (platform-specific)
 * @return Newly allocated path string (caller must free with g_free)
 */
gchar* undo_journal_get_default_temp_dir(void);

/**
 * Free an undo entry index
 * @param entry_index The entry index to free
 */
void undo_entry_index_free(UndoEntryIndex* entry_index);

#endif /* UNDO_DISK_H */
