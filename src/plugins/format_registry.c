#include "plugins/format_registry.h"
#include "document.h"
#include "image_format_plugin.h"
#include "plugins/plugin_host_api.h"
#include "plugins/plugin_loader.h"
#include "render/compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include <ctype.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

/* Format registry state */
static GList* format_handlers = NULL;
static gboolean registry_initialized = FALSE;

/**
 * Helper function to extract file extension
 */
static gchar* get_file_extension(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return NULL;
    }

    gchar* ext = g_strdup(dot + 1);
    /* Convert to lowercase */
    for (gchar* p = ext; *p; p++) {
        *p = tolower((unsigned char)*p);
    }

    return ext;
}

/**
 * Check if extension matches format info
 */
static gboolean extension_matches(const char* extension, const char* extensions_str) {
    if (!extension || !extensions_str) {
        return FALSE;
    }

    gchar** exts = g_strsplit(extensions_str, ",", -1);
    gboolean found = FALSE;

    for (gint i = 0; exts[i]; i++) {
        /* Trim whitespace */
        g_strstrip(exts[i]);
        if (g_ascii_strcasecmp(extension, exts[i]) == 0) {
            found = TRUE;
            break;
        }
    }

    g_strfreev(exts);
    return found;
}

/**
 * Initialize the format registry
 */
void format_registry_init(void) {
    if (registry_initialized) {
        return;
    }

    format_handlers = NULL;
    registry_initialized = TRUE;
}

/**
 * Shutdown the format registry
 */
void format_registry_shutdown(void) {
    if (!registry_initialized) {
        return;
    }

    /* Free handler structures (but don't unload plugins here, let plugin_loader do it) */
    for (GList* iter = format_handlers; iter; iter = iter->next) {
        FormatHandler* handler = (FormatHandler*)iter->data;
        /* Free plugin structure if it was allocated (built-in plugins) */
        if (handler->plugin_handle == NULL && handler->plugin) {
            g_free(handler->plugin);
        }
        g_free(handler);
    }

    g_list_free(format_handlers);
    format_handlers = NULL;
    registry_initialized = FALSE;
}

/**
 * Register a format plugin
 */
gboolean format_registry_register(PluginHandle* plugin_handle, ImageFormatPlugin* plugin) {
    FormatHandler* handler;
    ImageFormatHostAPI* host_api;

    if (!registry_initialized) {
        g_warning("Format registry not initialized");
        return FALSE;
    }

    if (!plugin_handle || !plugin) {
        g_warning("Invalid parameters for format_registry_register");
        return FALSE;
    }

    /* Initialize plugin with host API if not already done */
    host_api = plugin_host_api_get();

    /* Check if plugin is already initialized */
    ImageFormatPlugin* existing_plugin = plugin_loader_get_plugin(plugin_handle);
    if (!existing_plugin) {
        /* Initialize plugin */
        g_message("Initializing plugin before registration");
        if (!plugin_loader_init_with_host(plugin_handle, host_api)) {
            g_warning("Failed to initialize plugin");
            g_message("Plugin registration failed (initialization failed)");
            return FALSE;
        }
        plugin = plugin_loader_get_plugin(plugin_handle);
        if (!plugin) {
            g_warning("Failed to get plugin after initialization");
            return FALSE;
        }
    } else {
        plugin = existing_plugin;
    }

    /* Validate plugin callbacks */
    if (!plugin->callbacks.can_load || !plugin->callbacks.load) {
        g_warning("Plugin missing required load callbacks");
        return FALSE;
    }

    /* can_save is required for all plugins, but save can be NULL (for read-only plugins) */
    if (!plugin->callbacks.can_save) {
        g_warning("Plugin missing required can_save callback");
        return FALSE;
    }

    /* Create handler */
    handler = g_malloc0(sizeof(FormatHandler));
    handler->plugin_handle = plugin_handle;
    handler->plugin = plugin;

    /* Get format info */
    if (plugin->callbacks.get_format_info) {
        FormatInfo* info = plugin->callbacks.get_format_info();
        if (info) {
            handler->format_info = *info;
        } else {
            handler->format_info = plugin->format_info;
        }
    } else {
        handler->format_info = plugin->format_info;
    }

    handler->priority = handler->format_info.priority;

    /* Add to registry */
    format_handlers = g_list_append(format_handlers, handler);

    if (plugin && plugin->format_info.name) {
        g_message("Successfully registered plugin: %s (extensions: %s)",
                  plugin->format_info.name,
                  plugin->format_info.extensions ? plugin->format_info.extensions : "none");
    } else {
        g_message("Successfully registered plugin (from handle)");
    }

    return TRUE;
}

/**
 * Register a built-in plugin directly
 */
