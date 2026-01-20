#ifndef SWATCHES_H
#define SWATCHES_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/**
 * Swatch data structure
 */
typedef struct {
    GdkRGBA color; /* Color of the swatch */
    gchar* name;   /* Optional color name (can be NULL) */
} SwatchData;

/**
 * Swatches data structure stored in AppContext
 */
typedef struct {
    SwatchData* main_swatches; /* Array of main swatches */
    gint main_swatch_count;    /* Number of main swatches */
    SwatchData* recent_colors; /* Array of recent colors (max 9) */
    gint recent_color_count;   /* Number of recent colors */
} SwatchesData;

/**
 * Initialize swatches data structure
 * @param swatches Pointer to SwatchesData to initialize
 */
void swatches_init(SwatchesData* swatches);

/**
 * Free swatches data structure
 * @param swatches Pointer to SwatchesData to free
 */
void swatches_free(SwatchesData* swatches);

/**
 * Add a swatch to main swatches
 * @param swatches Swatches data
 * @param color Color to add
 * @param name Optional color name (can be NULL)
 */
void swatches_add_main(SwatchesData* swatches, const GdkRGBA* color, const gchar* name);

/**
 * Clear all main swatches
 * @param swatches Swatches data
 */
void swatches_clear_main(SwatchesData* swatches);

/**
 * Add a color to recent colors (up to 9 colors)
 * @param swatches Swatches data
 * @param color Color to add
 */
void swatches_add_recent(SwatchesData* swatches, const GdkRGBA* color);

/**
 * Clear all recent colors
 * @param swatches Swatches data
 */
void swatches_clear_recent(SwatchesData* swatches);

/**
 * Get main swatch count
 * @param swatches Swatches data
 * @return Number of main swatches
 */
gint swatches_get_main_count(const SwatchesData* swatches);

/**
 * Get recent color count
 * @param swatches Swatches data
 * @return Number of recent colors
 */
gint swatches_get_recent_count(const SwatchesData* swatches);

/**
 * Get a main swatch
 * @param swatches Swatches data
 * @param index Index of swatch
 * @param color Output parameter for color
 * @param name Output parameter for name (can be NULL, caller must free if not NULL)
 * @return TRUE if successful, FALSE otherwise
 */
gboolean swatches_get_main(const SwatchesData* swatches, gint index, GdkRGBA* color, gchar** name);

/**
 * Get a recent color
 * @param swatches Swatches data
 * @param index Index of color
 * @param color Output parameter for color
 * @return TRUE if successful, FALSE otherwise
 */
gboolean swatches_get_recent(const SwatchesData* swatches, gint index, GdkRGBA* color);

/**
 * Load swatches from file
 * @param swatches Swatches data to populate
 * @param app_dir Application directory where swatches file is located
 */
void swatches_load(SwatchesData* swatches, const gchar* app_dir);

/**
 * Save swatches to file
 * @param swatches Swatches data to save
 * @param app_dir Application directory where to save swatches file
 */
void swatches_save(const SwatchesData* swatches, const gchar* app_dir);

/**
 * Sync swatches data to widgets
 * @param swatches Swatches data
 * @param main_widget Main swatches widget (can be NULL)
 * @param recent_widget Recent colors widget (can be NULL)
 */
void swatches_sync_to_widgets(const SwatchesData* swatches, GtkWidget* main_widget, GtkWidget* recent_widget);

/**
 * Sync widgets to swatches data
 * @param swatches Swatches data to populate
 * @param main_widget Main swatches widget (can be NULL)
 * @param recent_widget Recent colors widget (can be NULL)
 */
void swatches_sync_from_widgets(SwatchesData* swatches, GtkWidget* main_widget, GtkWidget* recent_widget);

G_END_DECLS

#endif /* SWATCHES_H */
