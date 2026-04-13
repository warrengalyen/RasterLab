/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "app/settings.h"
#include "app/recent_files.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlwriter.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "debug_logger.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

/* Default values */
#define DEFAULT_CANVAS_BG_R (160.0 / 255.0)
#define DEFAULT_CANVAS_BG_G (160.0 / 255.0)
#define DEFAULT_CANVAS_BG_B (160.0 / 255.0)
#define DEFAULT_MAX_RECENT_FILES 10
#define MIN_MAX_RECENT_FILES 1
#define MAX_MAX_RECENT_FILES 32
#define DEFAULT_UNDO_COMPRESSION_LEVEL 1           /* LZ4 fast compression */
#define DEFAULT_UNDO_LEVELS 10                     /* Number of undo levels */
#define DEFAULT_WORKER_THREADS 4                   /* Number of worker threads */
#define DEFAULT_FILE_RECOVERY_INTERVAL_SECONDS 300 /* File recovery save interval (30-2700) */
#define DEFAULT_GPU_ACCELERATION TRUE              /* GPU acceleration enabled by default */
#define DEFAULT_SHOW_SMART_GUIDES TRUE             /* View > Show smart guides on by default */
#define DEFAULT_INTERFACE_LOCALE "en_US"
#define DEFAULT_MOUSE_SNAP_DISTANCE 8
#define MIN_MOUSE_SNAP_DISTANCE 1
#define MAX_MOUSE_SNAP_DISTANCE 255

/* Default tool option values */
#define DEFAULT_TOOL_SIZE 5.0f              /* 5px brush size */
#define DEFAULT_TOOL_OPACITY 1.0f           /* 100% opacity */
#define DEFAULT_TOOL_HARDNESS 1.0f          /* Hard edge */
#define DEFAULT_TOOL_FLOW 1.0f              /* Full flow */
#define DEFAULT_TOOL_SPACING 0.25f          /* 25% spacing */
#define DEFAULT_TOOL_TOLERANCE 15.0f        /* 15% tolerance */
#define DEFAULT_TOOL_FILL_CONTIGUOUS TRUE   /* Contiguous fill by default */
#define DEFAULT_TOOL_FILL_ANTIALIASED FALSE /* Hard edges by default */

/**
 * Free a tool options hash table
 */
static void tool_options_hash_table_free(gpointer data) {
    GHashTable* options = (GHashTable*)data;
    if (options) {
        g_hash_table_destroy(options);
    }
}

/**
 * Free a RecentFile structure
 */
static void recent_file_free(gpointer data) {
    RecentFile* rf = (RecentFile*)data;
    if (rf) {
        g_free(rf->path);
        g_free(rf);
    }
}

/**
 * Create default settings
 */
static Settings* settings_create_default(void) {
    Settings* settings = (Settings*)g_malloc(sizeof(Settings));

    settings->canvas_bg_r = DEFAULT_CANVAS_BG_R;
    settings->canvas_bg_g = DEFAULT_CANVAS_BG_G;
    settings->canvas_bg_b = DEFAULT_CANVAS_BG_B;

    settings->tool_options = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, tool_options_hash_table_free);
    settings->recent_files = NULL;
    settings->max_recent_files = DEFAULT_MAX_RECENT_FILES;
    settings->undo_compression_level = DEFAULT_UNDO_COMPRESSION_LEVEL;
    settings->undo_temp_directory = NULL; /* NULL = use system temp directory */
    settings->undo_levels = DEFAULT_UNDO_LEVELS;
    settings->worker_threads = DEFAULT_WORKER_THREADS;
    settings->file_recovery_interval_seconds = DEFAULT_FILE_RECOVERY_INTERVAL_SECONDS;
    settings->show_layer_edges = TRUE; /* Show layer edges by default */
    settings->show_smart_guides = DEFAULT_SHOW_SMART_GUIDES;
    settings->show_statusbar = TRUE;   /* Show status bar by default */
    settings->show_rulers = TRUE;      /* Show canvas rulers by default */
    settings->show_gpu_stats = FALSE;  /* Hide GPU stats by default */

    /* Alpha checkerboard: Medium (16px), white + light gray */
    settings->alpha_check_size = 1; /* 0=Small/8, 1=Medium/16, 2=Large/32 */
    settings->alpha_color_one_r = 1.0;
    settings->alpha_color_one_g = 1.0;
    settings->alpha_color_one_b = 1.0;
    settings->alpha_color_two_r = 204.0 / 255.0;
    settings->alpha_color_two_g = 204.0 / 255.0;
    settings->alpha_color_two_b = 204.0 / 255.0;

    /* Tone mapping defaults */
    settings->tone_map_auto_apply = FALSE;      /* Show dialog by default */
    settings->tone_map_operator = 0;            /* Linear operator */
    settings->tone_map_normalize = 0;           /* No normalization */
    settings->tone_map_gamma = 2.20;            /* Default gamma */
    settings->tone_map_exposure = 2.00;         /* Default exposure */
    settings->tone_map_white_point = 11.20;     /* Default white point */
    settings->tone_map_intensity = 0.00;        /* Default intensity */
    settings->tone_map_adaptation = 1.00;       /* Default adaptation */
    settings->tone_map_color_correction = 0.00; /* Default color correction */

    /* GPU acceleration defaults */
    settings->gpu_acceleration_enabled = DEFAULT_GPU_ACCELERATION;
    settings->gpu_device_name = NULL; /* NULL = use system default GPU */

    /* Color management defaults */
    settings->cm_rendering_intent = 1; /* RELATIVE_COLORIMETRIC */
    settings->cm_black_point_compensation = TRUE;
    settings->cm_use_embedded_icc = TRUE;
    settings->cm_mode = 0; /* CM_MODE_SYSTEM_PROFILE */
    settings->cm_display_profiles = NULL;

    settings->interface_locale = g_strdup(DEFAULT_INTERFACE_LOCALE);

    settings->mouse_snap_distance = DEFAULT_MOUSE_SNAP_DISTANCE;
    settings->mouse_snap = TRUE;
    settings->mouse_snap_to_canvas_edges = TRUE;
    settings->mouse_snap_to_centerlines = TRUE;
    settings->mouse_snap_to_layers = FALSE;

    return settings;
}

/**
 * Get the executable directory path (cross-platform)
 */
gchar* settings_get_executable_dir(void) {
#ifdef _WIN32
    /* Windows: Use GetModuleFileName */
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH) != 0) {
        /* Convert wide string to UTF-8 */
        gchar* utf8_path = g_utf16_to_utf8((gunichar2*)path, -1, NULL, NULL, NULL);
        if (utf8_path) {
            gchar* dir = g_path_get_dirname(utf8_path);
            g_free(utf8_path);
            return dir;
        }
    }
#else
    /* Linux/Unix: Use /proc/self/exe or readlink */
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return g_path_get_dirname(path);
    }
#endif

    /* Fallback: return current directory */
    return g_get_current_dir();
}

/**
 * Get the settings file path
 */
static gchar* settings_get_file_path(const char* app_dir) {
    return g_build_filename(app_dir, "settings.xml", NULL);
}

/**
 * Parse a double value from XML attribute
 */
static gdouble parse_double_attr(xmlNode* node, const char* attr_name, gdouble default_value) {
    xmlChar* attr = xmlGetProp(node, (const xmlChar*)attr_name);
    if (attr) {
        gdouble value = g_ascii_strtod((const char*)attr, NULL);
        xmlFree(attr);
        return value;
    }
    return default_value;
}

/**
 * Parse "R,G,B" string (0-255) into 0.0-1.0 doubles. Returns TRUE if parsed successfully.
 */
static gboolean parse_rgb_string(const char* str, gdouble* r, gdouble* g, gdouble* b);

/**
 * Parse a boolean value from XML attribute ("true" / "false")
 */
static gboolean parse_bool_attr(xmlNode* node, const char* attr_name, gboolean default_value) {
    xmlChar* attr = xmlGetProp(node, (const xmlChar*)attr_name);
    if (!attr) {
        return default_value;
    }
    gboolean v = (xmlStrcmp(attr, (const xmlChar*)"true") == 0);
    xmlFree(attr);
    return v;
}

/**
 * Parse an integer value from XML attribute
 */
static gint parse_int_attr(xmlNode* node, const char* attr_name, gint default_value) {
    xmlChar* attr = xmlGetProp(node, (const xmlChar*)attr_name);
    if (attr) {
        gint value = (gint)strtol((const char*)attr, NULL, 10);
        xmlFree(attr);
        return value;
    }
    return default_value;
}

/**
 * Load canvas background color from XML
 */
static void settings_load_canvas(Settings* settings, xmlNode* canvas_node) {
    xmlNode* bg_node = NULL;

    /* Find background node */
    for (xmlNode* cur = canvas_node->children; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE && xmlStrcmp(cur->name, (const xmlChar*)"background") == 0) {
            bg_node = cur;
            break;
        }
    }

    if (bg_node) {
        /* Parse RGBA attributes (0-255 range) */
        gint r = parse_int_attr(bg_node, "r", (gint)(DEFAULT_CANVAS_BG_R * 255.0));
        gint g = parse_int_attr(bg_node, "g", (gint)(DEFAULT_CANVAS_BG_G * 255.0));
        gint b = parse_int_attr(bg_node, "b", (gint)(DEFAULT_CANVAS_BG_B * 255.0));
        gint a = parse_int_attr(bg_node, "a", 255);

        /* Convert to 0.0-1.0 range */
        settings->canvas_bg_r = (gdouble)r / 255.0;
        settings->canvas_bg_g = (gdouble)g / 255.0;
        settings->canvas_bg_b = (gdouble)b / 255.0;

        /* Clamp values */
        if (settings->canvas_bg_r < 0.0)
            settings->canvas_bg_r = 0.0;
        if (settings->canvas_bg_r > 1.0)
            settings->canvas_bg_r = 1.0;
        if (settings->canvas_bg_g < 0.0)
            settings->canvas_bg_g = 0.0;
        if (settings->canvas_bg_g > 1.0)
            settings->canvas_bg_g = 1.0;
        if (settings->canvas_bg_b < 0.0)
            settings->canvas_bg_b = 0.0;
        if (settings->canvas_bg_b > 1.0)
            settings->canvas_bg_b = 1.0;
    }
}

