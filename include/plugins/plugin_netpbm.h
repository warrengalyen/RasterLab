#ifndef PLUGIN_NETPBM_H
#define PLUGIN_NETPBM_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize Netpbm plugin
 */
bool plugin_init_netpbm(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_NETPBM_H */
