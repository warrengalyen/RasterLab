#ifndef PLUGIN_XPM_H
#define PLUGIN_XPM_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize XPM plugin
 */
bool plugin_init_xpm(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_XPM_H */
