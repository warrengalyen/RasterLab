/**
 * Session-based debug logging for RasterLab (one log per process session).
 * Writes rotating log files under debug/ next to the executable (same base as settings/plugins).
 */

#ifndef DEBUG_LOGGER_H
#define DEBUG_LOGGER_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open the rotating log file, write the session header.
 * @param app_dir Executable directory (UTF-8), same as settings; if NULL or empty, resolved via
 *                settings_get_executable_dir(). Logs always go under exe_dir/debug (created if missing).
 * @return false if the log file could not be opened (logging disabled for this run).
 */
bool debug_init(const char* app_dir);

/**
 * Write the session footer and close the log file. Safe to call if init failed or already shut down.
 */
void debug_shutdown(void);

/**
 * Append one line: -TYPE- | time | message (printf-style). No-op if logging is not active.
 * @param type 3-letter code (e.g. "DBG", "ERR"); shorter strings are padded, longer are truncated.
 */
void debug_log(const char* type, const char* fmt, ...);

/**
 * Flush the session log file so external viewers see recent lines.
 * No-op if logging is not active.
 */
void debug_flush(void);

/**
 * Copy the UTF-8 path of the current session log (DebugReport_n.log, n in 1..10) into @a buf.
 * @return false if logging is not active or the path does not fit.
 */
bool debug_get_current_log_path(char* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_LOGGER_H */
