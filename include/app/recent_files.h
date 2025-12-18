#ifndef RECENT_FILES_H
#define RECENT_FILES_H

#include <glib.h>
#include <time.h>

/**
 * Maximum number of recent files to track
 */
#define MAX_RECENT_FILES 10

/**
 * Recent file entry structure
 */
typedef struct {
    gchar* path;        /* Absolute path to the file */
    time_t last_opened; /* Timestamp when file was last opened */
} RecentFile;

/**
 * Initialize the recent files system
 * Should be called at application startup after settings are loaded
 * Settings must be set via recent_files_set_settings() before calling this
 */
void recent_files_init(void);

/**
 * Shutdown the recent files system
 * Should be called at application shutdown
 */
void recent_files_shutdown(void);

/**
 * Add a file to the recent files list
 * If the file already exists, it will be moved to the top
 * @param filepath Absolute or relative path to the file
 */
void recent_files_add(const gchar* filepath);

/**
 * Add a file to the recent files list with a specific timestamp
 * Used when loading from settings to preserve original timestamps
 * @param filepath Absolute or relative path to the file
 * @param timestamp The timestamp to use (time_t)
 * @param check_exists If TRUE, only add files that exist. If FALSE, add regardless of existence.
 */
void recent_files_add_with_timestamp(const gchar* filepath, time_t timestamp, gboolean check_exists);

/**
 * Get the list of recent files
 * @return GList of RecentFile* entries (most recent first), or NULL if empty
 *         The list is owned by the recent files module and should not be freed
 */
const GList* recent_files_get(void);

/**
 * Remove a file from the recent files list
 * @param filepath Absolute path to the file to remove
 */
void recent_files_remove(const gchar* filepath);

/**
 * Clear all recent files
 */
void recent_files_clear(void);

/**
 * Load recent files from persistent storage
 * Called automatically by recent_files_init()
 */
void recent_files_load(void);

/**
 * Save recent files to persistent storage
 * Called automatically by recent_files_shutdown()
 */
void recent_files_save(void);

/**
 * Set the Settings pointer for syncing recent files
 * Must be called before recent_files_init() to use XML settings storage
 * @param settings The Settings structure (must not be NULL)
 */
void recent_files_set_settings(void* settings);

#endif /* RECENT_FILES_H */
