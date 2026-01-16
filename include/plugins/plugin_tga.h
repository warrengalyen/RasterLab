#ifndef PLUGIN_TGA_H
#define PLUGIN_TGA_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize TGA plugin
 */
bool plugin_init_tga(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_TGA_H */