/**
 * Load tool options from XML
 */
static void settings_load_tools(Settings* settings, xmlNode* tools_node) {
    /* Iterate through tool nodes */
    for (xmlNode* tool_node = tools_node->children; tool_node; tool_node = tool_node->next) {
        if (tool_node->type != XML_ELEMENT_NODE || xmlStrcmp(tool_node->name, (const xmlChar*)"tool") != 0) {
            continue;
        }

        /* Get tool name attribute */
        xmlChar* tool_name_attr = xmlGetProp(tool_node, (const xmlChar*)"name");
        if (!tool_name_attr) {
            continue;
        }

        gchar* tool_name = g_strdup((const char*)tool_name_attr);
        xmlFree(tool_name_attr);

        /* Create or get tool options hash table */
        GHashTable* tool_opts = (GHashTable*)g_hash_table_lookup(settings->tool_options, tool_name);
        if (!tool_opts) {
            tool_opts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
            g_hash_table_insert(settings->tool_options, g_strdup(tool_name), tool_opts);
        }

        /* Iterate through option nodes */
        for (xmlNode* option_node = tool_node->children; option_node; option_node = option_node->next) {
            if (option_node->type != XML_ELEMENT_NODE || xmlStrcmp(option_node->name, (const xmlChar*)"option") != 0) {
                continue;
            }

            xmlChar* option_name_attr = xmlGetProp(option_node, (const xmlChar*)"name");
            xmlChar* option_value_attr = xmlGetProp(option_node, (const xmlChar*)"value");

            if (option_name_attr && option_value_attr) {
                gchar* option_name = g_strdup((const char*)option_name_attr);
                gchar* option_value = g_strdup((const char*)option_value_attr);
                g_hash_table_insert(tool_opts, option_name, option_value);
            }

            if (option_name_attr)
                xmlFree(option_name_attr);
            if (option_value_attr)
                xmlFree(option_value_attr);
        }

        g_free(tool_name);
    }
}

/**
 * Load undo settings from XML (forward declaration)
 */
static void settings_load_undo(Settings* settings, xmlNode* undo_node);

/**
 * Save undo settings to XML (forward declaration)
 */
static void settings_save_undo(xmlTextWriterPtr writer, Settings* settings);

/**
 * Load performance settings from XML (forward declaration)
 */
static void settings_load_performance(Settings* settings, xmlNode* performance_node);

/**
 * Save performance settings to XML (forward declaration)
 */
static void settings_save_performance(xmlTextWriterPtr writer, Settings* settings);

/**
 * Load UI settings from XML (canvas background + checkerboard, under <ui>)
 */
static void settings_load_ui(Settings* settings, xmlNode* ui_node);

/**
 * Save UI settings to XML (forward declaration)
 */
static void settings_save_ui(xmlTextWriterPtr writer, Settings* settings);

/**
 * Load view settings from XML (forward declaration)
 */
static void settings_load_view(Settings* settings, xmlNode* view_node);

/**
 * Save view settings to XML (forward declaration)
 */
static void settings_save_view(xmlTextWriterPtr writer, Settings* settings);

/**
 * Load tone mapping settings from XML (forward declaration)
 */
static void settings_load_tone_mapping(Settings* settings, xmlNode* tone_mapping_node);

/**
 * Save tone mapping settings to XML (forward declaration)
 */
static void settings_save_tone_mapping(xmlTextWriterPtr writer, Settings* settings);

/**
 * Load Advanced settings from XML (forward declaration)
 */
static void settings_load_advanced(Settings* settings, xmlNode* advanced_node);

/**
 * Save Advanced settings to XML (forward declaration)
 */
static void settings_save_advanced(xmlTextWriterPtr writer, Settings* settings);

/**
 * Load color management settings from XML (forward declaration)
 */
static void settings_load_color_management(Settings* settings, xmlNode* color_management_node);

/**
 * Save color management settings to XML (forward declaration)
 */
static void settings_save_color_management(xmlTextWriterPtr writer, Settings* settings);

static void settings_load_mouse(Settings* settings, xmlNode* mouse_node);
static void settings_save_mouse(xmlTextWriterPtr writer, Settings* settings);

/**
 * Load recent files from XML
 * Stores RecentFile* entries in Settings->recent_files (includes path and timestamp)
 */
static void settings_load_recent_files(Settings* settings, xmlNode* recent_files_node) {
    /* Load max attribute if present (1-32) */
    xmlChar* max_attr = xmlGetProp(recent_files_node, (const xmlChar*)"max");
    if (max_attr) {
        gint max_val = (gint)strtol((const char*)max_attr, NULL, 10);
        settings_set_max_recent_files(settings, (guint)max_val);
        xmlFree(max_attr);
    }

    /* Iterate through file nodes */
    for (xmlNode* file_node = recent_files_node->children; file_node; file_node = file_node->next) {
        if (file_node->type != XML_ELEMENT_NODE || xmlStrcmp(file_node->name, (const xmlChar*)"file") != 0) {
            continue;
        }

        xmlChar* path_attr = xmlGetProp(file_node, (const xmlChar*)"path");
        xmlChar* timestamp_attr = xmlGetProp(file_node, (const xmlChar*)"timestamp");

        if (path_attr) {
            RecentFile* rf = g_malloc(sizeof(RecentFile));
            rf->path = g_strdup((const char*)path_attr);

            /* Parse timestamp if present, otherwise use current time */
            if (timestamp_attr) {
                rf->last_opened = (time_t)strtol((const char*)timestamp_attr, NULL, 10);
                xmlFree(timestamp_attr);
            } else {
                rf->last_opened = time(NULL);
            }

            /* Add to front (most recent first) */
            settings->recent_files = g_list_prepend(settings->recent_files, rf);
            xmlFree(path_attr);
        } else if (timestamp_attr) {
            xmlFree(timestamp_attr);
        }
    }

    /* Limit to max_recent_files */
    guint count = g_list_length(settings->recent_files);
    if (count > settings->max_recent_files) {
        GList* to_remove = g_list_nth(settings->recent_files, settings->max_recent_files);
        if (to_remove) {
            GList* rest = to_remove->next;
            to_remove->next = NULL;
            g_list_free_full(rest, recent_file_free);
        }
    }
}

/**
 * Load settings from XML file
 */
Settings* settings_load(const char* app_dir) {
    Settings* settings = settings_create_default();

    if (!app_dir) {
        return settings;
    }

    gchar* file_path = settings_get_file_path(app_dir);

    /* Check if file exists before trying to parse it */
    /* This prevents libxml2 from printing warnings when file doesn't exist */
    if (!g_file_test(file_path, G_FILE_TEST_EXISTS)) {
        /* File doesn't exist - return default settings */
        g_free(file_path);
        return settings;
    }

    /* Try to parse XML file */
    xmlDoc* doc = xmlReadFile(file_path, NULL, 0);
    if (!doc) {
        /* File exists but is invalid - return default settings */
        g_free(file_path);
        return settings;
    }

    xmlNode* root = xmlDocGetRootElement(doc);
    if (!root || xmlStrcmp(root->name, (const xmlChar*)"app_settings") != 0) {
        xmlFreeDoc(doc);
        g_free(file_path);
        return settings;
    }

    /* Parse child nodes */
    for (xmlNode* cur = root->children; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(cur->name, (const xmlChar*)"ui") == 0) {
            settings_load_ui(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"tools") == 0) {
            settings_load_tools(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"recent_files") == 0) {
            settings_load_recent_files(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"performance") == 0) {
            settings_load_performance(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"view") == 0) {
            settings_load_view(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"mouse") == 0) {
            settings_load_mouse(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"tone_mapping") == 0) {
            settings_load_tone_mapping(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"advanced") == 0) {
            settings_load_advanced(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"color_management") == 0) {
            settings_load_color_management(settings, cur);
        }
    }

    xmlFreeDoc(doc);
    g_free(file_path);

    if (!settings->interface_locale || !settings->interface_locale[0]) {
        settings_set_interface_locale(settings, DEFAULT_INTERFACE_LOCALE);
    }

    return settings;
}

/**
 * Load undo settings from XML
 */
static void settings_load_undo(Settings* settings, xmlNode* undo_node) {
    if (!settings || !undo_node) {
        return;
    }

    /* Load compression level */
    xmlChar* compression_attr = xmlGetProp(undo_node, (const xmlChar*)"compression_level");
    if (compression_attr) {
        gint level = (gint)strtol((const char*)compression_attr, NULL, 10);
        settings_set_undo_compression_level(settings, level);
        xmlFree(compression_attr);
    }

    /* Load temp directory (backward compat: also in performance/undo) */
    xmlChar* temp_dir_attr = xmlGetProp(undo_node, (const xmlChar*)"temp_directory");
    if (temp_dir_attr) {
        settings_set_undo_temp_directory(settings, (const char*)temp_dir_attr);
        xmlFree(temp_dir_attr);
    }

    /* Load undo levels */
    xmlChar* levels_attr = xmlGetProp(undo_node, (const xmlChar*)"levels");
    if (levels_attr) {
        gint levels = (gint)strtol((const char*)levels_attr, NULL, 10);
        settings_set_undo_levels(settings, levels);
        xmlFree(levels_attr);
    }
}

/**
 * Save undo settings to XML
 */
static void settings_save_undo(xmlTextWriterPtr writer, Settings* settings) {
    if (!writer || !settings) {
        return;
    }

    xmlTextWriterStartElement(writer, (const xmlChar*)"undo");

    /* Save compression level */
    gchar level_str[16];
    g_snprintf(level_str, sizeof(level_str), "%d", settings->undo_compression_level);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"compression_level", (const xmlChar*)level_str);

    /* Save undo levels */
    gchar levels_str[16];
    g_snprintf(levels_str, sizeof(levels_str), "%d", settings->undo_levels);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"levels", (const xmlChar*)levels_str);

    xmlTextWriterEndElement(writer); /* undo */
}

/**
 * Load performance settings from XML
 */
