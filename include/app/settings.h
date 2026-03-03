#ifndef SETTINGS_H
#define SETTINGS_H

#include <glib.h>

/**
 * Rendering intent for color management (ICC)
 */
typedef enum {
    CM_RENDERING_INTENT_PERCEPTUAL = 0,
    CM_RENDERING_INTENT_RELATIVE_COLORIMETRIC = 1,
    CM_RENDERING_INTENT_SATURATION = 2,
    CM_RENDERING_INTENT_ABSOLUTE_COLORIMETRIC = 3
} ColorManagementRenderingIntent;

/**
 * Color management display mode
 */
typedef enum {
    CM_MODE_SYSTEM_PROFILE = 0, /* Use current system profile for each display (default) */
    CM_MODE_CUSTOM_PROFILE = 1  /* Use custom ICC/ICM profile per display */
} ColorManagementMode;

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

    /* Maximum number of recent files to track */
    guint max_recent_files;

    /* Undo/redo disk settings */
    gint undo_compression_level; /* LZ4 compression level (1-9, default 1) */
    gchar* undo_temp_directory;  /* Root directory for undo journal files (NULL = system temp) */
    gint undo_levels;            /* Number of undo levels to maintain (default 10) */

    /* Performance settings */
    gint worker_threads; /* Number of worker threads for tile compositing (default 4, min 1, max CPU count) */

    /* File recovery (autosave) interval in seconds (30-2700, default 300) */
    gint file_recovery_interval_seconds;

    /* View settings */
    gboolean show_layer_edges; /* Show outline when moving layers (default TRUE) */
    gboolean show_statusbar;   /* Show status bar (default TRUE) */
    gboolean show_rulers;      /* Show canvas rulers (default TRUE) */
    gboolean show_gpu_stats;   /* Show GPU compositor statistics overlay (default FALSE) */

    /* Alpha (transparency) checkerboard: 0=Small (8px), 1=Medium (16px), 2=Large (32px), default 1 */
    gint alpha_check_size;
    /* Alpha check colors (0.0-1.0). Default one: white, two: rgb(204,204,204) */
    gdouble alpha_color_one_r;
    gdouble alpha_color_one_g;
    gdouble alpha_color_one_b;
    gdouble alpha_color_two_r;
    gdouble alpha_color_two_g;
    gdouble alpha_color_two_b;

    /* Tone mapping settings */
    gboolean tone_map_auto_apply;      /* Auto-apply tone mapping settings (bypass dialog) */
    gint tone_map_operator;            /* Tone mapping operator (0=linear, 1=filmic, 2=drago, 3=reinhard) */
    gint tone_map_normalize;           /* Normalization mode (0=none, 1=visible, 2=full) */
    gdouble tone_map_gamma;            /* Gamma value (1.00-5.00, default 2.20) */
    gdouble tone_map_exposure;         /* Exposure value (0.01-8.00, default varies) */
    gdouble tone_map_white_point;      /* White point (1.00-40.00, default 11.20, Filmic only) */
    gdouble tone_map_intensity;        /* Intensity (-4.00-4.00, default 0.00, Reinhard only) */
    gdouble tone_map_adaptation;       /* Adaptation (0.00-1.00, default 1.00, Reinhard only) */
    gdouble tone_map_color_correction; /* Color correction (0.00-1.00, default 0.00, Reinhard only) */

    /* GPU acceleration settings */
    gboolean gpu_acceleration_enabled; /* Enable GPU-accelerated compositing (default TRUE) */
    gchar* gpu_device_name;            /* GPU device name to use (NULL = system default) */

    /* Color management settings (section "color_management") */
    gint cm_rendering_intent;             /* ColorManagementRenderingIntent (default: RELATIVE_COLORIMETRIC) */
    gboolean cm_black_point_compensation; /* Use black point compensation (default TRUE) */
    gboolean cm_use_embedded_icc;         /* Use embedded ICC profiles in files when available (default TRUE) */
    gint cm_mode;                         /* ColorManagementMode (default: SYSTEM_PROFILE) */
    GHashTable* cm_display_profiles;      /* When mode=CUSTOM: display_id (gchar*) -> profile path (gchar*). NULL if unused. */
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
 * Get file recovery (autosave) interval in seconds
 * @param settings The settings structure
 * @return Interval in seconds (30-2700)
 */
gint settings_get_file_recovery_interval_seconds(Settings* settings);

/**
 * Set file recovery (autosave) interval in seconds
 * @param settings The settings structure
 * @param seconds Interval in seconds (clamped to 30-2700)
 */
void settings_set_file_recovery_interval_seconds(Settings* settings, gint seconds);

/**
 * Get maximum number of recent files to track
 * @param settings The settings structure
 * @return Max recent files (1-32)
 */
guint settings_get_max_recent_files(Settings* settings);

/**
 * Set maximum number of recent files to track
 * @param settings The settings structure
 * @param max_count Maximum count (clamped to 1-32)
 */
void settings_set_max_recent_files(Settings* settings, guint max_count);

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

/**
 * Set show rulers setting
 * @param settings The settings structure
 * @param show TRUE to show canvas rulers, FALSE to hide
 */
void settings_set_show_rulers(Settings* settings, gboolean show);

