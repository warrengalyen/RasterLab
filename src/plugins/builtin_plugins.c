#include "image_format_plugin.h"
#include "plugins/format_registry.h"
#include "plugins/plugin_host_api.h"
#include "plugins/plugin_loader.h"
#include "plugins/plugin_png.h"
#include <glib.h>

/**
 * Register built-in plugins (PNG, JPEG)
 * These are compiled directly into the application
 */
void builtin_plugins_register(void) {
    ImageFormatHostAPI* host_api = plugin_host_api_get();
    ImageFormatPlugin png_plugin;
    ImageFormatPlugin jpeg_plugin;

    /* Register PNG plugin */
    if (plugin_init_png(host_api, &png_plugin)) {
        format_registry_register_builtin(&png_plugin);
    }

    /* Register JPEG plugin */
    if (plugin_init_jpeg(host_api, &jpeg_plugin)) {
        format_registry_register_builtin(&jpeg_plugin);
    }
}