static void settings_load_performance(Settings* settings, xmlNode* performance_node) {
    if (!settings || !performance_node) {
        return;
    }

    /* Load worker_threads attribute if present */
    xmlChar* threads_attr = xmlGetProp(performance_node, (const xmlChar*)"worker_threads");
    if (threads_attr) {
        gint threads = (gint)strtol((const char*)threads_attr, NULL, 10);
        settings_set_worker_threads(settings, threads);
        xmlFree(threads_attr);
    }

    /* Load file_recovery_interval attribute if present (seconds, 30-2700) */
    xmlChar* recovery_attr = xmlGetProp(performance_node, (const xmlChar*)"file_recovery_interval");
    if (recovery_attr) {
        gint interval = (gint)strtol((const char*)recovery_attr, NULL, 10);
        settings_set_file_recovery_interval_seconds(settings, interval);
        xmlFree(recovery_attr);
    }

    /* Load GPU acceleration attribute if present */
    xmlChar* gpu_enabled_attr = xmlGetProp(performance_node, (const xmlChar*)"gpu_acceleration");
    if (gpu_enabled_attr) {
        if (xmlStrcmp(gpu_enabled_attr, (const xmlChar*)"true") == 0) {
            settings->gpu_acceleration_enabled = TRUE;
        } else {
            settings->gpu_acceleration_enabled = FALSE;
        }
        xmlFree(gpu_enabled_attr);
    }

    /* Load GPU device name attribute if present */
    xmlChar* gpu_device_attr = xmlGetProp(performance_node, (const xmlChar*)"gpu_device");
    if (gpu_device_attr) {
        if (settings->gpu_device_name) {
            g_free(settings->gpu_device_name);
        }
        /* Empty string means system default */
        if (xmlStrlen(gpu_device_attr) > 0) {
            settings->gpu_device_name = g_strdup((const char*)gpu_device_attr);
        } else {
            settings->gpu_device_name = NULL;
        }
        xmlFree(gpu_device_attr);
    }

    /* Iterate through child nodes */
    for (xmlNode* cur = performance_node->children; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) {
            continue;
        }

        /* Load undo settings */
        if (xmlStrcmp(cur->name, (const xmlChar*)"undo") == 0) {
            settings_load_undo(settings, cur);
        }
    }
}

/**
 * Save performance settings to XML
 */
static void settings_save_performance(xmlTextWriterPtr writer, Settings* settings) {
    if (!writer || !settings) {
        return;
    }

    xmlTextWriterStartElement(writer, (const xmlChar*)"performance");

    /* Save worker threads as attribute */
    gchar threads_str[16];
    g_snprintf(threads_str, sizeof(threads_str), "%d", settings->worker_threads);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"worker_threads", (const xmlChar*)threads_str);

    /* Save file recovery interval as attribute */
    gchar recovery_str[16];
    g_snprintf(recovery_str, sizeof(recovery_str), "%d", settings->file_recovery_interval_seconds);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"file_recovery_interval", (const xmlChar*)recovery_str);

    /* Save GPU acceleration as attribute */
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"gpu_acceleration",
                                (const xmlChar*)(settings->gpu_acceleration_enabled ? "true" : "false"));

    /* Save GPU device name (empty string for system default) */
    if (settings->gpu_device_name) {
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"gpu_device",
                                    (const xmlChar*)settings->gpu_device_name);
    } else {
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"gpu_device", (const xmlChar*)"");
    }

    /* Save undo settings */
    settings_save_undo(writer, settings);

    xmlTextWriterEndElement(writer); /* performance */
}

/**
 * Load UI settings from XML (canvas background + checkerboard under <ui>)
 */
static void settings_load_ui(Settings* settings, xmlNode* ui_node) {
    if (!settings || !ui_node) {
        return;
    }

    for (xmlNode* cur = ui_node->children; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(cur->name, (const xmlChar*)"canvas") == 0) {
            settings_load_canvas(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"alpha_check_size") == 0) {
            xmlChar* value_attr = xmlGetProp(cur, (const xmlChar*)"value");
            if (value_attr) {
                gint v = (gint)strtol((const char*)value_attr, NULL, 10);
                settings_set_alpha_check_size(settings, v);
                xmlFree(value_attr);
            }
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"alpha_color_one") == 0) {
            xmlChar* value_attr = xmlGetProp(cur, (const xmlChar*)"value");
            if (value_attr) {
                gdouble r, g, b;
                if (parse_rgb_string((const char*)value_attr, &r, &g, &b)) {
                    settings_set_alpha_color_one(settings, r, g, b);
                }
                xmlFree(value_attr);
            }
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"alpha_color_two") == 0) {
            xmlChar* value_attr = xmlGetProp(cur, (const xmlChar*)"value");
            if (value_attr) {
                gdouble r, g, b;
                if (parse_rgb_string((const char*)value_attr, &r, &g, &b)) {
                    settings_set_alpha_color_two(settings, r, g, b);
                }
                xmlFree(value_attr);
            }
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"interface_locale") == 0) {
            xmlChar* value_attr = xmlGetProp(cur, (const xmlChar*)"value");
            if (value_attr) {
                const char* s = (const char*)value_attr;
                settings_set_interface_locale(settings, s);
                xmlFree(value_attr);
            }
        }
    }
}

/**
 * Load view settings from XML
 */
static void settings_load_view(Settings* settings, xmlNode* view_node) {
    if (!settings || !view_node) {
        return;
    }

    /* Iterate through child nodes */
    for (xmlNode* cur = view_node->children; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) {
            continue;
        }

        /* Load show_layer_edges setting */
        if (xmlStrcmp(cur->name, (const xmlChar*)"show_layer_edges") == 0) {
            xmlChar* value_attr = xmlGetProp(cur, (const xmlChar*)"value");
            if (value_attr) {
                if (xmlStrcmp(value_attr, (const xmlChar*)"true") == 0) {
                    settings->show_layer_edges = TRUE;
                } else if (xmlStrcmp(value_attr, (const xmlChar*)"false") == 0) {
                    settings->show_layer_edges = FALSE;
                }
                xmlFree(value_attr);
            }
        }
        /* Load show_smart_guides setting (enabled unless value explicitly false) */
        else if (xmlStrcmp(cur->name, (const xmlChar*)"show_smart_guides") == 0) {
            xmlChar* value_attr = xmlGetProp(cur, (const xmlChar*)"value");
            if (value_attr) {
                if (xmlStrcmp(value_attr, (const xmlChar*)"false") == 0) {
                    settings->show_smart_guides = FALSE;
                } else {
                    settings->show_smart_guides = TRUE;
                }
                xmlFree(value_attr);
            }
        }
        /* Load show_statusbar setting */
        else if (xmlStrcmp(cur->name, (const xmlChar*)"show_statusbar") == 0) {
            xmlChar* value_attr = xmlGetProp(cur, (const xmlChar*)"value");
            if (value_attr) {
                if (xmlStrcmp(value_attr, (const xmlChar*)"true") == 0) {
                    settings->show_statusbar = TRUE;
                } else if (xmlStrcmp(value_attr, (const xmlChar*)"false") == 0) {
                    settings->show_statusbar = FALSE;
                }
                xmlFree(value_attr);
            }
        }
        /* Load show_rulers setting */
        else if (xmlStrcmp(cur->name, (const xmlChar*)"show_rulers") == 0) {
            xmlChar* value_attr = xmlGetProp(cur, (const xmlChar*)"value");
            if (value_attr) {
                if (xmlStrcmp(value_attr, (const xmlChar*)"true") == 0) {
                    settings->show_rulers = TRUE;
                } else if (xmlStrcmp(value_attr, (const xmlChar*)"false") == 0) {
                    settings->show_rulers = FALSE;
                }
                xmlFree(value_attr);
            }
        }
        /* Load show_gpu_stats setting */
        else if (xmlStrcmp(cur->name, (const xmlChar*)"show_gpu_stats") == 0) {
            xmlChar* value_attr = xmlGetProp(cur, (const xmlChar*)"value");
            if (value_attr) {
                if (xmlStrcmp(value_attr, (const xmlChar*)"true") == 0) {
                    settings->show_gpu_stats = TRUE;
                } else if (xmlStrcmp(value_attr, (const xmlChar*)"false") == 0) {
                    settings->show_gpu_stats = FALSE;
                }
                xmlFree(value_attr);
            }
        }
    }
}

/**
 * Save view settings to XML
 */
static void settings_save_view(xmlTextWriterPtr writer, Settings* settings) {
    if (!writer || !settings) {
        return;
    }

    xmlTextWriterStartElement(writer, (const xmlChar*)"view");

    /* Save show_layer_edges setting */
    xmlTextWriterStartElement(writer, (const xmlChar*)"show_layer_edges");
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value",
                                (const xmlChar*)(settings->show_layer_edges ? "true" : "false"));
    xmlTextWriterEndElement(writer); /* show_layer_edges */

    /* Save show_smart_guides setting */
    xmlTextWriterStartElement(writer, (const xmlChar*)"show_smart_guides");
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value",
                                (const xmlChar*)(settings->show_smart_guides ? "true" : "false"));
    xmlTextWriterEndElement(writer); /* show_smart_guides */

    /* Save show_statusbar setting */
    xmlTextWriterStartElement(writer, (const xmlChar*)"show_statusbar");
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value",
                                (const xmlChar*)(settings->show_statusbar ? "true" : "false"));
    xmlTextWriterEndElement(writer); /* show_statusbar */

    /* Save show_rulers setting */
    xmlTextWriterStartElement(writer, (const xmlChar*)"show_rulers");
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value",
                                (const xmlChar*)(settings->show_rulers ? "true" : "false"));
    xmlTextWriterEndElement(writer); /* show_rulers */

    /* Save show_gpu_stats setting */
    xmlTextWriterStartElement(writer, (const xmlChar*)"show_gpu_stats");
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value",
                                (const xmlChar*)(settings->show_gpu_stats ? "true" : "false"));
    xmlTextWriterEndElement(writer); /* show_gpu_stats */

    xmlTextWriterEndElement(writer); /* view */
}

/**
 * Find first direct child element with the given tag name (libxml2).
 */
static xmlNode* settings_xml_child_element_named(xmlNode* parent, const char* name) {
    if (!parent || !name) {
        return NULL;
    }
    for (xmlNode* cur = parent->children; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE && xmlStrcmp(cur->name, (const xmlChar*)name) == 0) {
            return cur;
        }
    }
    return NULL;
}

