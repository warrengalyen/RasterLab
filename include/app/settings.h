#ifndef SETTINGS_H
#define SETTINGS_H

#include <glib.h>

/**
 * Settings structure - holds all application settings
 */
typedef struct {
    /* Canvas background color (0.0-1.0 range) */
    gdouble canvas_bg_r;
    gdouble canvas_bg_g;
    gdouble canvas_bg_b;

    /* Tool options - stored as GHashTable mapping tool_name -> GHashTable(option_name -> value_string) */
    GHashTable* tool_options; /* Key: tool name (gchar*), Value: GHashTable* (option_name -> value_string) */

    /* Recent files list (most recent first) */
    GList* recent_files; /* List of RecentFile* entries (includes path and timestamp) */

    /* Maximum number of recent files */
    guint max_recent_files;

    /* Undo/redo disk settings */
    gint undo_compression_level; /* LZ4 compression level (1-9, default 1) */
    gchar* undo_temp_directory;  /* Root directory for undo journal files (NULL = system temp) */
    gint undo_levels;            /* Number of undo levels to maintain (default 10) */

    /* Performance settings */
    gint worker_threads; /* Number of worker threads for tile compositing (default 4, min 1, max CPU count) */

    /* View settings */
    gboolean show_layer_edges; /* Show outline when moving layers (default TRUE) */
    gboolean show_statusbar;   /* Show status bar (default TRUE) */
} Settings;

/**
 * Load settings from XML file
 * If the file doesn't exist, creates default settings
 * @param app_dir Directory where the application executable is located
 * @return Newly allocated Settings structure, or NULL on error
 *         Caller must free with settings_free()
 */
Settings* settings_load(const char* app_dir);

/**
 * Save settings to XML file
 * @param settings The settings to save
 * @param app_dir Directory where the application executable is located
 * @return TRUE on success, FALSE on failure
 */
gboolean settings_save(Settings* settings, const char* app_dir);

/**
 * Free a Settings structure and all its resources
 * @param settings The settings to free
 */
void settings_free(Settings* settings);

/**
 * Get the executable directory path
 * @return Newly allocated path string (caller must free with g_free)
 *         Returns current directory if executable path cannot be determined
 */
gchar* settings_get_executable_dir(void);

/**
 * Sync recent files from Settings to recent_files.c system
 * Call this after loading settings to populate the recent_files system
 * @param settings The settings structure
 */
void settings_sync_recent_files_to_system(Settings* settings);

/**
 * Sync recent files from recent_files.c system to Settings
 * Call this before saving settings to update Settings with current recent files
 * @param settings The settings structure
 */
void settings_sync_recent_files_from_system(Settings* settings);

/**
 * Set a tool option value
 * @param settings The settings structure
 * @param tool Tool name (e.g., "brush", "eraser")
 * @param key Option name (e.g., "size", "opacity")
 * @param value Option value as string
 */
void settings_set_tool_option(Settings* settings, const char* tool, const char* key, const char* value);

/**
 * Get a tool option value
 * @param settings The settings structure
 * @param tool Tool name (e.g., "brush", "eraser")
 * @param key Option name (e.g., "size", "opacity")
 * @return Option value as string, or NULL if not found
 *         String is owned by Settings, do not free
 */
const char* settings_get_tool_option(Settings* settings, const char* tool, const char* key);

/**
 * Set canvas background color
 * @param settings The settings structure
 * @param r Red component (0.0-1.0)
 * @param g Green component (0.0-1.0)
 * @param b Blue component (0.0-1.0)
 */
void settings_set_canvas_background(Settings* settings, gdouble r, gdouble g, gdouble b);

/**
 * Get canvas background color
 * @param settings The settings structure
 * @param r Output parameter for red component (0.0-1.0)
 * @param g Output parameter for green component (0.0-1.0)
 * @param b Output parameter for blue component (0.0-1.0)
 */
void settings_get_canvas_background(Settings* settings, gdouble* r, gdouble* g, gdouble* b);

/**
 * Get default tool option values
 * These are the default values used when creating new tool options
 */
gfloat settings_get_default_tool_size(void);
gfloat settings_get_default_tool_opacity(void);
gfloat settings_get_default_tool_hardness(void);
gfloat settings_get_default_tool_flow(void);
gfloat settings_get_default_tool_spacing(void);
gfloat settings_get_default_tool_tolerance(void);
gboolean settings_get_default_tool_fill_contiguous(void);
gboolean settings_get_default_tool_fill_antialiased(void);

/**
 * Get undo compression level
 * @param settings The settings structure
 * @return Compression level (1-9)
 */
gint settings_get_undo_compression_level(Settings* settings);

/**
 * Set undo compression level
 * @param settings The settings structure
 * @param level Compression level (1-9, clamped if out of range)
 */
void settings_set_undo_compression_level(Settings* settings, gint level);

/**
 * Get undo temp directory
 * @param settings The settings structure
 * @return Temp directory path, or NULL to use system default
 */
const gchar* settings_get_undo_temp_directory(Settings* settings);

/**
 * Set undo temp directory
 * @param settings The settings structure
 * @param directory Temp directory path (NULL = use system default)
 */
void settings_set_undo_temp_directory(Settings* settings, const gchar* directory);

/**
 * Get undo levels
 * @param settings The settings structure
 * @return Number of undo levels (default 10)
 */
gint settings_get_undo_levels(Settings* settings);

/**
 * Set undo levels
 * @param settings The settings structure
 * @param levels Number of undo levels (clamped to 1-100)
 */
void settings_set_undo_levels(Settings* settings, gint levels);

/**
 * Get worker threads count
 * @param settings The settings structure
 * @return Number of worker threads (default 4)
 */
gint settings_get_worker_threads(Settings* settings);

/**
 * Set worker threads count
 * @param settings The settings structure
 * @param threads Number of worker threads (clamped to 1-CPU count)
 */
void settings_set_worker_threads(Settings* settings, gint threads);

/**
 * Set show layer edges setting
 * @param settings The settings structure
 * @param show TRUE to show layer edges when moving, FALSE to hide
 */
void settings_set_show_layer_edges(Settings* settings, gboolean show);

/**
 * Get show layer edges setting
 * @param settings The settings structure
 * @return TRUE if layer edges should be shown when moving
 */
gboolean settings_get_show_layer_edges(Settings* settings);

/**
 * Set show status bar setting
 * @param settings The settings structure
 * @param show TRUE to show status bar, FALSE to hide
 */
void settings_set_show_statusbar(Settings* settings, gboolean show);

/**
 * Get show status bar setting
 * @param settings The settings structure
 * @return TRUE if status bar should be shown
 */
gboolean settings_get_show_statusbar(Settings* settings);

#endif /* SETTINGS_H */
