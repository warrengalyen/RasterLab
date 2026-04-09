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
#include "image_format_plugin.h"
#include "plugins/format_registry.h"
#include "plugins/plugin_runtime_deps.h"
#include "plugins/plugin_bmp.h"
#include "plugins/plugin_cut.h"
#include "plugins/plugin_deep.h"
#include "plugins/plugin_dicom.h"
#include "plugins/plugin_fits.h"
#include "plugins/plugin_hdr.h"
#include "plugins/plugin_heic.h"
#include "plugins/plugin_avif.h"
#include "plugins/plugin_host_api.h"
#include "plugins/plugin_jpeg.h"
#include "plugins/plugin_loader.h"
#include "plugins/plugin_netpbm.h"
#include "plugins/plugin_pcd.h"
#include "plugins/plugin_pcx.h"
#include "plugins/plugin_png.h"
#include "plugins/plugin_ras.h"
#include "plugins/plugin_rli.h"
#include "plugins/plugin_sgi.h"
#include "plugins/plugin_tga.h"
#include "plugins/plugin_tiff.h"
#include "plugins/plugin_webp.h"
#include "plugins/plugin_xbm.h"
#include "plugins/plugin_xpm.h"
#ifdef HAVE_OPENEXR
#include "plugins/plugin_exr.h"
#endif
#include <glib.h>
#include "debug_logger.h"

/**
 * Register built-in plugins (PNG, JPEG, BMP)
 * These are compiled directly into the application
 */