/**
 * Read a mouse setting: prefer child `<name value="…"/>`, else legacy attribute on `<mouse name="…"/>`.
 */
static gint settings_read_mouse_int(xmlNode* mouse_node, const char* name, gint default_value) {
    xmlNode* child = settings_xml_child_element_named(mouse_node, name);
    if (child) {
        return parse_int_attr(child, "value", default_value);
    }
    return parse_int_attr(mouse_node, name, default_value);
}

static gboolean settings_read_mouse_bool(xmlNode* mouse_node, const char* name, gboolean default_value) {
    xmlNode* child = settings_xml_child_element_named(mouse_node, name);
    if (child) {
        return parse_bool_attr(child, "value", default_value);
    }
    return parse_bool_attr(mouse_node, name, default_value);
}

/**
 * Load mouse settings from XML (same nested style as view: child elements with value="…").
 * Legacy: attributes on a single empty mouse element are still accepted.
 */
static void settings_load_mouse(Settings* settings, xmlNode* mouse_node) {
    if (!settings || !mouse_node) {
        return;
    }
    settings_set_mouse_snap_distance(settings, settings_read_mouse_int(mouse_node, "snap_distance", DEFAULT_MOUSE_SNAP_DISTANCE));
    settings_set_mouse_snap(settings, settings_read_mouse_bool(mouse_node, "snap", TRUE));
    settings_set_mouse_snap_to_canvas_edges(settings, settings_read_mouse_bool(mouse_node, "snap_to_canvas_edges", TRUE));
    settings_set_mouse_snap_to_centerlines(settings, settings_read_mouse_bool(mouse_node, "snap_to_centerlines", TRUE));
    settings_set_mouse_snap_to_layers(settings, settings_read_mouse_bool(mouse_node, "snap_to_layers", FALSE));
}

/**
 * Save mouse settings to XML (nested elements with value="…", like view)
 */
static void settings_save_mouse(xmlTextWriterPtr writer, Settings* settings) {
    if (!writer || !settings) {
        return;
    }
    gchar buf[16];

    xmlTextWriterStartElement(writer, (const xmlChar*)"mouse");

    g_snprintf(buf, sizeof(buf), "%d", settings_get_mouse_snap_distance(settings));
    xmlTextWriterStartElement(writer, (const xmlChar*)"snap_distance");
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value", (const xmlChar*)buf);
    xmlTextWriterEndElement(writer); /* snap_distance */

    xmlTextWriterStartElement(writer, (const xmlChar*)"snap");
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value",
                                (const xmlChar*)(settings_get_mouse_snap(settings) ? "true" : "false"));
    xmlTextWriterEndElement(writer); /* snap */

    xmlTextWriterStartElement(writer, (const xmlChar*)"snap_to_canvas_edges");
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value",
                                (const xmlChar*)(settings_get_mouse_snap_to_canvas_edges(settings) ? "true" : "false"));
    xmlTextWriterEndElement(writer); /* snap_to_canvas_edges */

    xmlTextWriterStartElement(writer, (const xmlChar*)"snap_to_centerlines");
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value",
                                (const xmlChar*)(settings_get_mouse_snap_to_centerlines(settings) ? "true" : "false"));
    xmlTextWriterEndElement(writer); /* snap_to_centerlines */

    xmlTextWriterStartElement(writer, (const xmlChar*)"snap_to_layers");
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value",
                                (const xmlChar*)(settings_get_mouse_snap_to_layers(settings) ? "true" : "false"));
    xmlTextWriterEndElement(writer); /* snap_to_layers */

    xmlTextWriterEndElement(writer); /* mouse */
}

/**
 * Save UI settings to XML (canvas background + checkerboard under <ui>)
 */
static void settings_save_ui(xmlTextWriterPtr writer, Settings* settings) {
    if (!writer || !settings) {
        return;
    }

    xmlTextWriterStartElement(writer, (const xmlChar*)"ui");

    /* Canvas background (under ui) */
    xmlTextWriterStartElement(writer, (const xmlChar*)"canvas");
    xmlTextWriterStartElement(writer, (const xmlChar*)"background");
    {
        guint r = (guint)(settings->canvas_bg_r * 255.0);
        guint g = (guint)(settings->canvas_bg_g * 255.0);
        guint b = (guint)(settings->canvas_bg_b * 255.0);
        gchar r_str[16], g_str[16], b_str[16];
        g_snprintf(r_str, sizeof(r_str), "%u", r);
        g_snprintf(g_str, sizeof(g_str), "%u", g);
        g_snprintf(b_str, sizeof(b_str), "%u", b);
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"r", (const xmlChar*)r_str);
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"g", (const xmlChar*)g_str);
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"b", (const xmlChar*)b_str);
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"a", (const xmlChar*)"255");
    }
    xmlTextWriterEndElement(writer); /* background */
    xmlTextWriterEndElement(writer); /* canvas */

    /* Alpha checkerboard (under ui) */
    {
        gchar buf[16];
        g_snprintf(buf, sizeof(buf), "%d", settings->alpha_check_size);
        xmlTextWriterStartElement(writer, (const xmlChar*)"alpha_check_size");
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value", (const xmlChar*)buf);
        xmlTextWriterEndElement(writer);
    }
    {
        gchar buf[16];
        g_snprintf(buf, sizeof(buf), "%d,%d,%d",
                   (gint)(settings->alpha_color_one_r * 255.0 + 0.5),
                   (gint)(settings->alpha_color_one_g * 255.0 + 0.5),
                   (gint)(settings->alpha_color_one_b * 255.0 + 0.5));
        xmlTextWriterStartElement(writer, (const xmlChar*)"alpha_color_one");
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value", (const xmlChar*)buf);
        xmlTextWriterEndElement(writer);
    }
    {
        gchar buf[16];
        g_snprintf(buf, sizeof(buf), "%d,%d,%d",
                   (gint)(settings->alpha_color_two_r * 255.0 + 0.5),
                   (gint)(settings->alpha_color_two_g * 255.0 + 0.5),
                   (gint)(settings->alpha_color_two_b * 255.0 + 0.5));
        xmlTextWriterStartElement(writer, (const xmlChar*)"alpha_color_two");
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value", (const xmlChar*)buf);
        xmlTextWriterEndElement(writer);
    }

    xmlTextWriterStartElement(writer, (const xmlChar*)"interface_locale");
    xmlTextWriterWriteAttribute(
        writer, (const xmlChar*)"value",
        (const xmlChar*)settings_get_interface_locale(settings));
    xmlTextWriterEndElement(writer);

    xmlTextWriterEndElement(writer); /* ui */
}

/**
 * Save tool options to XML
 */
static void settings_save_tools(xmlTextWriterPtr writer, Settings* settings) {
    if (!writer || !settings) {
        return;
    }

    xmlTextWriterStartElement(writer, (const xmlChar*)"tools");

    /* Check if tool_options hash table exists and has entries */
    if (!settings->tool_options) {
        xmlTextWriterEndElement(writer); /* tools */
        return;
    }

    /* Safety: Verify hash table is still valid before iterating */
    /* Try to get the size - if this fails, the hash table is corrupted */
    guint tool_count = 0;
    GHashTableIter iter;
    gpointer tool_name_ptr, tool_opts_ptr;

    /* Initialize iterator - if this fails, skip saving tools */
    g_hash_table_iter_init(&iter, settings->tool_options);

    /* Limit iteration to prevent infinite loops from corrupted hash tables */
    guint max_iterations = 1000;
    guint iteration_count = 0;

    while (g_hash_table_iter_next(&iter, &tool_name_ptr, &tool_opts_ptr) && iteration_count < max_iterations) {
        iteration_count++;

        const gchar* tool_name = (const gchar*)tool_name_ptr;
        GHashTable* tool_opts = (GHashTable*)tool_opts_ptr;

        /* Validate pointers before using */
        if (!tool_name || !tool_opts) {
            continue; /* Skip invalid entries */
        }

        /* Validate tool_name is a valid string */
        if (tool_name[0] == '\0') {
            continue; /* Skip empty tool names */
        }

        xmlTextWriterStartElement(writer, (const xmlChar*)"tool");
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"name", (const xmlChar*)tool_name);

        /* Iterate through tool options with safety limit */
        GHashTableIter opt_iter;
        gpointer opt_name_ptr, opt_value_ptr;
        guint opt_iteration_count = 0;
        const guint max_opt_iterations = 100;

        g_hash_table_iter_init(&opt_iter, tool_opts);
        while (g_hash_table_iter_next(&opt_iter, &opt_name_ptr, &opt_value_ptr) && opt_iteration_count < max_opt_iterations) {
            opt_iteration_count++;

            const gchar* opt_name = (const gchar*)opt_name_ptr;
            const gchar* opt_value = (const gchar*)opt_value_ptr;

            /* Validate pointers before using */
            if (!opt_name || !opt_value) {
                continue; /* Skip invalid entries */
            }

            /* Validate strings are not empty */
            if (opt_name[0] == '\0' || opt_value[0] == '\0') {
                continue; /* Skip empty option names or values */
            }

            xmlTextWriterStartElement(writer, (const xmlChar*)"option");
            xmlTextWriterWriteAttribute(writer, (const xmlChar*)"name", (const xmlChar*)opt_name);
            xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value", (const xmlChar*)opt_value);
            xmlTextWriterEndElement(writer); /* option */
        }

        xmlTextWriterEndElement(writer); /* tool */
    }

    xmlTextWriterEndElement(writer); /* tools */
}

/**
 * Save recent files to XML
 * Reads from Settings->recent_files (GList of RecentFile*)
 */
