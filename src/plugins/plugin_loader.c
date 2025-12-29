#include "plugins/plugin_loader.h"
#include "image_format_plugin.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/**
 * Plugin handle structure
 */
struct PluginHandle {
#ifdef _WIN32
    HMODULE handle;
#else
    void* handle;
#endif
    ImageFormatPlugin plugin;
    gchar* plugin_path;
    gboolean initialized;
};

/**
 * Get plugin entry point function signature
 */
typedef bool (*PluginInitFunc)(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

/**
 * Plugin loader state
 */
static gboolean plugin_loader_initialized = FALSE;
static GList* loaded_plugins = NULL;

/**
 * Initialize plugin loader system
 */
void plugin_loader_init(void) {
    if (plugin_loader_initialized) {
        return;
    }

    loaded_plugins = NULL;
    plugin_loader_initialized = TRUE;
}

/**
 * Shutdown plugin loader system
 */
void plugin_loader_shutdown(void) {
    if (!plugin_loader_initialized) {
        return;
    }

    /* Unload all plugins */
    for (GList* iter = loaded_plugins; iter; iter = iter->next) {
        PluginHandle* handle = (PluginHandle*)iter->data;
        plugin_loader_unload(handle);
    }

    g_list_free(loaded_plugins);
    loaded_plugins = NULL;
    plugin_loader_initialized = FALSE;
}

/**
 * Load a plugin from a shared library file
 */
PluginHandle* plugin_loader_load(const char* plugin_path) {
    PluginHandle* handle;
    PluginInitFunc init_func;

    if (!plugin_loader_initialized) {
        g_warning("Plugin loader not initialized");
        return NULL;
    }

    if (!plugin_path) {
        g_warning("Invalid plugin path");
        return NULL;
    }

    /* Allocate handle */
    handle = g_malloc0(sizeof(PluginHandle));
    handle->plugin_path = g_strdup(plugin_path);
    handle->initialized = FALSE;

    /* Load shared library */
#ifdef _WIN32
    handle->handle = LoadLibraryA(plugin_path);
    if (!handle->handle) {
        DWORD error = GetLastError();
        g_warning("Failed to load plugin %s: error %lu", plugin_path, (unsigned long)error);
        g_free(handle->plugin_path);
        g_free(handle);
        return NULL;
    }

    /* Get plugin_init function */
    init_func = (PluginInitFunc)GetProcAddress(handle->handle, "plugin_init");
#else
    handle->handle = dlopen(plugin_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle->handle) {
        const char* error = dlerror();
        g_warning("Failed to load plugin %s: %s", plugin_path, error ? error : "Unknown error");
        g_free(handle->plugin_path);
        g_free(handle);
        return NULL;
    }

    /* Get plugin_init function */
    dlerror(); /* Clear any existing error */
    init_func = (PluginInitFunc)dlsym(handle->handle, "plugin_init");
    const char* dlsym_error = dlerror();
    if (dlsym_error) {
        g_warning("Failed to find plugin_init in %s: %s", plugin_path, dlsym_error);
        dlclose(handle->handle);
        g_free(handle->plugin_path);
        g_free(handle);
        return NULL;
    }
#endif

    if (!init_func) {
        g_warning("plugin_init symbol not found in %s", plugin_path);
#ifdef _WIN32
        FreeLibrary(handle->handle);
#else
        dlclose(handle->handle);
#endif
        g_free(handle->plugin_path);
        g_free(handle);
        return NULL;
    }

    /* Call plugin_init - but we need the host API first, so we'll do this later */
    /* For now, just store the handle */

    /* Add to loaded plugins list */
    loaded_plugins = g_list_append(loaded_plugins, handle);

    return handle;
}

/**
 * Initialize a plugin with host API
 * This should be called after loading the plugin
 */
static gboolean plugin_handle_init(PluginHandle* handle, const ImageFormatHostAPI* host_api) {
    PluginInitFunc init_func;

    if (!handle || handle->initialized) {
        return FALSE;
    }

    /* Get plugin_init function */
#ifdef _WIN32
    init_func = (PluginInitFunc)GetProcAddress(handle->handle, "plugin_init");
#else
    dlerror(); /* Clear any existing error */
    init_func = (PluginInitFunc)dlsym(handle->handle, "plugin_init");
    const char* dlsym_error = dlerror();
    if (dlsym_error) {
        g_warning("Failed to find plugin_init: %s", dlsym_error);
        return FALSE;
    }
#endif

    if (!init_func) {
        g_warning("plugin_init symbol not found");
        return FALSE;
    }

    /* Initialize plugin structure */
    memset(&handle->plugin, 0, sizeof(ImageFormatPlugin));

    /* Call plugin_init */
    if (!init_func(host_api, &handle->plugin)) {
        g_warning("Plugin initialization failed for %s", handle->plugin_path);
        return FALSE;
    }

    /* Validate plugin structure */
    if (!handle->plugin.callbacks.can_load ||
        !handle->plugin.callbacks.load ||
        !handle->plugin.callbacks.can_save ||
        !handle->plugin.callbacks.save) {
        g_warning("Plugin %s missing required callbacks", handle->plugin_path);
        return FALSE;
    }

    handle->initialized = TRUE;
    return TRUE;
}

/**
 * Unload a plugin
 */
void plugin_loader_unload(PluginHandle* handle) {
    if (!handle) {
        return;
    }

    /* Call plugin cleanup if available */
    if (handle->initialized && handle->plugin.callbacks.cleanup) {
        handle->plugin.callbacks.cleanup();
    }

    /* Close shared library */
#ifdef _WIN32
    if (handle->handle) {
        FreeLibrary(handle->handle);
    }
#else
    if (handle->handle) {
        dlclose(handle->handle);
    }
#endif

    /* Remove from loaded plugins list */
    loaded_plugins = g_list_remove(loaded_plugins, handle);

    /* Free handle */
    g_free(handle->plugin_path);
    g_free(handle);
}

/**
 * Get the plugin structure from a loaded plugin
 */
ImageFormatPlugin* plugin_loader_get_plugin(PluginHandle* handle) {
    if (!handle || !handle->initialized) {
        return NULL;
    }

    return &handle->plugin;
}

/**
 * Check if plugin file has correct extension
 */
static gboolean is_plugin_file(const char* filename) {
    if (!filename) {
        return FALSE;
    }

#ifdef _WIN32
    /* Windows: .dll */
    if (g_str_has_suffix(filename, ".dll")) {
        return TRUE;
    }
#elif defined(__APPLE__)
    /* macOS: .dylib */
    if (g_str_has_suffix(filename, ".dylib")) {
        return TRUE;
    }
#else
    /* Linux: .so */
    if (g_str_has_suffix(filename, ".so")) {
        return TRUE;
    }
#endif

    return FALSE;
}

/**
 * Scan directory for plugins and load them
 */
GList* plugin_loader_scan_directory(const char* directory_path) {
    GList* plugin_handles = NULL;
    GDir* dir;
    const gchar* filename;
    GError* error = NULL;

    if (!directory_path) {
        return NULL;
    }

    /* Open directory */
    dir = g_dir_open(directory_path, 0, &error);
    if (!dir) {
        if (error) {
            g_warning("Failed to open plugin directory %s: %s", directory_path, error->message);
            g_error_free(error);
        }
        return NULL;
    }

    /* Scan for plugin files */
    while ((filename = g_dir_read_name(dir))) {
        if (is_plugin_file(filename)) {
            gchar* full_path = g_build_filename(directory_path, filename, NULL);
            PluginHandle* handle = plugin_loader_load(full_path);

            if (handle) {
                plugin_handles = g_list_append(plugin_handles, handle);
            }

            g_free(full_path);
        }
    }

    g_dir_close(dir);

    return plugin_handles;
}

/**
 * Free a list of plugin handles
 */
void plugin_loader_free_list(GList* plugin_list) {
    for (GList* iter = plugin_list; iter; iter = iter->next) {
        PluginHandle* handle = (PluginHandle*)iter->data;
        plugin_loader_unload(handle);
    }

    g_list_free(plugin_list);
}

/**
 * Initialize plugin with host API (internal function for format registry)
 * This is exported via a header that format_registry can access
 */
gboolean plugin_loader_init_with_host(PluginHandle* handle, const ImageFormatHostAPI* host_api) {
    return plugin_handle_init(handle, host_api);
}