gboolean format_registry_register_builtin(ImageFormatPlugin* plugin) {
    FormatHandler* handler;

    if (!registry_initialized) {
        g_warning("Format registry not initialized");
        return FALSE;
    }

    if (!plugin) {
        g_warning("Invalid parameters for format_registry_register_builtin");
        return FALSE;
    }

    /* Validate plugin callbacks */
    if (!plugin->callbacks.can_load || !plugin->callbacks.load) {
        g_warning("Plugin missing required load callbacks");
        return FALSE;
    }

    /* can_save is required for all plugins, but save can be NULL (for read-only plugins) */
    if (!plugin->callbacks.can_save) {
        g_warning("Plugin doesn't support saving");
        return FALSE;
    }

    /* Create handler with copied plugin structure */
    handler = g_malloc0(sizeof(FormatHandler));
    handler->plugin_handle = NULL; /* NULL means built-in plugin */
    handler->plugin = g_malloc(sizeof(ImageFormatPlugin));
    *handler->plugin = *plugin; /* Copy plugin structure */

    handler->format_info = plugin->format_info;
    handler->priority = plugin->format_info.priority;

    /* Add to registry */
    format_handlers = g_list_append(format_handlers, handler);

    g_message("Successfully registered built-in plugin: %s (extensions: %s)",
              plugin->format_info.name,
              plugin->format_info.extensions ? plugin->format_info.extensions : "none");

    return TRUE;
}

/**
 * Find plugin handler for loading a file
 */
FormatHandler* format_registry_find_loader(const char* filename,
                                           const uint8_t* header,
                                           size_t header_size) {
    FormatHandler* best_handler = NULL;
    int32_t best_priority = INT32_MIN;

    if (!registry_initialized || !filename) {
        return NULL;
    }

    /* Get file extension */
    gchar* ext = get_file_extension(filename);

    /* Find best matching handler */
    for (GList* iter = format_handlers; iter; iter = iter->next) {
        FormatHandler* handler = (FormatHandler*)iter->data;

        /* Check extension match */
        if (ext && extension_matches(ext, handler->format_info.extensions)) {
            /* Try can_load to verify format */
            if (handler->plugin->callbacks.can_load && header && header_size > 0) {
                if (handler->plugin->callbacks.can_load(filename, header, header_size)) {
                    if (handler->priority > best_priority) {
                        best_handler = handler;
                        best_priority = handler->priority;
                    }
                }
            } else if (handler->priority > best_priority) {
                /* No header checking available, use priority */
                best_handler = handler;
                best_priority = handler->priority;
            }
        }
    }

    g_free(ext);

    /* If no match by extension, try header-only matching */
    if (!best_handler && header && header_size > 0) {
        for (GList* iter = format_handlers; iter; iter = iter->next) {
            FormatHandler* handler = (FormatHandler*)iter->data;
            if (handler->plugin->callbacks.can_load) {
                if (handler->plugin->callbacks.can_load(filename, header, header_size)) {
                    if (handler->priority > best_priority) {
                        best_handler = handler;
                        best_priority = handler->priority;
                    }
                }
            }
        }
    }

    return best_handler;
}

/**
 * Find plugin handler for saving a file
 */
FormatHandler* format_registry_find_saver(const char* filename) {
    FormatHandler* best_handler = NULL;
    int32_t best_priority = INT32_MIN;

    if (!registry_initialized || !filename) {
        return NULL;
    }

    /* Get file extension */
    gchar* ext = get_file_extension(filename);
    if (!ext) {
        return NULL;
    }

    /* Find best matching handler */
    for (GList* iter = format_handlers; iter; iter = iter->next) {
        FormatHandler* handler = (FormatHandler*)iter->data;

        if (extension_matches(ext, handler->format_info.extensions)) {
            /* Check if plugin can save */
            if (handler->plugin->callbacks.can_save) {
                if (handler->plugin->callbacks.can_save(filename)) {
                    if (handler->priority > best_priority) {
                        best_handler = handler;
                        best_priority = handler->priority;
                    }
                }
            } else if (handler->priority > best_priority) {
                best_handler = handler;
                best_priority = handler->priority;
            }
        }
    }

    g_free(ext);
    return best_handler;
}

/**
 * Get all registered format handlers
 */
GList* format_registry_get_all_handlers(void) {
    return format_handlers;
}

/**
 * Get file filter patterns for GTK file chooser
 */
gchar* format_registry_get_file_filter_patterns(void) {
    GString* patterns = g_string_new(NULL);
    GHashTable* seen = g_hash_table_new(g_str_hash, g_str_equal);

    for (GList* iter = format_handlers; iter; iter = iter->next) {
        FormatHandler* handler = (FormatHandler*)iter->data;

        if (handler->format_info.extensions) {
            gchar** exts = g_strsplit(handler->format_info.extensions, ",", -1);
            for (gint i = 0; exts[i]; i++) {
                g_strstrip(exts[i]);
                if (!g_hash_table_contains(seen, exts[i])) {
                    g_hash_table_insert(seen, g_strdup(exts[i]), GINT_TO_POINTER(1));
                    if (patterns->len > 0) {
                        g_string_append(patterns, ";");
                    }
                    g_string_append_printf(patterns, "*.%s", exts[i]);
                }
            }
            g_strfreev(exts);
        }
    }

    g_hash_table_destroy(seen);
    return g_string_free(patterns, FALSE);
}

/**
 * Get format names for UI
 */
gchar** format_registry_get_format_names(void) {
    GPtrArray* names = g_ptr_array_new();

    for (GList* iter = format_handlers; iter; iter = iter->next) {
        FormatHandler* handler = (FormatHandler*)iter->data;
        if (handler->format_info.name) {
            g_ptr_array_add(names, g_strdup(handler->format_info.name));
        }
    }

    g_ptr_array_add(names, NULL);
    return (gchar**)g_ptr_array_free(names, FALSE);
}