static void settings_save_recent_files(xmlTextWriterPtr writer, Settings* settings) {
    xmlTextWriterStartElement(writer, (const xmlChar*)"recent_files");

    /* Write max attribute */
    gchar max_str[16];
    g_snprintf(max_str, sizeof(max_str), "%u", settings->max_recent_files);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"max", (const xmlChar*)max_str);

    /* Write files in order (most recent first) */
    for (GList* iter = settings->recent_files; iter; iter = iter->next) {
        RecentFile* rf = (RecentFile*)iter->data;
        if (rf && rf->path) {
            xmlTextWriterStartElement(writer, (const xmlChar*)"file");
            xmlTextWriterWriteAttribute(writer, (const xmlChar*)"path", (const xmlChar*)rf->path);

            /* Write timestamp as string */
            gchar* timestamp_str = g_strdup_printf("%ld", (long)rf->last_opened);
            xmlTextWriterWriteAttribute(writer, (const xmlChar*)"timestamp", (const xmlChar*)timestamp_str);
            g_free(timestamp_str);

            xmlTextWriterEndElement(writer); /* file */
        }
    }

    xmlTextWriterEndElement(writer); /* recent_files */
}

/**
 * Save settings to XML file
 */
gboolean settings_save(Settings* settings, const char* app_dir) {
    if (!settings || !app_dir) {
        return FALSE;
    }

    gchar* file_path = settings_get_file_path(app_dir);
    gchar* temp_path = g_strdup_printf("%s.tmp", file_path);

    /* Create XML writer */
    xmlTextWriterPtr writer = xmlNewTextWriterFilename(temp_path, 0);
    if (!writer) {
        g_free(file_path);
        g_free(temp_path);
        return FALSE;
    }

    /* Set indentation */
    xmlTextWriterSetIndent(writer, 1);
    xmlTextWriterSetIndentString(writer, (const xmlChar*)"    ");

    /* Write XML declaration */
    xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);

    /* Write root element */
    xmlTextWriterStartElement(writer, (const xmlChar*)"app_settings");

    /* Write UI settings (canvas background + checkerboard) */
    settings_save_ui(writer, settings);

    /* Write tool options */
    settings_save_tools(writer, settings);

    /* Write recent files */
    settings_save_recent_files(writer, settings);

    /* Write performance settings (includes undo) */
    settings_save_performance(writer, settings);

    /* Write view settings */
    settings_save_view(writer, settings);

    /* Mouse settings (snap distance, etc.) */
    settings_save_mouse(writer, settings);

    /* Write tone mapping settings */
    settings_save_tone_mapping(writer, settings);

    /* Write Advanced settings (temp file directory, etc.) */
    settings_save_advanced(writer, settings);

    /* Write color management settings */
    settings_save_color_management(writer, settings);

    /* Close root element */
    xmlTextWriterEndElement(writer); /* app_settings */

    /* Close document */
    xmlTextWriterEndDocument(writer);
    xmlFreeTextWriter(writer);

    /* Atomically replace the old file with the new one */
    gboolean success = FALSE;
    if (g_file_test(temp_path, G_FILE_TEST_EXISTS)) {
        /* Remove old file if it exists */
        if (g_file_test(file_path, G_FILE_TEST_EXISTS)) {
            g_remove(file_path);
        }
        /* Rename temp file to final file */
        if (g_rename(temp_path, file_path) == 0) {
            success = TRUE;
        } else {
            debug_log("WRN", "Failed to rename settings file: %s -> %s", temp_path, file_path);
        }
    }

    g_free(file_path);
    g_free(temp_path);

    return success;
}

/**
 * Free settings structure
 */
void settings_free(Settings* settings) {
    if (!settings) {
        return;
    }

    if (settings->tool_options) {
        g_hash_table_destroy(settings->tool_options);
    }

    if (settings->recent_files) {
        g_list_free_full(settings->recent_files, recent_file_free);
    }

    if (settings->undo_temp_directory) {
        g_free(settings->undo_temp_directory);
    }

    if (settings->gpu_device_name) {
        g_free(settings->gpu_device_name);
    }

    if (settings->cm_display_profiles) {
        g_hash_table_destroy(settings->cm_display_profiles);
    }

    g_free(settings->interface_locale);

    g_free(settings);
}

/**
 * Sync recent files from Settings to recent_files.c system
 * Populates the recent_files system with RecentFile entries from Settings
 * This is called by recent_files_load() to load from XML
 * Preserves timestamps from Settings
 */
void settings_sync_recent_files_to_system(Settings* settings) {
    if (!settings) {
        return;
    }

    /* Clear existing recent files */
    recent_files_clear();

    /* Add each RecentFile from settings to recent_files system */
    /* Settings stores most recent first, so we iterate in order */
    /* Don't check file existence when loading from settings - show all entries */
    for (GList* iter = settings->recent_files; iter; iter = iter->next) {
        RecentFile* rf = (RecentFile*)iter->data;
        if (rf && rf->path) {
            /* Use the helper function to add with preserved timestamp */
            /* Pass FALSE for check_exists to show files even if they don't exist */
            recent_files_add_with_timestamp(rf->path, rf->last_opened, FALSE);
        }
    }
}

/**
 * Sync recent files from recent_files.c system to Settings
 * Updates Settings->recent_files with current state from recent_files system
 * This is called by recent_files_save() to save to XML
 */
void settings_sync_recent_files_from_system(Settings* settings) {
    if (!settings) {
        return;
    }

    /* Clear existing paths in settings */
    if (settings->recent_files) {
        g_list_free_full(settings->recent_files, recent_file_free);
        settings->recent_files = NULL;
    }

    /* Copy RecentFile entries from recent_files system to settings */
    const GList* recent_files = recent_files_get();
    for (const GList* iter = recent_files; iter; iter = iter->next) {
        RecentFile* rf = (RecentFile*)iter->data;
        if (rf && rf->path) {
            /* Create a copy of the RecentFile to store in settings */
            RecentFile* new_rf = g_malloc(sizeof(RecentFile));
            new_rf->path = g_strdup(rf->path);
            new_rf->last_opened = rf->last_opened;
            settings->recent_files = g_list_prepend(settings->recent_files, new_rf);
        }
    }

    /* Limit to max_recent_files */
    guint count = g_list_length(settings->recent_files);
    if (count > settings->max_recent_files) {
        GList* to_remove = g_list_nth(settings->recent_files, settings->max_recent_files);
        if (to_remove) {
            GList* rest = to_remove->next;
            to_remove->next = NULL;
            g_list_free_full(rest, recent_file_free);
        }
    }
}

/**
 * Set a tool option value
 */
void settings_set_tool_option(Settings* settings, const char* tool, const char* key, const char* value) {
    if (!settings || !tool || !key || !value) {
        return;
    }

    /* Validate string parameters are not empty */
    if (tool[0] == '\0' || key[0] == '\0' || value[0] == '\0') {
        return;
    }

    /* Ensure tool_options hash table exists */
    if (!settings->tool_options) {
        settings->tool_options = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, tool_options_hash_table_free);
    }

    /* Safety check: verify hash table is still valid */
    if (!settings->tool_options) {
        return; /* Failed to create hash table */
    }

    /* Get or create tool options hash table */
    GHashTable* tool_opts = (GHashTable*)g_hash_table_lookup(settings->tool_options, tool);
    if (!tool_opts) {
        tool_opts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        if (!tool_opts) {
            return; /* Failed to create tool options hash table */
        }

        /* Duplicate tool name string before inserting */
        gchar* tool_dup = g_strdup(tool);
        if (!tool_dup) {
            g_hash_table_destroy(tool_opts);
            return; /* Failed to duplicate tool name */
        }

        /* Verify settings->tool_options is still valid before inserting */
        if (!settings->tool_options) {
            g_free(tool_dup);
            g_hash_table_destroy(tool_opts);
            return; /* Hash table was destroyed */
        }

        g_hash_table_insert(settings->tool_options, tool_dup, tool_opts);
    }

    /* Verify tool_opts is still valid */
    if (!tool_opts) {
        return; /* Tool options hash table is invalid */
    }

    /* Duplicate key and value strings before inserting */
    gchar* key_dup = g_strdup(key);
    gchar* value_dup = g_strdup(value);

    if (!key_dup || !value_dup) {
        if (key_dup)
            g_free(key_dup);
        if (value_dup)
            g_free(value_dup);
        return; /* Failed to duplicate strings */
    }

    /* Set option value - verify hash table is still valid */
    if (tool_opts && key_dup && value_dup) {
        /* All checks passed - safe to insert */
        g_hash_table_insert(tool_opts, key_dup, value_dup);
    } else {
        /* Hash table became invalid - free duplicated strings */
        if (key_dup)
            g_free(key_dup);
        if (value_dup)
            g_free(value_dup);
    }
}

/**
 * Get a tool option value
 */
const char* settings_get_tool_option(Settings* settings, const char* tool, const char* key) {
    if (!settings || !tool || !key) {
        return NULL;
    }

    GHashTable* tool_opts = (GHashTable*)g_hash_table_lookup(settings->tool_options, tool);
    if (!tool_opts) {
        return NULL;
    }

    return (const char*)g_hash_table_lookup(tool_opts, key);
}

/**
 * Set canvas background color
 */
void settings_set_canvas_background(Settings* settings, gdouble r, gdouble g, gdouble b) {
    if (!settings) {
        return;
    }

    settings->canvas_bg_r = (r < 0.0) ? 0.0 : ((r > 1.0) ? 1.0 : r);
    settings->canvas_bg_g = (g < 0.0) ? 0.0 : ((g > 1.0) ? 1.0 : g);
    settings->canvas_bg_b = (b < 0.0) ? 0.0 : ((b > 1.0) ? 1.0 : b);
}

/**
 * Get canvas background color
 */
void settings_get_canvas_background(Settings* settings, gdouble* r, gdouble* g, gdouble* b) {
    if (!settings || !r || !g || !b) {
        return;
    }

    *r = settings->canvas_bg_r;
    *g = settings->canvas_bg_g;
    *b = settings->canvas_bg_b;
}

/**
 * Get default tool option values
 */
gfloat settings_get_default_tool_size(void) {
    return DEFAULT_TOOL_SIZE;
}

gfloat settings_get_default_tool_opacity(void) {
    return DEFAULT_TOOL_OPACITY;
}

gfloat settings_get_default_tool_hardness(void) {
    return DEFAULT_TOOL_HARDNESS;
}

gfloat settings_get_default_tool_flow(void) {
    return DEFAULT_TOOL_FLOW;
}

gfloat settings_get_default_tool_spacing(void) {
    return DEFAULT_TOOL_SPACING;
}

