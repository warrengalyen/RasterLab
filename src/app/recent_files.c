#include "app/recent_files.h"
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
 * Get the path to the recent files storage file
 * Stores in the same directory as the application executable
 * @return Newly allocated path string (caller must free with g_free)
 */
static gchar* recent_files_get_storage_path(void) {
    /* Get the directory where the application is running from */
    gchar* app_dir = g_get_current_dir();
    gchar* file_path = g_build_filename(app_dir, "recent_files.txt", NULL);

    g_free(app_dir);

    return file_path;
}

/**
 * Ensure the application directory exists (for storing recent_files.txt)
 * Note: The directory should already exist since it's where the app runs from
 */
static void recent_files_ensure_config_dir(void) {
    /* No need to create directory - it's the current working directory where app runs */
    /* If we need to ensure it exists, we could check here, but it should always exist */
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

/**
 * Load recent files from persistent storage
 */
void recent_files_load(void) {
    gchar* file_path = recent_files_get_storage_path();
    GError* error = NULL;
    gchar* contents = NULL;
    gsize length = 0;

    if (!g_file_get_contents(file_path, &contents, &length, &error)) {
        /* File doesn't exist yet, that's okay */
        if (error) {
            g_error_free(error);
        }
        g_free(file_path);
        return;
    }

    /* Parse file line by line */
    gchar** lines = g_strsplit(contents, "\n", -1);
    g_free(contents);

    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = lines[i];

        /* Skip empty lines */
        if (!line || strlen(line) == 0) {
            continue;
        }

        /* Remove trailing newline if present */
        g_strchomp(line);

        if (strlen(line) == 0) {
            continue; /* Skip empty lines after trimming */
        }

        /* Parse line: path|timestamp */
        gchar* pipe = strchr(line, '|');
        if (!pipe) {
            continue; /* Skip malformed lines */
        }

        *pipe = '\0';
        gchar* path = line;
        gchar* timestamp_str = pipe + 1;

        /* Validate path */
        if (!path || strlen(path) == 0) {
            continue;
        }

        /* Parse timestamp */
        time_t timestamp = (time_t)strtol(timestamp_str, NULL, 10);
        if (timestamp == 0 && errno == EINVAL) {
            continue; /* Invalid timestamp */
        }

        /* Create recent file entry */
        RecentFile* rf = g_malloc(sizeof(RecentFile));
        rf->path = g_strdup(path);
        rf->last_opened = timestamp;

        /* Add to list (most recent first) */
        g_recent_files = g_list_prepend(g_recent_files, rf);
    }

    g_strfreev(lines);
    g_free(file_path);
}

/**
 * Save recent files to persistent storage
 */
void recent_files_save(void) {
    if (!g_initialized) {
        return;
    }

    recent_files_ensure_config_dir();

    gchar* file_path = recent_files_get_storage_path();
    FILE* file = g_fopen(file_path, "w");

    if (!file) {
        g_warning("Failed to open recent files storage for writing: %s", file_path);
        g_free(file_path);
        return;
    }

    /* Write entries in reverse order (oldest first) so loading restores correct order */
    GList* reversed = g_list_reverse(g_list_copy(g_recent_files));

    for (GList* iter = reversed; iter; iter = iter->next) {
        RecentFile* rf = (RecentFile*)iter->data;
        if (rf && rf->path) {
            fprintf(file, "%s|%ld\n", rf->path, (long)rf->last_opened);
        }
    }

    /* Free the copied list (but not the data, as it's still in g_recent_files) */
    g_list_free(reversed);

    fclose(file);
    g_free(file_path);
}

/**
 * Initialize the recent files system
 */
void recent_files_init(void) {
    if (g_initialized) {
        return; /* Already initialized */
    }

    g_recent_files = NULL;
    recent_files_load();
    g_initialized = TRUE;
}

/**
 * Shutdown the recent files system
 */
void recent_files_shutdown(void) {
    if (!g_initialized) {
        return;
    }

    recent_files_save();

    /* Free all entries */
    g_list_free_full(g_recent_files, (GDestroyNotify)recent_file_free);
    g_recent_files = NULL;

    g_initialized = FALSE;
}

/**
 * Add a file to the recent files list
 */
void recent_files_add(const gchar* filepath) {
    if (!filepath || !g_initialized) {
        return;
    }

    /* Convert to absolute path */
    gchar* absolute_path = recent_files_make_absolute(filepath);
    if (!absolute_path) {
        return;
    }

    /* Check if file exists */
    if (!g_file_test(absolute_path, G_FILE_TEST_EXISTS)) {
        g_free(absolute_path);
        return; /* Don't add non-existent files */
    }

    /* Check if already in list */
    RecentFile* existing = recent_file_find(absolute_path);
    if (existing) {
        /* Remove from current position */
        g_recent_files = g_list_remove(g_recent_files, existing);
        /* Update timestamp */
        existing->last_opened = time(NULL);
    } else {
        /* Create new entry */
        existing = g_malloc(sizeof(RecentFile));
        existing->path = g_strdup(absolute_path);
        existing->last_opened = time(NULL);
    }

    /* Add to front (most recent first) */
    g_recent_files = g_list_prepend(g_recent_files, existing);

    /* Limit to MAX_RECENT_FILES */
    if (g_list_length(g_recent_files) > MAX_RECENT_FILES) {
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
    if (!g_initialized) {
        return;
    }

    g_list_free_full(g_recent_files, (GDestroyNotify)recent_file_free);
    g_recent_files = NULL;
}
