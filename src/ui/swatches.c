#include "ui/swatches.h"
#include "ui/widgets/swatches_widget.h"
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/**
 * Initialize swatches data structure
 */
void swatches_init(SwatchesData* swatches) {
    if (!swatches) {
        return;
    }

    swatches->main_swatches = NULL;
    swatches->main_swatch_count = 0;
    swatches->recent_colors = NULL;
    swatches->recent_color_count = 0;
}

/**
 * Free swatches data structure
 */
void swatches_free(SwatchesData* swatches) {
    if (!swatches) {
        return;
    }

    /* Free main swatches */
    if (swatches->main_swatches) {
        for (gint i = 0; i < swatches->main_swatch_count; i++) {
            if (swatches->main_swatches[i].name) {
                g_free(swatches->main_swatches[i].name);
            }
        }
        g_free(swatches->main_swatches);
        swatches->main_swatches = NULL;
    }
    swatches->main_swatch_count = 0;

    /* Free recent colors */
    if (swatches->recent_colors) {
        for (gint i = 0; i < swatches->recent_color_count; i++) {
            if (swatches->recent_colors[i].name) {
                g_free(swatches->recent_colors[i].name);
            }
        }
        g_free(swatches->recent_colors);
        swatches->recent_colors = NULL;
    }
    swatches->recent_color_count = 0;
}

/**
 * Add a swatch to main swatches
 */
void swatches_add_main(SwatchesData* swatches, const GdkRGBA* color, const gchar* name) {
    if (!swatches || !color) {
        return;
    }

    swatches->main_swatches = g_realloc(swatches->main_swatches,
                                        sizeof(SwatchData) * (swatches->main_swatch_count + 1));
    swatches->main_swatches[swatches->main_swatch_count].color = *color;
    swatches->main_swatches[swatches->main_swatch_count].name = name ? g_strdup(name) : NULL;
    swatches->main_swatch_count++;
}

/**
 * Clear all main swatches
 */
void swatches_clear_main(SwatchesData* swatches) {
    if (!swatches) {
        return;
    }

    if (swatches->main_swatches) {
        for (gint i = 0; i < swatches->main_swatch_count; i++) {
            if (swatches->main_swatches[i].name) {
                g_free(swatches->main_swatches[i].name);
            }
        }
        g_free(swatches->main_swatches);
        swatches->main_swatches = NULL;
    }
    swatches->main_swatch_count = 0;
}

/**
 * Add a color to recent colors (up to 9 colors)
 */
void swatches_add_recent(SwatchesData* swatches, const GdkRGBA* color) {
    if (!swatches || !color) {
        return;
    }

    /* Check if color already exists */
    gboolean found = FALSE;
    gint found_index = -1;
    for (gint i = 0; i < swatches->recent_color_count; i++) {
        if (fabs(swatches->recent_colors[i].color.red - color->red) < 0.01 &&
            fabs(swatches->recent_colors[i].color.green - color->green) < 0.01 &&
            fabs(swatches->recent_colors[i].color.blue - color->blue) < 0.01) {
            found = TRUE;
            found_index = i;
            break;
        }
    }

    if (found) {
        /* Move found color to front */
        SwatchData temp = swatches->recent_colors[found_index];
        for (gint i = found_index; i > 0; i--) {
            swatches->recent_colors[i] = swatches->recent_colors[i - 1];
        }
        swatches->recent_colors[0] = temp;
    } else {
        /* Add new color at front, remove oldest if at max */
        if (swatches->recent_color_count >= 9) {
            /* Remove oldest (last) */
            if (swatches->recent_colors[8].name) {
                g_free(swatches->recent_colors[8].name);
            }
            /* Shift all down */
            for (gint i = 8; i > 0; i--) {
                swatches->recent_colors[i] = swatches->recent_colors[i - 1];
            }
        } else {
            /* Expand array */
            swatches->recent_colors = g_realloc(swatches->recent_colors,
                                                sizeof(SwatchData) * (swatches->recent_color_count + 1));
            swatches->recent_color_count++;
            /* Shift all down */
            for (gint i = swatches->recent_color_count - 1; i > 0; i--) {
                swatches->recent_colors[i] = swatches->recent_colors[i - 1];
            }
        }
        /* Add new color at front */
        swatches->recent_colors[0].color = *color;
        swatches->recent_colors[0].name = NULL;
    }
}

/**
 * Clear all recent colors
 */
void swatches_clear_recent(SwatchesData* swatches) {
    if (!swatches) {
        return;
    }

    if (swatches->recent_colors) {
        for (gint i = 0; i < swatches->recent_color_count; i++) {
            if (swatches->recent_colors[i].name) {
                g_free(swatches->recent_colors[i].name);
            }
        }
        g_free(swatches->recent_colors);
        swatches->recent_colors = NULL;
    }
    swatches->recent_color_count = 0;
}