gfloat settings_get_default_tool_tolerance(void) {
    return DEFAULT_TOOL_TOLERANCE;
}

gboolean settings_get_default_tool_fill_contiguous(void) {
    return DEFAULT_TOOL_FILL_CONTIGUOUS;
}

gboolean settings_get_default_tool_fill_antialiased(void) {
    return DEFAULT_TOOL_FILL_ANTIALIASED;
}

/**
 * Get undo compression level
 */
gint settings_get_undo_compression_level(Settings* settings) {
    if (!settings) {
        return DEFAULT_UNDO_COMPRESSION_LEVEL;
    }
    return settings->undo_compression_level;
}

/**
 * Set undo compression level
 */
void settings_set_undo_compression_level(Settings* settings, gint level) {
    if (!settings) {
        return;
    }
    /* Clamp to valid range */
    if (level < 1) {
        level = 1;
    } else if (level > 9) {
        level = 9;
    }
    settings->undo_compression_level = level;
}

/**
 * Get undo temp directory
 */
const gchar* settings_get_undo_temp_directory(Settings* settings) {
    if (!settings) {
        return NULL;
    }
    return settings->undo_temp_directory;
}

/**
 * Set undo temp directory
 */
void settings_set_undo_temp_directory(Settings* settings, const gchar* directory) {
    if (!settings) {
        return;
    }
    if (settings->undo_temp_directory) {
        g_free(settings->undo_temp_directory);
    }
    settings->undo_temp_directory = directory ? g_strdup(directory) : NULL;
}

/**
 * Get undo levels
 */
gint settings_get_undo_levels(Settings* settings) {
    if (!settings) {
        return DEFAULT_UNDO_LEVELS;
    }
    return settings->undo_levels;
}

/**
 * Set undo levels
 */
void settings_set_undo_levels(Settings* settings, gint levels) {
    if (!settings) {
        return;
    }
    /* Clamp to valid range (1-100) */
    if (levels < 1) {
        levels = 1;
    } else if (levels > 100) {
        levels = 100;
    }
    settings->undo_levels = levels;
}

/**
 * Get worker threads count
 */
gint settings_get_worker_threads(Settings* settings) {
    if (!settings) {
        return DEFAULT_WORKER_THREADS;
    }
    return settings->worker_threads;
}

/**
 * Set worker threads count
 */
void settings_set_worker_threads(Settings* settings, gint threads) {
    if (!settings) {
        return;
    }
    /* Clamp to valid range */
    gint cpu_count = (gint)g_get_num_processors();
    if (threads < 1) {
        threads = 1;
    } else if (threads > cpu_count) {
        threads = cpu_count;
    }
    settings->worker_threads = threads;
}

/**
 * Get file recovery interval in seconds
 */
gint settings_get_file_recovery_interval_seconds(Settings* settings) {
    if (!settings) {
        return DEFAULT_FILE_RECOVERY_INTERVAL_SECONDS;
    }
    return settings->file_recovery_interval_seconds;
}

/**
 * Set file recovery interval in seconds
 */
void settings_set_file_recovery_interval_seconds(Settings* settings, gint seconds) {
    if (!settings) {
        return;
    }
    settings->file_recovery_interval_seconds = max(30, min(2700, seconds));
}

/**
 * Get maximum number of recent files
 */
guint settings_get_max_recent_files(Settings* settings) {
    if (!settings) {
        return DEFAULT_MAX_RECENT_FILES;
    }
    return settings->max_recent_files;
}

/**
 * Set maximum number of recent files
 */
void settings_set_max_recent_files(Settings* settings, guint max_count) {
    if (!settings) {
        return;
    }
    settings->max_recent_files = (guint)max(MIN_MAX_RECENT_FILES, min(MAX_MAX_RECENT_FILES, (gint)max_count));
}

/**
 * Get show layer edges setting
 */
gboolean settings_get_show_layer_edges(Settings* settings) {
    if (!settings) {
        return TRUE; /* Default to showing edges */
    }
    return settings->show_layer_edges;
}

/**
 * Set show layer edges setting
 */
void settings_set_show_layer_edges(Settings* settings, gboolean show) {
    if (!settings) {
        return;
    }
    settings->show_layer_edges = show;
}

void settings_set_show_smart_guides(Settings* settings, gboolean show) {
    if (!settings) {
        return;
    }
    settings->show_smart_guides = show ? TRUE : FALSE;
}

gboolean settings_get_show_smart_guides(Settings* settings) {
    if (!settings) {
        return DEFAULT_SHOW_SMART_GUIDES;
    }
    return settings->show_smart_guides;
}

/**
 * Get show status bar setting
 */
gboolean settings_get_show_statusbar(Settings* settings) {
    if (!settings) {
        return TRUE; /* Default to showing status bar */
    }
    return settings->show_statusbar;
}

/**
 * Set show status bar setting
 */
void settings_set_show_statusbar(Settings* settings, gboolean show) {
    if (!settings) {
        return;
    }
    settings->show_statusbar = show;
}

/**
 * Get show rulers setting
 */
gboolean settings_get_show_rulers(Settings* settings) {
    if (!settings) {
        return TRUE; /* Default to showing rulers */
    }
    return settings->show_rulers;
}

/**
 * Set show rulers setting
 */
void settings_set_show_rulers(Settings* settings, gboolean show) {
    if (!settings) {
        return;
    }
    settings->show_rulers = show;
}

/**
 * Get show GPU stats setting
 */
gboolean settings_get_show_gpu_stats(Settings* settings) {
    if (!settings) {
        return FALSE; /* Default to hiding GPU stats */
    }
    return settings->show_gpu_stats;
}

/**
 * Set show GPU stats setting
 */
void settings_set_show_gpu_stats(Settings* settings, gboolean show) {
    if (!settings) {
        return;
    }
    settings->show_gpu_stats = show;
}

void settings_set_mouse_snap_distance(Settings* settings, gint distance) {
    if (!settings) {
        return;
    }
    if (distance < MIN_MOUSE_SNAP_DISTANCE) {
        distance = MIN_MOUSE_SNAP_DISTANCE;
    }
    if (distance > MAX_MOUSE_SNAP_DISTANCE) {
        distance = MAX_MOUSE_SNAP_DISTANCE;
    }
    settings->mouse_snap_distance = distance;
}

gint settings_get_mouse_snap_distance(Settings* settings) {
    if (!settings) {
        return DEFAULT_MOUSE_SNAP_DISTANCE;
    }
    return settings->mouse_snap_distance;
}

void settings_set_mouse_snap(Settings* settings, gboolean enabled) {
    if (!settings) {
        return;
    }
    settings->mouse_snap = enabled ? TRUE : FALSE;
}

gboolean settings_get_mouse_snap(Settings* settings) {
    if (!settings) {
        return TRUE;
    }
    return settings->mouse_snap;
}

void settings_set_mouse_snap_to_canvas_edges(Settings* settings, gboolean enabled) {
    if (!settings) {
        return;
    }
    settings->mouse_snap_to_canvas_edges = enabled ? TRUE : FALSE;
}

gboolean settings_get_mouse_snap_to_canvas_edges(Settings* settings) {
    if (!settings) {
        return TRUE;
    }
    return settings->mouse_snap_to_canvas_edges;
}

void settings_set_mouse_snap_to_centerlines(Settings* settings, gboolean enabled) {
    if (!settings) {
        return;
    }
    settings->mouse_snap_to_centerlines = enabled ? TRUE : FALSE;
}

gboolean settings_get_mouse_snap_to_centerlines(Settings* settings) {
    if (!settings) {
        return TRUE;
    }
    return settings->mouse_snap_to_centerlines;
}

void settings_set_mouse_snap_to_layers(Settings* settings, gboolean enabled) {
    if (!settings) {
        return;
    }
    settings->mouse_snap_to_layers = enabled ? TRUE : FALSE;
}

gboolean settings_get_mouse_snap_to_layers(Settings* settings) {
    if (!settings) {
        return FALSE;
    }
    return settings->mouse_snap_to_layers;
}

/**
 * Alpha checkerboard size: 0=Small (8px), 1=Medium (16px), 2=Large (32px)
 */
void settings_set_alpha_check_size(Settings* settings, gint size) {
    if (!settings) {
        return;
    }
    if (size < 0) {
        size = 0;
    }
    if (size > 2) {
        size = 2;
    }
    settings->alpha_check_size = size;
}

gint settings_get_alpha_check_size(Settings* settings) {
    if (!settings) {
        return 1; /* Default Medium */
    }
    return settings->alpha_check_size;
}

static gboolean parse_rgb_string(const char* str, gdouble* r, gdouble* g, gdouble* b) {
    if (!str || !r || !g || !b) {
        return FALSE;
    }
    gint ri = 0, gi = 0, bi = 0;
    if (sscanf(str, "%d,%d,%d", &ri, &gi, &bi) != 3) {
        return FALSE;
    }
    if (ri < 0)
        ri = 0;
    if (ri > 255)
        ri = 255;
    if (gi < 0)
        gi = 0;
    if (gi > 255)
        gi = 255;
    if (bi < 0)
        bi = 0;
    if (bi > 255)
        bi = 255;
    *r = (gdouble)ri / 255.0;
    *g = (gdouble)gi / 255.0;
    *b = (gdouble)bi / 255.0;
    return TRUE;
}

void settings_set_alpha_color_one(Settings* settings, gdouble r, gdouble g, gdouble b) {
    if (!settings) {
        return;
    }
    settings->alpha_color_one_r = (r < 0.0) ? 0.0 : ((r > 1.0) ? 1.0 : r);
    settings->alpha_color_one_g = (g < 0.0) ? 0.0 : ((g > 1.0) ? 1.0 : g);
    settings->alpha_color_one_b = (b < 0.0) ? 0.0 : ((b > 1.0) ? 1.0 : b);
}

void settings_get_alpha_color_one(Settings* settings, gdouble* r, gdouble* g, gdouble* b) {
    if (!settings || !r || !g || !b) {
        return;
    }
    *r = settings->alpha_color_one_r;
    *g = settings->alpha_color_one_g;
    *b = settings->alpha_color_one_b;
}

