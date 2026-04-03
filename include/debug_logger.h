/**
 * Session-based debug logging for RasterLab (one log per process session).
 * Writes rotating log files under ./debug relative to the process current working directory.
 */

#ifndef DEBUG_LOGGER_H
#define DEBUG_LOGGER_H

#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open the rotating log file, write the session header.
 * @param app_dir Executable directory (UTF-8) for bundled shared-library checks; may be NULL.
 * @return false if the log file could not be opened (logging disabled for this run).
 */
bool debug_init(const char *app_dir);

/**
 * Write the session footer and close the log file. Safe to call if init failed or already shut down.
 */
void debug_shutdown(void);

/**
 * Append one line: -TYPE- | time | message (printf-style). No-op if logging is not active.
 * @param type 3-letter code (e.g. "DBG", "ERR"); shorter strings are padded, longer are truncated.
 */
void debug_log(const char *type, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_LOGGER_H */