void builtin_plugins_register(void) {
    gchar* app_dir = settings_get_executable_dir();
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
    ImageFormatPlugin pcd_plugin;
    ImageFormatPlugin heic_plugin;
    ImageFormatPlugin avif_plugin;
    ImageFormatPlugin exr_plugin;
    ImageFormatPlugin rli_plugin;

    /* Register RLI plugin (native Rasterlab Image format); LZ4 is linked statically */
    debug_log("DBG", "Registering built-in RLI plugin");
    if (plugin_init_rli(host_api, &rli_plugin)) {
        if (format_registry_register_builtin(&rli_plugin)) {
            debug_log("DBG", "Successfully registered RLI plugin");
        } else {
            debug_log("ERR", "Failed to register RLI plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize RLI plugin");
    }

    /* Register PNG plugin (using libpng) */
#ifdef HAVE_LIBPNG
    debug_log("DBG", "Registering built-in PNG plugin");
    if (!plugin_runtime_deps_png_ok(app_dir)) {
        debug_log("WRN", "Skipping PNG plugin: zlib and/or libpng shared libraries not found in application directory");
    } else if (plugin_init_png(host_api, &png_plugin)) {
        if (format_registry_register_builtin(&png_plugin)) {
            debug_log("DBG", "Successfully registered PNG plugin");
        } else {
            debug_log("ERR", "Failed to register PNG plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize PNG plugin (libpng may not be available)");
    }
#else
    debug_log("DBG", "PNG plugin not available (HAVE_LIBPNG not defined)");
#endif

    /* Register JPEG plugin (using libjpeg) */
#ifdef HAVE_LIBJPEG
    debug_log("DBG", "Registering built-in JPEG plugin");
    if (!plugin_runtime_deps_jpeg_ok(app_dir)) {
        debug_log("WRN", "Skipping JPEG plugin: libjpeg shared library not found in application directory");
    } else if (plugin_init_jpeg(host_api, &jpeg_plugin)) {
        if (format_registry_register_builtin(&jpeg_plugin)) {
            debug_log("DBG", "Successfully registered JPEG plugin");
        } else {
            debug_log("ERR", "Failed to register JPEG plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize JPEG plugin (libjpeg may not be available)");
    }
#else
    debug_log("DBG", "JPEG plugin not available (HAVE_LIBJPEG not defined)");
#endif

    /* Register BMP plugin */
    debug_log("DBG", "Registering built-in BMP plugin");
    if (plugin_init_bmp(host_api, &bmp_plugin)) {
        if (format_registry_register_builtin(&bmp_plugin)) {
            debug_log("DBG", "Successfully registered BMP plugin");
        } else {
            debug_log("ERR", "Failed to register BMP plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize BMP plugin");
    }

    /* Register Netpbm plugin */
    debug_log("DBG", "Registering built-in Netpbm plugin");
    if (plugin_init_netpbm(host_api, &netpbm_plugin)) {
        if (format_registry_register_builtin(&netpbm_plugin)) {
            debug_log("DBG", "Successfully registered Netpbm plugin");
        } else {
            debug_log("ERR", "Failed to register Netpbm plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize Netpbm plugin");
    }

    /* Register PCX plugin */
    debug_log("DBG", "Registering built-in PCX plugin");
    if (plugin_init_pcx(host_api, &pcx_plugin)) {
        if (format_registry_register_builtin(&pcx_plugin)) {
            debug_log("DBG", "Successfully registered PCX plugin");
        } else {
            debug_log("ERR", "Failed to register PCX plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize PCX plugin");
    }

    /* Register TGA plugin */
    debug_log("DBG", "Registering built-in TGA plugin");
    if (plugin_init_tga(host_api, &tga_plugin)) {
        if (format_registry_register_builtin(&tga_plugin)) {
            debug_log("DBG", "Successfully registered TGA plugin");
        } else {
            debug_log("ERR", "Failed to register TGA plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize TGA plugin");
    }

    /* Register XBM plugin */
    debug_log("DBG", "Registering built-in XBM plugin");
    if (plugin_init_xbm(host_api, &xbm_plugin)) {
        if (format_registry_register_builtin(&xbm_plugin)) {
            debug_log("DBG", "Successfully registered XBM plugin");
        } else {
            debug_log("ERR", "Failed to register XBM plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize XBM plugin");
    }

    /* Register XPM plugin */
    debug_log("DBG", "Registering built-in XPM plugin");
    if (plugin_init_xpm(host_api, &xpm_plugin)) {
        if (format_registry_register_builtin(&xpm_plugin)) {
            debug_log("DBG", "Successfully registered XPM plugin");
        } else {
            debug_log("ERR", "Failed to register XPM plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize XPM plugin");
    }

    /* Register CUT plugin */
    debug_log("DBG", "Registering built-in CUT plugin");
    if (plugin_init_cut(host_api, &cut_plugin)) {
        if (format_registry_register_builtin(&cut_plugin)) {
            debug_log("DBG", "Successfully registered CUT plugin");
        } else {
            debug_log("ERR", "Failed to register CUT plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize CUT plugin");
    }

    /* Register RAS plugin */
    debug_log("DBG", "Registering built-in RAS plugin");
    if (plugin_init_ras(host_api, &ras_plugin)) {
        if (format_registry_register_builtin(&ras_plugin)) {
            debug_log("DBG", "Successfully registered RAS plugin");
        } else {
            debug_log("ERR", "Failed to register RAS plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize RAS plugin");
    }

    /* Register SGI plugin */
    debug_log("DBG", "Registering built-in SGI plugin");
    if (plugin_init_sgi(host_api, &sgi_plugin)) {
        if (format_registry_register_builtin(&sgi_plugin)) {
            debug_log("DBG", "Successfully registered SGI plugin");
        } else {
            debug_log("ERR", "Failed to register SGI plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize SGI plugin");
    }

    /* Register DEEP plugin */
    debug_log("DBG", "Registering built-in DEEP plugin");
    if (plugin_init_deep(host_api, &deep_plugin)) {
        if (format_registry_register_builtin(&deep_plugin)) {
            debug_log("DBG", "Successfully registered DEEP plugin");
        } else {
            debug_log("ERR", "Failed to register DEEP plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize DEEP plugin");
    }

    /* Register WebP plugin (using libwebp) */
#ifdef HAVE_LIBWEBP
    debug_log("DBG", "Registering built-in WebP plugin");
    if (!plugin_runtime_deps_webp_ok(app_dir)) {
        debug_log("WRN", "Skipping WebP plugin: libwebp shared libraries not found in application directory");
    } else if (plugin_init_webp(host_api, &webp_plugin)) {
        if (format_registry_register_builtin(&webp_plugin)) {
            debug_log("DBG", "Successfully registered WebP plugin");
        } else {
            debug_log("ERR", "Failed to register WebP plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize WebP plugin (libwebp may not be available)");
    }
#else
    debug_log("DBG", "WebP plugin not available (HAVE_LIBWEBP not defined)");
#endif

    /* Register TIFF plugin (using libtiff) */
#ifdef HAVE_LIBTIFF
    debug_log("DBG", "Registering built-in TIFF plugin");
    if (!plugin_runtime_deps_tiff_ok(app_dir)) {
        debug_log("WRN", "Skipping TIFF plugin: libtiff and/or dependency DLLs not found in application directory");
    } else if (plugin_init_tiff(host_api, &tiff_plugin)) {
        if (format_registry_register_builtin(&tiff_plugin)) {
            debug_log("DBG", "Successfully registered TIFF plugin");
        } else {
            debug_log("ERR", "Failed to register TIFF plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize TIFF plugin (libtiff may not be available)");
    }
#else
    debug_log("DBG", "TIFF plugin not available (HAVE_LIBTIFF not defined)");
#endif

    /* Register HDR plugin */
    debug_log("DBG", "Registering built-in HDR plugin");
    if (plugin_init_hdr(host_api, &hdr_plugin)) {
        if (format_registry_register_builtin(&hdr_plugin)) {
            debug_log("DBG", "Successfully registered HDR plugin");
        } else {
            debug_log("ERR", "Failed to register HDR plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize HDR plugin");
    }

    /* Register FITS plugin */
    debug_log("DBG", "Registering built-in FITS plugin");
    if (plugin_init_fits(host_api, &fits_plugin)) {
        if (format_registry_register_builtin(&fits_plugin)) {
            debug_log("DBG", "Successfully registered FITS plugin");
        } else {
            debug_log("ERR", "Failed to register FITS plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize FITS plugin");
    }

    /* Register DICOM plugin */
    debug_log("DBG", "Registering built-in DICOM plugin");
    if (plugin_init_dicom(host_api, &dicom_plugin)) {
        if (format_registry_register_builtin(&dicom_plugin)) {
            debug_log("DBG", "Successfully registered DICOM plugin");
        } else {
            debug_log("ERR", "Failed to register DICOM plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize DICOM plugin");
    }

    /* Register PCD plugin */
    debug_log("DBG", "Registering built-in PCD plugin");
    if (plugin_init_pcd(host_api, &pcd_plugin)) {
        if (format_registry_register_builtin(&pcd_plugin)) {
            debug_log("DBG", "Successfully registered PCD plugin");
        } else {
            debug_log("ERR", "Failed to register PCD plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize PCD plugin");
    }

    /* Register HEIC plugin (using libheif + libde265) */
#ifdef HAVE_LIBHEIF
    debug_log("DBG", "Registering built-in HEIC plugin");
    if (!plugin_runtime_deps_heic_ok(app_dir)) {
        debug_log("WRN", "Skipping HEIC plugin: libheif/libde265 shared libraries not found in application directory");
    } else if (plugin_init_heic(host_api, &heic_plugin)) {
        if (format_registry_register_builtin(&heic_plugin)) {
            debug_log("DBG", "Successfully registered HEIC plugin");
        } else {
            debug_log("ERR", "Failed to register HEIC plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize HEIC plugin (libheif may not be available)");
    }
#else
    debug_log("DBG", "HEIC plugin not available (HAVE_LIBHEIF not defined)");
#endif

    /* Register AVIF plugin (using libheif + libaom) */
#if HAVE_LIBHEIF && HAVE_LIBAOM
    debug_log("DBG", "Registering built-in AVIF plugin");
    if (plugin_init_avif(host_api, &avif_plugin)) {
        if (format_registry_register_builtin(&avif_plugin)) {
            debug_log("DBG", "Successfully registered AVIF plugin");
        } else {
            debug_log("ERR", "Failed to register AVIF plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize AVIF plugin (libaom may not be available)");
    }
#else
    debug_log("DBG", "AVIF plugin not available (requires HAVE_LIBHEIF and HAVE_LIBAOM)");
#endif

    /* Register EXR plugin (OpenEXR) */
#ifdef HAVE_OPENEXR
    debug_log("DBG", "Registering built-in EXR plugin");
    if (!plugin_runtime_deps_exr_ok(app_dir)) {
        debug_log("WRN", "Skipping EXR plugin: zlib and/or OpenEXR stack (OpenEXR, OpenEXRCore, Imath, IlmThread) not found in application directory");
    } else if (plugin_init_exr(host_api, &exr_plugin)) {
        if (format_registry_register_builtin(&exr_plugin)) {
            debug_log("DBG", "Successfully registered EXR plugin");
        } else {
            debug_log("ERR", "Failed to register EXR plugin with format registry");
        }
    } else {
        debug_log("ERR", "Failed to initialize EXR plugin");
    }
#else
    debug_log("DBG", "EXR plugin not available (HAVE_OPENEXR not defined)");
#endif

    g_free(app_dir);
}
