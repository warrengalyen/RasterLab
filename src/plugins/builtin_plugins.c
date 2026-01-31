#include "image_format_plugin.h"
#include "plugins/format_registry.h"
#include "plugins/plugin_bmp.h"
#include "plugins/plugin_cut.h"
#include "plugins/plugin_deep.h"
#include "plugins/plugin_host_api.h"
#include "plugins/plugin_jpeg.h"
#include "plugins/plugin_loader.h"
#include "plugins/plugin_netpbm.h"
#include "plugins/plugin_pcx.h"
#include "plugins/plugin_png.h"
#include "plugins/plugin_ras.h"
#include "plugins/plugin_sgi.h"
#include "plugins/plugin_tga.h"
#include "plugins/plugin_tiff.h"
#include "plugins/plugin_webp.h"
#include "plugins/plugin_xbm.h"
#include "plugins/plugin_xpm.h"
#include "plugins/plugin_hdr.h"
#include "plugins/plugin_fits.h"
#include "plugins/plugin_dicom.h"
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
    ImageFormatPlugin xbm_plugin;
    ImageFormatPlugin xpm_plugin;
    ImageFormatPlugin cut_plugin;
    ImageFormatPlugin ras_plugin;
    ImageFormatPlugin sgi_plugin;
    ImageFormatPlugin deep_plugin;
    ImageFormatPlugin webp_plugin;
    ImageFormatPlugin tiff_plugin;
    ImageFormatPlugin hdr_plugin;
    ImageFormatPlugin fits_plugin;
    ImageFormatPlugin dicom_plugin;

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

    /* Register XBM plugin */
    g_message("Registering built-in XBM plugin");
    if (plugin_init_xbm(host_api, &xbm_plugin)) {
        if (format_registry_register_builtin(&xbm_plugin)) {
            g_message("Successfully registered XBM plugin");
        } else {
            g_message("Failed to register XBM plugin with format registry");
        }
    } else {
        g_message("Failed to initialize XBM plugin");
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

    /* Register SGI plugin */
    g_message("Registering built-in SGI plugin");
    if (plugin_init_sgi(host_api, &sgi_plugin)) {
        if (format_registry_register_builtin(&sgi_plugin)) {
            g_message("Successfully registered SGI plugin");
        } else {
            g_message("Failed to register SGI plugin with format registry");
        }
    } else {
        g_message("Failed to initialize SGI plugin");
    }

    /* Register DEEP plugin */
    g_message("Registering built-in DEEP plugin");
    if (plugin_init_deep(host_api, &deep_plugin)) {
        if (format_registry_register_builtin(&deep_plugin)) {
            g_message("Successfully registered DEEP plugin");
        } else {
            g_message("Failed to register DEEP plugin with format registry");
        }
    } else {
        g_message("Failed to initialize DEEP plugin");
    }

    /* Register WebP plugin (using libwebp) */
#ifdef HAVE_LIBWEBP
    g_message("Registering built-in WebP plugin");
    if (plugin_init_webp(host_api, &webp_plugin)) {
        if (format_registry_register_builtin(&webp_plugin)) {
            g_message("Successfully registered WebP plugin");
        } else {
            g_message("Failed to register WebP plugin with format registry");
        }
    } else {
        g_message("Failed to initialize WebP plugin (libwebp may not be available)");
    }
#else
    g_message("WebP plugin not available (HAVE_LIBWEBP not defined)");
#endif

    /* Register TIFF plugin (using libtiff) */
#ifdef HAVE_LIBTIFF
    g_message("Registering built-in TIFF plugin");
    if (plugin_init_tiff(host_api, &tiff_plugin)) {
        if (format_registry_register_builtin(&tiff_plugin)) {
            g_message("Successfully registered TIFF plugin");
        } else {
            g_message("Failed to register TIFF plugin with format registry");
        }
    } else {
        g_message("Failed to initialize TIFF plugin (libtiff may not be available)");
    }
#else
    g_message("TIFF plugin not available (HAVE_LIBTIFF not defined)");
#endif

    /* Register HDR plugin */
    g_message("Registering built-in HDR plugin");
    if (plugin_init_hdr(host_api, &hdr_plugin)) {
        if (format_registry_register_builtin(&hdr_plugin)) {
            g_message("Successfully registered HDR plugin");
        } else {
            g_message("Failed to register HDR plugin with format registry");
        }
    } else {
        g_message("Failed to initialize HDR plugin");
    }

    /* Register FITS plugin */
    g_message("Registering built-in FITS plugin");
    if (plugin_init_fits(host_api, &fits_plugin)) {
        if (format_registry_register_builtin(&fits_plugin)) {
            g_message("Successfully registered FITS plugin");
        } else {
            g_message("Failed to register FITS plugin with format registry");
        }
    } else {
        g_message("Failed to initialize FITS plugin");
    }

    /* Register DICOM plugin */
    g_message("Registering built-in DICOM plugin");
    if (plugin_init_dicom(host_api, &dicom_plugin)) {
        if (format_registry_register_builtin(&dicom_plugin)) {
            g_message("Successfully registered DICOM plugin");
        } else {
            g_message("Failed to register DICOM plugin with format registry");
        }
    } else {
        g_message("Failed to initialize DICOM plugin");
    }
}
