#ifndef PLUGIN_WEBP_H
#define PLUGIN_WEBP_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize WebP plugin using libwebp
 */
bool plugin_init_webp(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_WEBP_H */