/**
 * Get main swatch count
 */
gint swatches_get_main_count(const SwatchesData* swatches) {
    if (!swatches) {
        return 0;
    }
    return swatches->main_swatch_count;
}

/**
 * Get recent color count
 */
gint swatches_get_recent_count(const SwatchesData* swatches) {
    if (!swatches) {
        return 0;
    }
    return swatches->recent_color_count;
}

/**
 * Get a main swatch
 */
gboolean swatches_get_main(const SwatchesData* swatches, gint index, GdkRGBA* color, gchar** name) {
    if (!swatches || !color || index < 0 || index >= swatches->main_swatch_count) {
        return FALSE;
    }

    *color = swatches->main_swatches[index].color;
    if (name) {
        *name = swatches->main_swatches[index].name ? g_strdup(swatches->main_swatches[index].name) : NULL;
    }
    return TRUE;
}

/**
 * Get a recent color
 */
gboolean swatches_get_recent(const SwatchesData* swatches, gint index, GdkRGBA* color) {
    if (!swatches || !color || index < 0 || index >= swatches->recent_color_count) {
        return FALSE;
    }

    *color = swatches->recent_colors[index].color;
    return TRUE;
}

/**
 * Get the swatches file path
 */
static gchar* get_swatches_file_path(const gchar* app_dir) {
    if (!app_dir) {
        return NULL;
    }
    return g_build_filename(app_dir, "rasterlab.swatches", NULL);
}

/**
 * Load swatches from file
 */
void swatches_load(SwatchesData* swatches, const gchar* app_dir) {
    if (!swatches || !app_dir) {
        g_warning("swatches_load: Invalid parameters (swatches=%p, app_dir=%s)", swatches, app_dir ? app_dir : "(null)");
        return;
    }

    gchar* file_path = get_swatches_file_path(app_dir);
    if (!file_path) {
        g_warning("swatches_load: Failed to get file path (app_dir=%s)", app_dir);
        return;
    }

    /* Check if file exists */
    if (!g_file_test(file_path, G_FILE_TEST_EXISTS)) {
        g_debug("swatches_load: File does not exist: %s (using defaults)", file_path);
        g_free(file_path);
        return; /* File doesn't exist, use defaults */
    }

    GError* error = NULL;
    gchar* contents = NULL;
    gsize length = 0;

    if (!g_file_get_contents(file_path, &contents, &length, &error)) {
        if (error) {
            g_warning("swatches_load: Failed to read swatches file: %s", error->message);
            g_error_free(error);
        } else {
            g_warning("swatches_load: Failed to read swatches file: %s (unknown error)", file_path);
        }
        g_free(file_path);
        return;
    }

    /* Clear existing swatches */
    swatches_clear_main(swatches);
    swatches_clear_recent(swatches);

    gint main_loaded = 0;
    gint recent_loaded = 0;

    /* Parse file line by line */
    gchar** lines = g_strsplit(contents, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = g_strstrip(lines[i]);

        /* Skip empty lines and comments */
        if (strlen(line) == 0 || line[0] == '#') {
            continue;
        }

        /* Parse line: [main|recent] hex_color [name] */
        /* Use max_parts=3 to preserve names with spaces */
        gchar** tokens = g_strsplit(line, " ", 3);
        if (!tokens || !tokens[0] || !tokens[1]) {
            g_strfreev(tokens);
            continue;
        }

        gchar* type = tokens[0];
        gchar* hex_color = tokens[1];
        gchar* name = tokens[2] ? g_strdup(tokens[2]) : NULL; /* Duplicate name since tokens will be freed */

        /* Parse hex color */
        if (hex_color[0] != '#' || strlen(hex_color) != 7) {
            if (name) {
                g_free(name);
            }
            g_strfreev(tokens);
            continue;
        }

        guint r, g, b;
        if (sscanf(hex_color, "#%02X%02X%02X", &r, &g, &b) != 3) {
            if (name) {
                g_free(name);
            }
            g_strfreev(tokens);
            continue;
        }

        GdkRGBA color;
        color.red = r / 255.0;
        color.green = g / 255.0;
        color.blue = b / 255.0;
        color.alpha = 1.0;

        /* Add to appropriate list */
        if (g_strcmp0(type, "main") == 0) {
            swatches_add_main(swatches, &color, name); /* name will be duplicated in swatches_add_main */
            if (name) {
                g_free(name); /* Free our copy since swatches_add_main duplicates it */
            }
            main_loaded++;
        } else if (g_strcmp0(type, "recent") == 0) {
            swatches_add_recent(swatches, &color);
            if (name) {
                g_free(name); /* Recent colors don't use names, so free it */
            }
            recent_loaded++;
        } else {
            /* Unknown type, skip but free name */
            if (name) {
                g_free(name);
            }
        }

        g_strfreev(tokens);
    }

    g_strfreev(lines);
    g_free(contents);

    g_free(file_path);
}

