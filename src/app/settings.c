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
#define DEFAULT_UNDO_COMPRESSION_LEVEL 1 /* LZ4 fast compression */

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
    settings->show_layer_edges = TRUE;    /* Show layer edges by default */
    settings->show_statusbar = TRUE;      /* Show status bar by default */

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
 * Load view settings from XML (forward declaration)
 */
static void settings_load_view(Settings* settings, xmlNode* view_node);

/**
 * Save view settings to XML (forward declaration)
 */
static void settings_save_view(xmlTextWriterPtr writer, Settings* settings);

/**
 * Load recent files from XML
 * Stores RecentFile* entries in Settings->recent_files (includes path and timestamp)
 */
static void settings_load_recent_files(Settings* settings, xmlNode* recent_files_node) {
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

        if (xmlStrcmp(cur->name, (const xmlChar*)"canvas") == 0) {
            settings_load_canvas(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"tools") == 0) {
            settings_load_tools(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"recent_files") == 0) {
            settings_load_recent_files(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"undo") == 0) {
            settings_load_undo(settings, cur);
        } else if (xmlStrcmp(cur->name, (const xmlChar*)"view") == 0) {
            settings_load_view(settings, cur);
        }
    }

    xmlFreeDoc(doc);
    g_free(file_path);

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

    /* Load temp directory */
    xmlChar* temp_dir_attr = xmlGetProp(undo_node, (const xmlChar*)"temp_directory");
    if (temp_dir_attr) {
        settings_set_undo_temp_directory(settings, (const char*)temp_dir_attr);
        xmlFree(temp_dir_attr);
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

    /* Save temp directory if set */
    if (settings->undo_temp_directory) {
        xmlTextWriterWriteAttribute(writer, (const xmlChar*)"temp_directory", (const xmlChar*)settings->undo_temp_directory);
    }

    xmlTextWriterEndElement(writer); /* undo */
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

    /* Save show_statusbar setting */
    xmlTextWriterStartElement(writer, (const xmlChar*)"show_statusbar");
    xmlTextWriterWriteAttribute(writer, (const xmlChar*)"value",
                                (const xmlChar*)(settings->show_statusbar ? "true" : "false"));
    xmlTextWriterEndElement(writer); /* show_statusbar */

    xmlTextWriterEndElement(writer); /* view */
}

/**
 * Save canvas background color to XML
 */
static void settings_save_canvas(xmlTextWriterPtr writer, Settings* settings) {
    xmlTextWriterStartElement(writer, (const xmlChar*)"canvas");
    xmlTextWriterStartElement(writer, (const xmlChar*)"background");

    /* Convert to 0-255 range */
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

    xmlTextWriterEndElement(writer); /* background */
    xmlTextWriterEndElement(writer); /* canvas */
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

    /* Write canvas settings */
    settings_save_canvas(writer, settings);

    /* Write tool options */
    settings_save_tools(writer, settings);

    /* Write recent files */
    settings_save_recent_files(writer, settings);

    /* Write undo settings */
    settings_save_undo(writer, settings);

    /* Write view settings */
    settings_save_view(writer, settings);

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
            g_warning("Failed to rename settings file: %s -> %s", temp_path, file_path);
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
