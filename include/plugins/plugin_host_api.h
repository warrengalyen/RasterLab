#ifndef PLUGIN_HOST_API_H
#define PLUGIN_HOST_API_H

#include "image_format_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the host API structure with all callbacks initialized
 * This should be used when initializing plugins
 */
ImageFormatHostAPI* plugin_host_api_get(void);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_HOST_API_H */