void settings_set_alpha_color_two(Settings* settings, gdouble r, gdouble g, gdouble b) {
    if (!settings) {
        return;
    }
    settings->alpha_color_two_r = (r < 0.0) ? 0.0 : ((r > 1.0) ? 1.0 : r);
    settings->alpha_color_two_g = (g < 0.0) ? 0.0 : ((g > 1.0) ? 1.0 : g);
    settings->alpha_color_two_b = (b < 0.0) ? 0.0 : ((b > 1.0) ? 1.0 : b);
}

void settings_get_alpha_color_two(Settings* settings, gdouble* r, gdouble* g, gdouble* b) {
    if (!settings || !r || !g || !b) {
        return;
    }
    *r = settings->alpha_color_two_r;
    *g = settings->alpha_color_two_g;
    *b = settings->alpha_color_two_b;
}

/**
 * Load tone mapping settings from XML
 */
static void settings_load_tone_mapping(Settings* settings, xmlNode* tone_mapping_node) {
    if (!settings || !tone_mapping_node) {
        return;
    }

    /* Load auto_apply */
    xmlChar* auto_apply_attr = xmlGetProp(tone_mapping_node, (const xmlChar*)"auto_apply");
    if (auto_apply_attr) {
        if (xmlStrcmp(auto_apply_attr, (const xmlChar*)"true") == 0) {
            settings->tone_map_auto_apply = TRUE;
        } else {
            settings->tone_map_auto_apply = FALSE;
        }
        xmlFree(auto_apply_attr);
    }

    /* Load operator */
    xmlChar* operator_attr = xmlGetProp(tone_mapping_node, (const xmlChar*)"operator");
    if (operator_attr) {
        gint op = (gint)strtol((const char*)operator_attr, NULL, 10);
        if (op >= 0 && op <= 3) {
            settings->tone_map_operator = op;
        }
        xmlFree(operator_attr);
    }

    /* Load normalize */
    xmlChar* normalize_attr = xmlGetProp(tone_mapping_node, (const xmlChar*)"normalize");
    if (normalize_attr) {
        gint norm = (gint)strtol((const char*)normalize_attr, NULL, 10);
        if (norm >= 0 && norm <= 2) {
            settings->tone_map_normalize = norm;
        }
        xmlFree(normalize_attr);
    }

    /* Load gamma */
    xmlChar* gamma_attr = xmlGetProp(tone_mapping_node, (const xmlChar*)"gamma");
    if (gamma_attr) {
        gdouble gamma = g_strtod((const char*)gamma_attr, NULL);
        if (gamma >= 1.0 && gamma <= 5.0) {
            settings->tone_map_gamma = gamma;
        }
        xmlFree(gamma_attr);
    }

    /* Load exposure */
    xmlChar* exposure_attr = xmlGetProp(tone_mapping_node, (const xmlChar*)"exposure");
    if (exposure_attr) {
        gdouble exposure = g_strtod((const char*)exposure_attr, NULL);
        if (exposure >= 0.01 && exposure <= 8.0) {
            settings->tone_map_exposure = exposure;
        }
        xmlFree(exposure_attr);
    }

    /* Load white_point */
    xmlChar* white_point_attr = xmlGetProp(tone_mapping_node, (const xmlChar*)"white_point");
    if (white_point_attr) {
        gdouble white_point = g_strtod((const char*)white_point_attr, NULL);
        if (white_point >= 1.0 && white_point <= 40.0) {
            settings->tone_map_white_point = white_point;
        }
        xmlFree(white_point_attr);
    }

    /* Load intensity */
    xmlChar* intensity_attr = xmlGetProp(tone_mapping_node, (const xmlChar*)"intensity");
    if (intensity_attr) {
        gdouble intensity = g_strtod((const char*)intensity_attr, NULL);
        if (intensity >= -4.0 && intensity <= 4.0) {
            settings->tone_map_intensity = intensity;
        }
        xmlFree(intensity_attr);
    }

    /* Load adaptation */
    xmlChar* adaptation_attr = xmlGetProp(tone_mapping_node, (const xmlChar*)"adaptation");
    if (adaptation_attr) {
        gdouble adaptation = g_strtod((const char*)adaptation_attr, NULL);
        if (adaptation >= 0.0 && adaptation <= 1.0) {
            settings->tone_map_adaptation = adaptation;
        }
        xmlFree(adaptation_attr);
    }

    /* Load color_correction */
    xmlChar* color_correction_attr = xmlGetProp(tone_mapping_node, (const xmlChar*)"color_correction");
    if (color_correction_attr) {
        gdouble color_correction = g_strtod((const char*)color_correction_attr, NULL);
        if (color_correction >= 0.0 && color_correction <= 1.0) {
            settings->tone_map_color_correction = color_correction;
        }
        xmlFree(color_correction_attr);
    }
}

/**
 * Save tone mapping settings to XML
 */
static void settings_save_tone_mapping(xmlTextWriterPtr writer, Settings* settings) {
    if (!writer || !settings) {
        return;
    }

    xmlTextWriterStartElement(writer, (const xmlChar*)"tone_mapping");

    /* Save auto_apply */
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"auto_apply",
                                (const xmlChar*)(settings->tone_map_auto_apply ? "true" : "false"));

    /* Save operator */
    gchar operator_str[16];
    g_snprintf(operator_str, sizeof(operator_str), "%d", settings->tone_map_operator);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"operator", (const xmlChar*)operator_str);

    /* Save normalize */
    gchar normalize_str[16];
    g_snprintf(normalize_str, sizeof(normalize_str), "%d", settings->tone_map_normalize);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"normalize", (const xmlChar*)normalize_str);

    /* Save gamma */
    gchar gamma_str[32];
    g_snprintf(gamma_str, sizeof(gamma_str), "%.2f", settings->tone_map_gamma);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"gamma", (const xmlChar*)gamma_str);

    /* Save exposure */
    gchar exposure_str[32];
    g_snprintf(exposure_str, sizeof(exposure_str), "%.2f", settings->tone_map_exposure);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"exposure", (const xmlChar*)exposure_str);

    /* Save white_point */
    gchar white_point_str[32];
    g_snprintf(white_point_str, sizeof(white_point_str), "%.2f", settings->tone_map_white_point);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"white_point", (const xmlChar*)white_point_str);

    /* Save intensity */
    gchar intensity_str[32];
    g_snprintf(intensity_str, sizeof(intensity_str), "%.2f", settings->tone_map_intensity);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"intensity", (const xmlChar*)intensity_str);

    /* Save adaptation */
    gchar adaptation_str[32];
    g_snprintf(adaptation_str, sizeof(adaptation_str), "%.2f", settings->tone_map_adaptation);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"adaptation", (const xmlChar*)adaptation_str);

    /* Save color_correction */
    gchar color_correction_str[32];
    g_snprintf(color_correction_str, sizeof(color_correction_str), "%.2f", settings->tone_map_color_correction);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"color_correction", (const xmlChar*)color_correction_str);

    xmlTextWriterEndElement(writer); /* tone_mapping */
}

/**
 * Load Advanced settings from XML (temp file directory, etc.)
 */
static void settings_load_advanced(Settings* settings, xmlNode* advanced_node) {
    if (!settings || !advanced_node) {
        return;
    }

    /* Load temp_directory for undo/scratch files (default: system temp) */
    xmlChar* temp_dir_attr = xmlGetProp(advanced_node, (const xmlChar*)"temp_directory");
    if (temp_dir_attr) {
        settings_set_undo_temp_directory(settings, (const char*)temp_dir_attr);
        xmlFree(temp_dir_attr);
    }
}

/**
 * Save Advanced settings to XML
 */
static void settings_save_advanced(xmlTextWriterPtr writer, Settings* settings) {
    if (!writer || !settings) {
        return;
    }

    xmlTextWriterStartElement(writer, (const xmlChar*)"advanced");

    /* Save temp directory if set (NULL/empty = use system temp) */
    if (settings->undo_temp_directory && settings->undo_temp_directory[0] != '\0') {
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"temp_directory",
                                    (const xmlChar*)settings->undo_temp_directory);
    }

    xmlTextWriterEndElement(writer); /* advanced */
}

/**
 * Load color management settings from XML
 */
static void settings_load_color_management(Settings* settings, xmlNode* color_management_node) {
    if (!settings || !color_management_node) {
        return;
    }

    xmlChar* intent_attr = xmlGetProp(color_management_node, (const xmlChar*)"rendering_intent");
    if (intent_attr) {
        gint v = (gint)strtol((const char*)intent_attr, NULL, 10);
        if (v >= 0 && v <= 3) {
            settings->cm_rendering_intent = v;
        }
        xmlFree(intent_attr);
    }

    xmlChar* bpc_attr = xmlGetProp(color_management_node, (const xmlChar*)"black_point_compensation");
    if (bpc_attr) {
        settings->cm_black_point_compensation = (xmlStrcmp(bpc_attr, (const xmlChar*)"true") == 0);
        xmlFree(bpc_attr);
    }

    xmlChar* embedded_attr = xmlGetProp(color_management_node, (const xmlChar*)"use_embedded_icc");
    if (embedded_attr) {
        settings->cm_use_embedded_icc = (xmlStrcmp(embedded_attr, (const xmlChar*)"true") == 0);
        xmlFree(embedded_attr);
    }

    xmlChar* mode_attr = xmlGetProp(color_management_node, (const xmlChar*)"mode");
    if (mode_attr) {
        gint v = (gint)strtol((const char*)mode_attr, NULL, 10);
        if (v >= 0 && v <= 2) {
            settings->cm_mode = v;
        }
        xmlFree(mode_attr);
    }

    /* Child elements: <display id="..." profile_path="..."/> */
    if (!settings->cm_display_profiles) {
        settings->cm_display_profiles = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    }
    g_hash_table_remove_all(settings->cm_display_profiles);

    for (xmlNode* cur = color_management_node->children; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE || xmlStrcmp(cur->name, (const xmlChar*)"display") != 0) {
            continue;
        }
        xmlChar* id_attr = xmlGetProp(cur, (const xmlChar*)"id");
        xmlChar* path_attr = xmlGetProp(cur, (const xmlChar*)"profile_path");
        if (id_attr && path_attr) {
            const char* id_str = (const char*)id_attr;
            const char* path_str = (const char*)path_attr;
            /* Strip " (display_id)" suffix if present (format we write for human readability) */
            gchar* path = g_strdup(path_str);
            size_t id_len = strlen(id_str);
            size_t path_len = strlen(path);
            if (path_len > id_len + 3) {
                const char* suffix = path + path_len - id_len - 3;
                if (suffix[0] == ' ' && suffix[1] == '(' &&
                    strncmp(suffix + 2, id_str, id_len) == 0 && suffix[2 + id_len] == ')') {
                    path[path_len - id_len - 3] = '\0';
                    if (path[path_len - id_len - 4] == ' ') {
                        path[path_len - id_len - 4] = '\0';
                    }
                }
            }
            g_hash_table_insert(settings->cm_display_profiles,
                                g_strdup(id_str),
                                path);
        }
        if (id_attr)
            xmlFree(id_attr);
        if (path_attr)
            xmlFree(path_attr);
    }
}

