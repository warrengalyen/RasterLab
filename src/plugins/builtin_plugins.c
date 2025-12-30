#include "image_format_plugin.h"
#include "plugins/format_registry.h"
#include "plugins/plugin_host_api.h"
#include "plugins/plugin_jpeg.h"
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

    /* Register PNG plugin (using libpng) */
#ifdef HAVE_LIBPNG
    g_message("Registering built-in PNG plugin");
    if (plugin_init_png(host_api, &png_plugin)) {
        if (format_registry_register_builtin(&png_plugin)) {
            g_message("Successfully registered PNG plugin");
        } else {
            g_message("Failed to register PNG plugin with format registry");
        }
    } else {
        g_message("Failed to initialize PNG plugin (libpng may not be available)");
    }
#else
    g_message("PNG plugin not available (HAVE_LIBPNG not defined)");
#endif

    /* Register JPEG plugin (using libjpeg) */
#ifdef HAVE_LIBJPEG
    g_message("Registering built-in JPEG plugin");
    if (plugin_init_jpeg(host_api, &jpeg_plugin)) {
        if (format_registry_register_builtin(&jpeg_plugin)) {
            g_message("Successfully registered JPEG plugin");
        } else {
            g_message("Failed to register JPEG plugin with format registry");
        }
    } else {
        g_message("Failed to initialize JPEG plugin (libjpeg may not be available)");
    }
#else
    g_message("JPEG plugin not available (HAVE_LIBJPEG not defined)");
#endif
}