/**
 * Save swatches to file
 */
void swatches_save(const SwatchesData* swatches, const gchar* app_dir) {
    if (!swatches || !app_dir) {
        return;
    }

    gchar* file_path = get_swatches_file_path(app_dir);
    if (!file_path) {
        g_warning("swatches_save: Failed to get file path (app_dir=%s)", app_dir);
        return;
    }

    g_debug("swatches_save: Saving to %s", file_path);

    FILE* file = g_fopen(file_path, "w");
    if (!file) {
        g_warning("swatches_save: Failed to open swatches file for writing: %s", file_path);
        g_free(file_path);
        return;
    }

    /* Write header */
    fprintf(file, "# RasterLab Swatches File\n");
    fprintf(file, "# Format: [main|recent] hex_color [name]\n\n");

    /* Save main swatches */
    for (gint i = 0; i < swatches->main_swatch_count; i++) {
        guint8 r = (guint8)(swatches->main_swatches[i].color.red * 255.0);
        guint8 g = (guint8)(swatches->main_swatches[i].color.green * 255.0);
        guint8 b = (guint8)(swatches->main_swatches[i].color.blue * 255.0);
        if (swatches->main_swatches[i].name && strlen(swatches->main_swatches[i].name) > 0) {
            fprintf(file, "main #%02X%02X%02X %s\n", r, g, b, swatches->main_swatches[i].name);
        } else {
            fprintf(file, "main #%02X%02X%02X\n", r, g, b);
        }
    }

    /* Write separator */
    fprintf(file, "\n");

    /* Save recent colors */
    for (gint i = 0; i < swatches->recent_color_count; i++) {
        guint8 r = (guint8)(swatches->recent_colors[i].color.red * 255.0);
        guint8 g = (guint8)(swatches->recent_colors[i].color.green * 255.0);
        guint8 b = (guint8)(swatches->recent_colors[i].color.blue * 255.0);
        fprintf(file, "recent #%02X%02X%02X\n", r, g, b);
    }

    fclose(file);

    g_debug("swatches_save: Saved %d main swatches and %d recent colors to %s",
            swatches->main_swatch_count, swatches->recent_color_count, file_path);

    g_free(file_path);
}

/**
 * Sync swatches data to widgets
 */
void swatches_sync_to_widgets(const SwatchesData* swatches, GtkWidget* main_widget, GtkWidget* recent_widget) {
    if (!swatches) {
        return;
    }

    /* Sync main swatches */
    if (main_widget && SWATCHES_IS_WIDGET(main_widget)) {
        SwatchesWidget* widget = SWATCHES_WIDGET(main_widget);
        swatches_widget_clear(widget);
        for (gint i = 0; i < swatches->main_swatch_count; i++) {
            swatches_widget_add_swatch(widget, &swatches->main_swatches[i].color,
                                       swatches->main_swatches[i].name);
        }
        /* Queue redraw to update the widget */
        gtk_widget_queue_draw(GTK_WIDGET(main_widget));
    }

    /* Sync recent colors */
    if (recent_widget && SWATCHES_IS_WIDGET(recent_widget)) {
        SwatchesWidget* widget = SWATCHES_WIDGET(recent_widget);
        swatches_widget_clear(widget);
        for (gint i = 0; i < swatches->recent_color_count; i++) {
            swatches_widget_add_swatch(widget, &swatches->recent_colors[i].color, NULL);
        }
        /* Queue redraw to update the widget */
        gtk_widget_queue_draw(GTK_WIDGET(recent_widget));
    }
}

/**
 * Sync widgets to swatches data
 */
void swatches_sync_from_widgets(SwatchesData* swatches, GtkWidget* main_widget, GtkWidget* recent_widget) {
    if (!swatches) {
        return;
    }

    /* Sync main swatches */
    if (main_widget && SWATCHES_IS_WIDGET(main_widget)) {
        SwatchesWidget* widget = SWATCHES_WIDGET(main_widget);
        swatches_clear_main(swatches);
        gint count = swatches_widget_get_swatch_count(widget);
        for (gint i = 0; i < count; i++) {
            GdkRGBA color;
            gchar* name = NULL;
            if (swatches_widget_get_swatch(widget, i, &color, &name)) {
                swatches_add_main(swatches, &color, name);
                if (name) {
                    g_free(name);
                }
            }
        }
    }

    /* Sync recent colors */
    if (recent_widget && SWATCHES_IS_WIDGET(recent_widget)) {
        SwatchesWidget* widget = SWATCHES_WIDGET(recent_widget);
        swatches_clear_recent(swatches);
        gint count = swatches_widget_get_swatch_count(widget);
        for (gint i = 0; i < count; i++) {
            GdkRGBA color;
            if (swatches_widget_get_swatch(widget, i, &color, NULL)) {
                swatches_add_recent(swatches, &color);
            }
        }
    }
}
