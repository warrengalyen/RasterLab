#ifndef BUILTIN_PLUGINS_H
#define BUILTIN_PLUGINS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register built-in plugins (PNG, JPEG, etc.)
 * Should be called during application startup
 */
void builtin_plugins_register(void);

#ifdef __cplusplus
}
#endif

#endif /* BUILTIN_PLUGINS_H */
