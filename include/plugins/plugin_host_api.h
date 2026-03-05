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

/**
 * Set the settings pointer for color management (use embedded ICC, etc.).
 * Call after loading settings (e.g. in main). Pass NULL if settings unavailable.
 */
void plugin_host_api_set_cm_settings(void* settings);

/**
 * Get the CMS rendering intent from settings (0-3). Default: 1 (relative colorimetric).
 */
int plugin_host_api_get_cm_rendering_intent(void);

/**
 * Get whether black point compensation is enabled. Default: true.
 */
bool plugin_host_api_get_cm_bpc(void);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_HOST_API_H */
