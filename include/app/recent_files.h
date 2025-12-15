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
 * Should be called at application startup
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

#endif /* RECENT_FILES_H */
