#ifndef PLUGIN_FITS_H
#define PLUGIN_FITS_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize FITS plugin
 */
bool plugin_init_fits(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_FITS_H */