/**
 * Get show rulers setting
 * @param settings The settings structure
 * @return TRUE if canvas rulers should be shown
 */
gboolean settings_get_show_rulers(Settings* settings);

/**
 * Set show GPU stats setting
 * @param settings The settings structure
 * @param show TRUE to show GPU stats overlay, FALSE to hide
 */
void settings_set_show_gpu_stats(Settings* settings, gboolean show);

/**
 * Get show GPU stats setting
 * @param settings The settings structure
 * @return TRUE if GPU stats overlay should be shown
 */
gboolean settings_get_show_gpu_stats(Settings* settings);

/**
 * Alpha checkerboard: size (0=Small/8px, 1=Medium/16px, 2=Large/32px)
 */
void settings_set_alpha_check_size(Settings* settings, gint size);
gint settings_get_alpha_check_size(Settings* settings);

/**
 * Alpha checkerboard: color one and color two (0.0-1.0 RGB)
 */
void settings_set_alpha_color_one(Settings* settings, gdouble r, gdouble g, gdouble b);
void settings_get_alpha_color_one(Settings* settings, gdouble* r, gdouble* g, gdouble* b);
void settings_set_alpha_color_two(Settings* settings, gdouble r, gdouble g, gdouble b);
void settings_get_alpha_color_two(Settings* settings, gdouble* r, gdouble* g, gdouble* b);

/**
 * Tone mapping settings getters/setters
 */
void settings_set_tone_map_auto_apply(Settings* settings, gboolean auto_apply);
gboolean settings_get_tone_map_auto_apply(Settings* settings);
void settings_set_tone_map_operator(Settings* settings, gint operator);
gint settings_get_tone_map_operator(Settings* settings);
void settings_set_tone_map_normalize(Settings* settings, gint normalize);
gint settings_get_tone_map_normalize(Settings* settings);
void settings_set_tone_map_gamma(Settings* settings, gdouble gamma);
gdouble settings_get_tone_map_gamma(Settings* settings);
void settings_set_tone_map_exposure(Settings* settings, gdouble exposure);
gdouble settings_get_tone_map_exposure(Settings* settings);
void settings_set_tone_map_white_point(Settings* settings, gdouble white_point);
gdouble settings_get_tone_map_white_point(Settings* settings);
void settings_set_tone_map_intensity(Settings* settings, gdouble intensity);
gdouble settings_get_tone_map_intensity(Settings* settings);
void settings_set_tone_map_adaptation(Settings* settings, gdouble adaptation);
gdouble settings_get_tone_map_adaptation(Settings* settings);
void settings_set_tone_map_color_correction(Settings* settings, gdouble color_correction);
gdouble settings_get_tone_map_color_correction(Settings* settings);

/**
 * GPU acceleration settings getters/setters
 */

/**
 * Get GPU acceleration enabled setting
 * @param settings The settings structure
 * @return TRUE if GPU acceleration is enabled
 */
gboolean settings_get_gpu_acceleration_enabled(Settings* settings);

/**
 * Set GPU acceleration enabled setting
 * @param settings The settings structure
 * @param enabled TRUE to enable GPU acceleration, FALSE to disable
 */
void settings_set_gpu_acceleration_enabled(Settings* settings, gboolean enabled);

/**
 * Get GPU device name
 * @param settings The settings structure
 * @return GPU device name, or NULL for system default
 *         String is owned by Settings, do not free
 */
const gchar* settings_get_gpu_device_name(Settings* settings);

/**
 * Set GPU device name
 * @param settings The settings structure
 * @param device_name GPU device name (NULL = use system default)
 */
void settings_set_gpu_device_name(Settings* settings, const gchar* device_name);

/**
 * Color management settings getters/setters
 * (Not yet exposed in the settings dialog.)
 */

/** Rendering intent (perceptual, relative colorimetric, saturation, absolute colorimetric). Default: relative colorimetric. */
void settings_set_cm_rendering_intent(Settings* settings, gint intent);
gint settings_get_cm_rendering_intent(Settings* settings);

/** Use black point compensation. Default: TRUE. */
void settings_set_cm_black_point_compensation(Settings* settings, gboolean use);
gboolean settings_get_cm_black_point_compensation(Settings* settings);

/** Use embedded ICC profiles in files when available. Default: TRUE. */
void settings_set_cm_use_embedded_icc(Settings* settings, gboolean use);
gboolean settings_get_cm_use_embedded_icc(Settings* settings);

/** Color management mode: system profile per display (0) or custom profile per display (1). Default: 0. */
void settings_set_cm_mode(Settings* settings, gint mode);
gint settings_get_cm_mode(Settings* settings);

/** Set custom ICC/ICM profile path for a display (used when mode is CUSTOM). display_id is e.g. monitor name or ID. */
void settings_set_cm_display_profile(Settings* settings, const gchar* display_id, const gchar* profile_path);

/** Get custom profile path for a display. Returns NULL if not set or mode is not CUSTOM. Caller must not free.
 *  When no system profile is available and no ICC/ICM path is set for the display, use default sRGB (no transform). */
const gchar* settings_get_cm_display_profile(Settings* settings, const gchar* display_id);

#endif /* SETTINGS_H */