/**
 * Save color management settings to XML
 */
static void settings_save_color_management(xmlTextWriterPtr writer, Settings* settings) {
    if (!writer || !settings) {
        return;
    }

    xmlTextWriterStartElement(writer, (const xmlChar*)"color_management");

    gchar intent_str[8];
    g_snprintf(intent_str, sizeof(intent_str), "%d", settings->cm_rendering_intent);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"rendering_intent", (const xmlChar*)intent_str);

    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"black_point_compensation",
                                (const xmlChar*)(settings->cm_black_point_compensation ? "true" : "false"));

    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"use_embedded_icc",
                                (const xmlChar*)(settings->cm_use_embedded_icc ? "true" : "false"));

    gchar mode_str[8];
    g_snprintf(mode_str, sizeof(mode_str), "%d", settings->cm_mode);
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"mode", (const xmlChar*)mode_str);

    if (settings->cm_display_profiles) {
        GHashTableIter iter;
        gpointer k, v;
        g_hash_table_iter_init(&iter, settings->cm_display_profiles);
        while (g_hash_table_iter_next(&iter, &k, &v)) {
            const gchar* display_id = (const gchar*)k;
            const gchar* profile_path = (const gchar*)v;
            if (display_id && profile_path) {
                xmlTextWriterStartElement(writer, (const xmlChar*)"display");
                xmlTextWriterWriteAttribute(writer, (const xmlChar*)"id", (const xmlChar*)display_id);
                /* Append display_id to profile_path so we know what display it is assigned to */
                gchar* profile_with_display = g_strdup_printf("%s (%s)", profile_path, display_id);
                xmlTextWriterWriteAttribute(writer, (const xmlChar*)"profile_path", (const xmlChar*)profile_with_display);
                g_free(profile_with_display);
                xmlTextWriterEndElement(writer); /* display */
            }
        }
    }

    xmlTextWriterEndElement(writer); /* color_management */
}

/**
 * Tone mapping settings getters/setters
 */
void settings_set_tone_map_auto_apply(Settings* settings, gboolean auto_apply) {
    if (!settings) {
        return;
    }
    settings->tone_map_auto_apply = auto_apply;
}

gboolean settings_get_tone_map_auto_apply(Settings* settings) {
    if (!settings) {
        return FALSE;
    }
    return settings->tone_map_auto_apply;
}

void settings_set_tone_map_operator(Settings* settings, gint operator) {
    if (!settings) {
        return;
    }
    if (operator>= 0 && operator<= 3) {
        settings->tone_map_operator = operator;
    }
}

gint settings_get_tone_map_operator(Settings* settings) {
    if (!settings) {
        return 0;
    }
    return settings->tone_map_operator;
}

void settings_set_tone_map_normalize(Settings* settings, gint normalize) {
    if (!settings) {
        return;
    }
    if (normalize >= 0 && normalize <= 2) {
        settings->tone_map_normalize = normalize;
    }
}

gint settings_get_tone_map_normalize(Settings* settings) {
    if (!settings) {
        return 0;
    }
    return settings->tone_map_normalize;
}

void settings_set_tone_map_gamma(Settings* settings, gdouble gamma) {
    if (!settings) {
        return;
    }
    if (gamma >= 1.0 && gamma <= 5.0) {
        settings->tone_map_gamma = gamma;
    }
}

gdouble settings_get_tone_map_gamma(Settings* settings) {
    if (!settings) {
        return 2.20;
    }
    return settings->tone_map_gamma;
}

void settings_set_tone_map_exposure(Settings* settings, gdouble exposure) {
    if (!settings) {
        return;
    }
    if (exposure >= 0.01 && exposure <= 8.0) {
        settings->tone_map_exposure = exposure;
    }
}

gdouble settings_get_tone_map_exposure(Settings* settings) {
    if (!settings) {
        return 2.00;
    }
    return settings->tone_map_exposure;
}

void settings_set_tone_map_white_point(Settings* settings, gdouble white_point) {
    if (!settings) {
        return;
    }
    if (white_point >= 1.0 && white_point <= 40.0) {
        settings->tone_map_white_point = white_point;
    }
}

gdouble settings_get_tone_map_white_point(Settings* settings) {
    if (!settings) {
        return 11.20;
    }
    return settings->tone_map_white_point;
}

void settings_set_tone_map_intensity(Settings* settings, gdouble intensity) {
    if (!settings) {
        return;
    }
    if (intensity >= -4.0 && intensity <= 4.0) {
        settings->tone_map_intensity = intensity;
    }
}

gdouble settings_get_tone_map_intensity(Settings* settings) {
    if (!settings) {
        return 0.00;
    }
    return settings->tone_map_intensity;
}

void settings_set_tone_map_adaptation(Settings* settings, gdouble adaptation) {
    if (!settings) {
        return;
    }
    if (adaptation >= 0.0 && adaptation <= 1.0) {
        settings->tone_map_adaptation = adaptation;
    }
}

gdouble settings_get_tone_map_adaptation(Settings* settings) {
    if (!settings) {
        return 1.00;
    }
    return settings->tone_map_adaptation;
}

void settings_set_tone_map_color_correction(Settings* settings, gdouble color_correction) {
    if (!settings) {
        return;
    }
    if (color_correction >= 0.0 && color_correction <= 1.0) {
        settings->tone_map_color_correction = color_correction;
    }
}

gdouble settings_get_tone_map_color_correction(Settings* settings) {
    if (!settings) {
        return 0.00;
    }
    return settings->tone_map_color_correction;
}

/**
 * Get GPU acceleration enabled setting
 */
gboolean settings_get_gpu_acceleration_enabled(Settings* settings) {
    if (!settings) {
        return DEFAULT_GPU_ACCELERATION;
    }
    return settings->gpu_acceleration_enabled;
}

/**
 * Set GPU acceleration enabled setting
 */
void settings_set_gpu_acceleration_enabled(Settings* settings, gboolean enabled) {
    if (!settings) {
        return;
    }
    settings->gpu_acceleration_enabled = enabled;
}

/**
 * Get GPU device name
 */
const gchar* settings_get_gpu_device_name(Settings* settings) {
    if (!settings) {
        return NULL;
    }
    return settings->gpu_device_name;
}

/**
 * Set GPU device name
 */
void settings_set_gpu_device_name(Settings* settings, const gchar* device_name) {
    if (!settings) {
        return;
    }
    if (settings->gpu_device_name) {
        g_free(settings->gpu_device_name);
    }
    settings->gpu_device_name = device_name ? g_strdup(device_name) : NULL;
}

/* Color management getters/setters */

void settings_set_cm_rendering_intent(Settings* settings, gint intent) {
    if (!settings)
        return;
    if (intent >= 0 && intent <= 3) {
        settings->cm_rendering_intent = intent;
    }
}

gint settings_get_cm_rendering_intent(Settings* settings) {
    if (!settings)
        return 1; /* RELATIVE_COLORIMETRIC */
    return settings->cm_rendering_intent;
}

void settings_set_cm_black_point_compensation(Settings* settings, gboolean use) {
    if (!settings)
        return;
    settings->cm_black_point_compensation = use;
}

gboolean settings_get_cm_black_point_compensation(Settings* settings) {
    if (!settings)
        return TRUE;
    return settings->cm_black_point_compensation;
}

void settings_set_cm_use_embedded_icc(Settings* settings, gboolean use) {
    if (!settings)
        return;
    settings->cm_use_embedded_icc = use;
}

gboolean settings_get_cm_use_embedded_icc(Settings* settings) {
    if (!settings)
        return TRUE;
    return settings->cm_use_embedded_icc;
}

void settings_set_cm_mode(Settings* settings, gint mode) {
    if (!settings)
        return;
    if (mode >= 0 && mode <= 2) {
        settings->cm_mode = mode;
    }
}

gint settings_get_cm_mode(Settings* settings) {
    if (!settings)
        return 0; /* CM_MODE_SYSTEM_PROFILE */
    return settings->cm_mode;
}

void settings_set_cm_display_profile(Settings* settings, const gchar* display_id, const gchar* profile_path) {
    if (!settings || !display_id)
        return;
    if (profile_path && profile_path[0] != '\0') {
        if (!settings->cm_display_profiles) {
            settings->cm_display_profiles = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        }
        g_hash_table_insert(settings->cm_display_profiles, g_strdup(display_id), g_strdup(profile_path));
    } else if (settings->cm_display_profiles) {
        g_hash_table_remove(settings->cm_display_profiles, display_id);
    }
}

const gchar* settings_get_cm_display_profile(Settings* settings, const gchar* display_id) {
    if (!settings || !display_id || !settings->cm_display_profiles)
        return NULL;
    return (const gchar*)g_hash_table_lookup(settings->cm_display_profiles, display_id);
}

void settings_set_interface_locale(Settings* settings, const gchar* locale) {
    if (!settings) {
        return;
    }
    g_free(settings->interface_locale);
    if (locale && locale[0]) {
        settings->interface_locale = g_strdup(locale);
    } else {
        settings->interface_locale = g_strdup(DEFAULT_INTERFACE_LOCALE);
    }
}

const gchar* settings_get_interface_locale(Settings* settings) {
    if (!settings || !settings->interface_locale || !settings->interface_locale[0]) {
        return DEFAULT_INTERFACE_LOCALE;
    }
    return settings->interface_locale;
}
