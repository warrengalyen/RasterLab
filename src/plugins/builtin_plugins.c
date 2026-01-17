#include "image_format_plugin.h"
#include "plugins/format_registry.h"
#include "plugins/plugin_bmp.h"
#include "plugins/plugin_cut.h"
#include "plugins/plugin_host_api.h"
#include "plugins/plugin_jpeg.h"
#include "plugins/plugin_loader.h"
#include "plugins/plugin_netpbm.h"
#include "plugins/plugin_pcx.h"
#include "plugins/plugin_png.h"
#include "plugins/plugin_ras.h"
#include "plugins/plugin_tga.h"
#include "plugins/plugin_xpm.h"
#include <glib.h>

/**
 * Register built-in plugins (PNG, JPEG, BMP)
 * These are compiled directly into the application
 */
void builtin_plugins_register(void) {
    ImageFormatHostAPI* host_api = plugin_host_api_get();
    ImageFormatPlugin png_plugin;
    ImageFormatPlugin jpeg_plugin;
    ImageFormatPlugin bmp_plugin;
    ImageFormatPlugin netpbm_plugin;
    ImageFormatPlugin pcx_plugin;
    ImageFormatPlugin tga_plugin;
    ImageFormatPlugin xpm_plugin;
    ImageFormatPlugin cut_plugin;
    ImageFormatPlugin ras_plugin;

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

    /* Register BMP plugin */
    g_message("Registering built-in BMP plugin");
    if (plugin_init_bmp(host_api, &bmp_plugin)) {
        if (format_registry_register_builtin(&bmp_plugin)) {
            g_message("Successfully registered BMP plugin");
        } else {
            g_message("Failed to register BMP plugin with format registry");
        }
    } else {
        g_message("Failed to initialize BMP plugin");
    }

    /* Register Netpbm plugin */
    g_message("Registering built-in Netpbm plugin");
    if (plugin_init_netpbm(host_api, &netpbm_plugin)) {
        if (format_registry_register_builtin(&netpbm_plugin)) {
            g_message("Successfully registered Netpbm plugin");
        } else {
            g_message("Failed to register Netpbm plugin with format registry");
        }
    } else {
        g_message("Failed to initialize Netpbm plugin");
    }

    /* Register PCX plugin */
    g_message("Registering built-in PCX plugin");
    if (plugin_init_pcx(host_api, &pcx_plugin)) {
        if (format_registry_register_builtin(&pcx_plugin)) {
            g_message("Successfully registered PCX plugin");
        } else {
            g_message("Failed to register PCX plugin with format registry");
        }
    } else {
        g_message("Failed to initialize PCX plugin");
    }

    /* Register TGA plugin */
    g_message("Registering built-in TGA plugin");
    if (plugin_init_tga(host_api, &tga_plugin)) {
        if (format_registry_register_builtin(&tga_plugin)) {
            g_message("Successfully registered TGA plugin");
        } else {
            g_message("Failed to register TGA plugin with format registry");
        }
    } else {
        g_message("Failed to initialize TGA plugin");
    }

    /* Register XPM plugin */
    g_message("Registering built-in XPM plugin");
    if (plugin_init_xpm(host_api, &xpm_plugin)) {
        if (format_registry_register_builtin(&xpm_plugin)) {
            g_message("Successfully registered XPM plugin");
        } else {
            g_message("Failed to register XPM plugin with format registry");
        }
    } else {
        g_message("Failed to initialize XPM plugin");
    }

    /* Register CUT plugin */
    g_message("Registering built-in CUT plugin");
    if (plugin_init_cut(host_api, &cut_plugin)) {
        if (format_registry_register_builtin(&cut_plugin)) {
            g_message("Successfully registered CUT plugin");
        } else {
            g_message("Failed to register CUT plugin with format registry");
        }
    } else {
        g_message("Failed to initialize CUT plugin");
    }

    /* Register RAS plugin */
    g_message("Registering built-in RAS plugin");
    if (plugin_init_ras(host_api, &ras_plugin)) {
        if (format_registry_register_builtin(&ras_plugin)) {
            g_message("Successfully registered RAS plugin");
        } else {
            g_message("Failed to register RAS plugin with format registry");
        }
    } else {
        g_message("Failed to initialize RAS plugin");
    }
}
