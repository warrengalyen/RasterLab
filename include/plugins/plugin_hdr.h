#ifndef PLUGIN_HDR_H
#define PLUGIN_HDR_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize HDR (Radiance RGBE) plugin
 */
bool plugin_init_hdr(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_HDR_H */
