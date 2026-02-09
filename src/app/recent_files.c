#include "app/recent_files.h"
#include "app/settings.h"
#include <glib.h>
#include <time.h>

/* Internal storage */
static GList* g_recent_files = NULL;
static gboolean g_initialized = FALSE;

/**
 * Free a RecentFile structure
 */
static void recent_file_free(RecentFile* rf) {
    if (rf) {
        g_free(rf->path);
        g_free(rf);
    }
}

/**
 * Find a recent file entry by path
 * @param path Absolute path to search for
 * @return Pointer to RecentFile if found, NULL otherwise
 */
static RecentFile* recent_file_find(const gchar* path) {
    if (!path) {
        return NULL;
    }

    for (GList* iter = g_recent_files; iter; iter = iter->next) {
        RecentFile* rf = (RecentFile*)iter->data;
        if (rf && rf->path && g_strcmp0(rf->path, path) == 0) {
            return rf;
        }
    }

    return NULL;
}

/**
 * Convert relative path to absolute path
 */
static gchar* recent_files_make_absolute(const gchar* filepath) {
    if (!filepath) {
        return NULL;
    }

    /* If already absolute, return a copy */
    if (g_path_is_absolute(filepath)) {
        return g_strdup(filepath);
    }

    /* Convert to absolute */
    gchar* cwd = g_get_current_dir();
    gchar* absolute = g_build_filename(cwd, filepath, NULL);
    g_free(cwd);

    return absolute;
}

/* Global Settings pointer for syncing */
static Settings* g_settings = NULL;

/**
 * Set the Settings pointer for syncing
 */
void recent_files_set_settings(void* settings) {
    g_settings = (Settings*)settings;
}

/**
 * Load recent files from persistent storage
 * Loads from Settings XML file via settings_sync_recent_files_to_system()
 */
void recent_files_load(void) {
    /* Settings must be set before loading */
    if (!g_settings) {
        return; /* No settings available, nothing to load */
    }

    /* Sync from settings XML */
    settings_sync_recent_files_to_system(g_settings);
}

/**
 * Save recent files to persistent storage
 * Saves to Settings XML file via settings_sync_recent_files_from_system()
 */
void recent_files_save(void) {
    if (!g_initialized) {
        return;
    }

    /* Settings must be set before saving */
    if (!g_settings) {
        return; /* No settings available, cannot save */
    }

    /* Sync to settings XML */
    settings_sync_recent_files_from_system(g_settings);
}

/**
 * Initialize the recent files system
 * Settings must be set via recent_files_set_settings() before calling this
 */
void recent_files_init(void) {
    if (g_initialized) {
        return; /* Already initialized */
    }

    if (!g_settings) {
        g_warning("recent_files_init() called but settings not set. Call recent_files_set_settings() first.");
        return;
    }

    g_recent_files = NULL;
    /* Mark as initialized before loading so recent_files_add() works during load */
    g_initialized = TRUE;
    recent_files_load();
}

/**
 * Shutdown the recent files system
 * Note: recent_files_save() should be called before this if you want to save
 * This function only cleans up memory
 */
void recent_files_shutdown(void) {
    if (!g_initialized) {
        return;
    }

    /* Don't save here - should be saved before shutdown in main.c */
    /* recent_files_save() is called in main.c before this function */

    /* Free all entries */
    g_list_free_full(g_recent_files, (GDestroyNotify)recent_file_free);
    g_recent_files = NULL;

    /* Clear settings pointer to avoid using freed memory */
    g_settings = NULL;

    g_initialized = FALSE;
}

/**
 * Add a file to the recent files list
 */
void recent_files_add(const gchar* filepath) {
    recent_files_add_with_timestamp(filepath, time(NULL), TRUE);
}

/**
 * Add a file to the recent files list with a specific timestamp
 * Used when loading from settings to preserve original timestamps
 */
void recent_files_add_with_timestamp(const gchar* filepath, time_t timestamp, gboolean check_exists) {
    if (!filepath || !g_initialized) {
        return;
    }

    /* Convert to absolute path */
    gchar* absolute_path = recent_files_make_absolute(filepath);
    if (!absolute_path) {
        return;
    }

    /* Check if file exists (if requested) */
    if (check_exists && !g_file_test(absolute_path, G_FILE_TEST_EXISTS)) {
        g_free(absolute_path);
        return; /* Don't add non-existent files */
    }

    /* Check if already in list */
    RecentFile* existing = recent_file_find(absolute_path);
    if (existing) {
        /* Remove from current position */
        g_recent_files = g_list_remove(g_recent_files, existing);
        /* Update timestamp */
        existing->last_opened = timestamp;
    } else {
        /* Create new entry */
        existing = g_malloc(sizeof(RecentFile));
        existing->path = g_strdup(absolute_path);
        existing->last_opened = timestamp;
    }

    /* Add to front (most recent first) */
    g_recent_files = g_list_prepend(g_recent_files, existing);

    /* Limit to max recent files (from settings if available, else default) */
    guint max_count = g_settings ? settings_get_max_recent_files(g_settings) : MAX_RECENT_FILES;
    while (g_list_length(g_recent_files) > max_count) {
        GList* last = g_list_last(g_recent_files);
        RecentFile* rf = (RecentFile*)last->data;
        g_recent_files = g_list_remove_link(g_recent_files, last);
        g_list_free(last);
        recent_file_free(rf);
    }

    g_free(absolute_path);
}

/**
 * Get the list of recent files
 */
const GList* recent_files_get(void) {
    return g_recent_files;
}

/**
 * Remove a file from the recent files list
 */
void recent_files_remove(const gchar* filepath) {
    if (!filepath || !g_initialized) {
        return;
    }

    /* Convert to absolute path */
    gchar* absolute_path = recent_files_make_absolute(filepath);
    if (!absolute_path) {
        return;
    }

    RecentFile* rf = recent_file_find(absolute_path);
    if (rf) {
        g_recent_files = g_list_remove(g_recent_files, rf);
        recent_file_free(rf);
    }

    g_free(absolute_path);
}

/**
 * Clear all recent files
 */
void recent_files_clear(void) {
    /* Safe to call even if not initialized - just clears the list */
    if (g_recent_files) {
        g_list_free_full(g_recent_files, (GDestroyNotify)recent_file_free);
        g_recent_files = NULL;
    }
}
