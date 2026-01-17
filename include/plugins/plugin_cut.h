#ifndef PLUGIN_CUT_H
#define PLUGIN_CUT_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize CUT plugin (Dr. Halo format)
 */
bool plugin_init_cut(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_CUT_H */
