/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

/**
 * debug_logger.c — session-based rotating debug logs (C99).
 */

#include "debug_logger.h"

#include "app/settings.h"

#include "plugins/plugin_runtime_deps.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "build_version.h"
#include "version.h"

#ifdef _WIN32
#ifndef DEBUG_LOGGER_H
#define DEBUG_LOGGER_H
#endif
#include <direct.h>
#include <intrin.h>
#include <process.h>
#include <windows.h>

#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)
#include <cpuid.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#endif
#endif

#include <inttypes.h>

#include <glib.h>

/* Optional compile-time flags from CMake (defaults keep standalone compilation working). */
#ifndef HAVE_GETTEXT
#define HAVE_GETTEXT 0
#endif
#ifndef HAVE_GLFW
#define HAVE_GLFW 0
#endif

#ifndef DEBUG_LOGGER_NO_MUTEX
#define DEBUG_LOGGER_USE_MUTEX 1
#endif

#if DEBUG_LOGGER_USE_MUTEX && !defined(_WIN32)
#include <pthread.h>
static pthread_mutex_t g_debug_log_mutex = PTHREAD_MUTEX_INITIALIZER;
#define DEBUG_LOG_LOCK() pthread_mutex_lock(&g_debug_log_mutex)
#define DEBUG_LOG_UNLOCK() pthread_mutex_unlock(&g_debug_log_mutex)
#elif DEBUG_LOGGER_USE_MUTEX && defined(_WIN32)
static CRITICAL_SECTION g_debug_log_cs;
static LONG g_debug_log_cs_init = 0;
static void debug_log_lock(void) {
    if (InterlockedCompareExchange(&g_debug_log_cs_init, 1, 0) == 0) {
        InitializeCriticalSection(&g_debug_log_cs);
        InterlockedExchange(&g_debug_log_cs_init, 2);
    } else {
        while (InterlockedCompareExchange(&g_debug_log_cs_init, 2, 2) != 2) {
            Sleep(0);
        }
    }
    EnterCriticalSection(&g_debug_log_cs);
}
static void debug_log_unlock(void) {
    LeaveCriticalSection(&g_debug_log_cs);
}
#define DEBUG_LOG_LOCK() debug_log_lock()
#define DEBUG_LOG_UNLOCK() debug_log_unlock()
#else
#define DEBUG_LOG_LOCK() ((void)0)
#define DEBUG_LOG_UNLOCK() ((void)0)
#endif

#define DEBUG_DIR_NAME "debug"
#define DEBUG_REPORT_PREFIX "DebugReport_"
#define DEBUG_REPORT_SUFFIX ".log"

static char s_debug_dir[4096];

/** Single session logger (file + metadata). */
typedef struct {
    FILE* file;
    uint32_t log_index;
    uint64_t session_id;
} DebugLoggerState;

static DebugLoggerState s_logger;

static uint32_t debug_get_pid_u32(void) {
#ifdef _WIN32
    return (uint32_t)GetCurrentProcessId();
#else
    return (uint32_t)getpid();
#endif
}

static uint64_t debug_mix_session_id(void) {
    unsigned long seed = (unsigned long)time(NULL) ^ (unsigned long)debug_get_pid_u32();
    srand((unsigned)seed);
    uint64_t t = (uint64_t)time(NULL);
    uint64_t p = (uint64_t)debug_get_pid_u32();
    uint64_t r = (uint64_t)(unsigned)rand();
    r ^= (uint64_t)(unsigned)rand() << 31u;
    return (t << 24) ^ (p << 48) ^ r ^ (r << 17u);
}

static int mkdir_debug_folder(const char* path) {
#ifdef _WIN32
    if (_mkdir(path) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -1;
#else
    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -1;
#endif
}

/**
 * Return index 1–10 if @a name is DebugReport_<n>.log.
 */
static int parse_report_index(const char* name) {
    const size_t pre_len = strlen(DEBUG_REPORT_PREFIX);
    size_t nlen = strlen(name);
    if (nlen < pre_len + 1u + strlen(DEBUG_REPORT_SUFFIX)) {
        return -1;
    }
    if (strncmp(name, DEBUG_REPORT_PREFIX, pre_len) != 0) {
        return -1;
    }
    const char* num = name + pre_len;
    char* end = NULL;
    long v = strtol(num, &end, 10);
    if (!end || end == num || strcmp(end, DEBUG_REPORT_SUFFIX) != 0) {
        return -1;
    }
    if (v < 1 || v > 10) {
        return -1;
    }
    return (int)v;
}

/**
 * Scan ./debug for DebugReport_<1-10>.log.
 * Use the smallest index i in 1..10 with no file yet (reuse gaps after deletes).
 * If all ten slots exist, rotate: (max_index % 10) + 1 (same as before when full).
 */
static int debug_get_next_index(void) {
    int present[11]; /* present[1..10] */
    int max_index = 0;
    for (int i = 0; i < 11; i++) {
        present[i] = 0;
    }

#ifdef _WIN32
    char search[4096];
    if (snprintf(search, sizeof(search), "%s\\%s*", s_debug_dir, DEBUG_REPORT_PREFIX) >= (int)sizeof(search)) {
        return 1;
    }
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(search, &ffd);
    if (h == INVALID_HANDLE_VALUE) {
        return 1;
    }
    FILETIME newest_ft = {0, 0};
    int newest_idx = 0;
    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            int idx = parse_report_index(ffd.cFileName);
            if (idx >= 1 && idx <= 10) {
                present[idx] = 1;
                if (idx > max_index) {
                    max_index = idx;
                }
                if (CompareFileTime(&ffd.ftLastWriteTime, &newest_ft) > 0) {
                    newest_ft = ffd.ftLastWriteTime;
                    newest_idx = idx;
                }
            }
        }
    } while (FindNextFileA(h, &ffd));
    FindClose(h);
#else
    DIR* d = opendir(s_debug_dir);
    if (!d) {
        return 1;
    }
    time_t newest_mtime = 0;
    int newest_idx = 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        int idx = parse_report_index(ent->d_name);
        if (idx >= 1 && idx <= 10) {
            present[idx] = 1;
            if (idx > max_index) {
                max_index = idx;
            }
            char epath[4096];
            snprintf(epath, sizeof(epath), "%s/%s", s_debug_dir, ent->d_name);
            struct stat st;
            if (stat(epath, &st) == 0 && st.st_mtime > newest_mtime) {
                newest_mtime = st.st_mtime;
                newest_idx = idx;
            }
        }
    }
    closedir(d);
#endif

    for (int i = 1; i <= 10; i++) {
        if (!present[i]) {
            return i;
        }
    }
    /* All ten slots occupied — advance past the most-recently-written slot. */
    if (newest_idx <= 0) {
        return 1;
    }
    return (newest_idx % 10) + 1;
}

static void get_os_string(char* buf, size_t buf_sz) {
    if (!buf || buf_sz == 0) {
        return;
    }
    buf[0] = '\0';
#ifdef _WIN32
    OSVERSIONINFOEXW osi;
    memset(&osi, 0, sizeof(osi));
    osi.dwOSVersionInfoSize = sizeof(osi);
    if (GetVersionExW((OSVERSIONINFOW*)&osi)) {
        snprintf(buf, buf_sz, "Windows %lu.%lu (build %lu)",
                 (unsigned long)osi.dwMajorVersion,
                 (unsigned long)osi.dwMinorVersion,
                 (unsigned long)osi.dwBuildNumber);
    } else {
        snprintf(buf, buf_sz, "Windows");
    }
#else
    struct utsname u;
    if (uname(&u) == 0) {
        snprintf(buf, buf_sz, "%s %s", u.sysname, u.release);
    } else {
        snprintf(buf, buf_sz, "Unix-like");
    }
#endif
}

static int get_cpu_core_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n <= 0) {
        return 1;
    }
    return (int)n;
#endif
}

static void append_feature(char* buf, size_t buf_sz, const char* feat, int* first) {
    size_t len = strlen(buf);
    if (len + strlen(feat) + 3u >= buf_sz) {
        return;
    }
    if (!*first) {
        strncat(buf, ", ", buf_sz - len - 1u);
        len = strlen(buf);
    }
    strncat(buf, feat, buf_sz - len - 1u);
    *first = 0;
}

static void get_cpu_features_string(char* buf, size_t buf_sz) {
    if (!buf || buf_sz == 0) {
        return;
    }
    buf[0] = '\0';
    int first = 1;

#if defined(__APPLE__) && defined(__aarch64__)
    int64_t hw_neon = 0;
    size_t sz = sizeof(hw_neon);
    if (sysctlbyname("hw.optional.neon", &hw_neon, &sz, NULL, 0) == 0 && hw_neon) {
        append_feature(buf, buf_sz, "NEON", &first);
    }
    if (buf[0] == '\0') {
        snprintf(buf, buf_sz, "ARM64");
    }
    return;
#endif

#if defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)
#ifdef _WIN32
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 1);
    unsigned ecx = (unsigned)cpu_info[2];
    unsigned edx = (unsigned)cpu_info[3];
#else
    unsigned eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        snprintf(buf, buf_sz, "unknown");
        return;
    }
#endif
    if (edx & (1u << 23)) {
        append_feature(buf, buf_sz, "MMX", &first);
    }
    if (edx & (1u << 25)) {
        append_feature(buf, buf_sz, "SSE", &first);
    }
    if (edx & (1u << 26)) {
        append_feature(buf, buf_sz, "SSE2", &first);
    }
    if (ecx & (1u << 28)) {
        append_feature(buf, buf_sz, "AVX", &first);
    }
    if (buf[0] == '\0') {
        snprintf(buf, buf_sz, "x86 (generic)");
    }
#else
    snprintf(buf, buf_sz, "n/a");
#endif
}

static uint64_t get_total_ram_mb(void) {
#ifdef _WIN32
    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return (uint64_t)(ms.ullTotalPhys / (1024ull * 1024ull));
    }
    return 0;
#elif defined(__APPLE__)
    int64_t mem = 0;
    size_t sz = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &sz, NULL, 0) == 0 && mem > 0) {
        return (uint64_t)(mem / (1024 * 1024));
    }
    return 0;
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_sz = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_sz > 0) {
        return (uint64_t)((double)pages * (double)page_sz / (1024.0 * 1024.0));
    }
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) {
        return 0;
    }
    char line[256];
    uint64_t kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            if (sscanf(line, "MemTotal: %" SCNu64 " kB", &kb) == 1) {
                break;
            }
        }
    }
    fclose(f);
    return kb / 1024u;
#endif
}

static int get_memory_usage_percent(void) {
#ifdef _WIN32
    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return (int)ms.dwMemoryLoad;
    }
    return 0;
#elif defined(__APPLE__)
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
    vm_statistics64_data_t vmstat;
    mach_port_t host = mach_host_self();
    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vmstat, &count) != KERN_SUCCESS) {
        return 0;
    }
    uint64_t total = vmstat.free_count + vmstat.active_count + vmstat.inactive_count + vmstat.wire_count;
    if (total == 0) {
        return 0;
    }
    uint64_t used = vmstat.active_count + vmstat.wire_count;
    return (int)((used * 100ull) / total);
#else
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) {
        return 0;
    }
    uint64_t total_kb = 0, avail_kb = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %" SCNu64 " kB", &total_kb);
        } else if (strncmp(line, "MemAvailable:", 14) == 0) {
            sscanf(line, "MemAvailable: %" SCNu64 " kB", &avail_kb);
        }
    }
    fclose(f);
    if (total_kb == 0) {
        return 0;
    }
    if (avail_kb > total_kb) {
        avail_kb = total_kb;
    }
    uint64_t used_kb = total_kb - avail_kb;
    return (int)((used_kb * 100ull) / total_kb);
#endif
}

static void get_avail_memory_mb(double* real_mb, double* swap_mb) {
    if (real_mb) {
        *real_mb = 0.0;
    }
    if (swap_mb) {
        *swap_mb = 0.0;
    }
    if (!real_mb || !swap_mb) {
        return;
    }
#ifdef _WIN32
    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) {
        return;
    }
    *real_mb = (double)ms.ullAvailPhys / (1024.0 * 1024.0);
    /* Approximate paging-file headroom beyond physical free (clamped). */
    {
        double page_avail = (double)ms.ullAvailPageFile / (1024.0 * 1024.0);
        double pfree = *real_mb;
        double s = page_avail > pfree ? page_avail - pfree : 0.0;
        if (s < 0.0) {
            s = 0.0;
        }
        *swap_mb = s;
    }
#elif defined(__APPLE__)
    mach_port_t host = mach_host_self();
    vm_size_t page_size;
    vm_statistics64_data_t vmstat;
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
    host_page_size(host, &page_size);
    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vmstat, &count) != KERN_SUCCESS) {
        return;
    }
    uint64_t free_bytes = (uint64_t)vmstat.free_count * (uint64_t)page_size;
    *real_mb = (double)free_bytes / (1024.0 * 1024.0);
    *swap_mb = 0.0; /* Per-process swap detail not exposed here; keep 0. */
#else
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) {
        return;
    }
    uint64_t mem_avail_kb = 0, swap_free_kb = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line, "MemAvailable: %" SCNu64 " kB", &mem_avail_kb);
        } else if (strncmp(line, "SwapFree:", 9) == 0) {
            sscanf(line, "SwapFree: %" SCNu64 " kB", &swap_free_kb);
        }
    }
    fclose(f);
    *real_mb = (double)mem_avail_kb / 1024.0;
    *swap_mb = (double)swap_free_kb / 1024.0;
#endif
}

static const char* plugin_status(const char* app_dir, gboolean (*check)(const gchar*)) {
    if (!app_dir || !app_dir[0]) {
        return "MISSING";
    }
    return check((const gchar*)app_dir) ? "available" : "MISSING";
}

static void normalize_type_code(const char* type, char out[4]) {
    if (!type) {
        memcpy(out, "???", 3);
        out[3] = '\0';
        return;
    }
    for (int i = 0; i < 3; i++) {
        out[i] = type[i] ? type[i] : ' ';
    }
    out[3] = '\0';
}

static void write_header(DebugLoggerState* logger, const char* app_dir) {
    if (!logger || !logger->file) {
        return;
    }
    time_t now = time(NULL);
    struct tm* tm_local = localtime(&now);
    char date_buf[64];
    char time_buf[64];
    strftime(date_buf, sizeof(date_buf), "%m-%d-%Y", tm_local);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_local);

    char osbuf[256];
    char cpufeat[256];
    get_os_string(osbuf, sizeof(osbuf));
    get_cpu_features_string(cpufeat, sizeof(cpufeat));

    double real_mb = 0.0, swap_mb = 0.0;
    get_avail_memory_mb(&real_mb, &swap_mb);

    const char* trans = (HAVE_GETTEXT) ? "True" : "False";
    const char* lang = "en";
    const char* env_lang = getenv("LANG");
    if (env_lang && env_lang[0]) {
        lang = env_lang;
    }
    const char* gpu = (HAVE_GLFW) ? "True" : "False";

    fprintf(logger->file, "-- RASTERLAB DEBUG LOG #%u --\r\n", logger->log_index);
    fprintf(logger->file, "Date: %s\n", date_buf);
    fprintf(logger->file, "Time: %s\n", time_buf);
    fprintf(logger->file, "Session ID: %016" PRIx64 "\r\n", logger->session_id);
    fprintf(logger->file, "-- SYSTEM INFORMATION --\r\n");
    fprintf(logger->file, "OS: %s\n", osbuf);
    fprintf(logger->file, "Processor cores: %d\n", get_cpu_core_count());
    fprintf(logger->file, "Processor features: %s\n", cpufeat);
    fprintf(logger->file, "System RAM: %" PRIu64 " MB\n", get_total_ram_mb());
    fprintf(logger->file, "Max memory available to Rasterlab: %.1f MB (real), %.1f MB (swap)\n",
            real_mb, swap_mb);
    fprintf(logger->file, "Memory load at startup: %d%%\r\n", get_memory_usage_percent());
    fprintf(logger->file, "-- PROGRAM INFORMATION --\r\n");
#if RASTERLAB_BUILD_NUMBER != 0
    fprintf(logger->file, "Version: %s (build %d)\n", RASTERLAB_VERSION_LINE, RASTERLAB_BUILD_NUMBER);
#else
    fprintf(logger->file, "Version: %s\n", RASTERLAB_VERSION_LINE);
#endif
    fprintf(logger->file, "Translation active: %s\n", trans);
    fprintf(logger->file, "Language in use: %s\n", lang);
    fprintf(logger->file, "GPU acceleration enabled: %s\r\n", gpu);
    fprintf(logger->file, "-- SHARED LIBRARIES --\r\n");
    fprintf(logger->file, "zlib: %s\n", plugin_status(app_dir, plugin_runtime_deps_zlib_ok));
    fprintf(logger->file, "libjpeg: %s\n", plugin_status(app_dir, plugin_runtime_deps_jpeg_ok));
    fprintf(logger->file, "libpng: %s\n", plugin_status(app_dir, plugin_runtime_deps_png_ok));
    fprintf(logger->file, "libwebp: %s\n", plugin_status(app_dir, plugin_runtime_deps_webp_ok));
    fprintf(logger->file, "libtiff: %s\n", plugin_status(app_dir, plugin_runtime_deps_tiff_ok));
    fprintf(logger->file, "libde265: %s\n", plugin_status(app_dir, plugin_runtime_deps_libde265_ok));
    fprintf(logger->file, "libaom: %s\n", plugin_status(app_dir, plugin_runtime_deps_libaom_ok));
    fprintf(logger->file, "libheif: %s\n", plugin_status(app_dir, plugin_runtime_deps_libheif_ok));
    fprintf(logger->file, "OpenEXR: %s\n", plugin_status(app_dir, plugin_runtime_deps_exr_ok));
    fprintf(logger->file, "lcms2: %s\n", plugin_status(app_dir, plugin_runtime_deps_lcms2_ok));
    fprintf(logger->file, "\r\n-- SESSION REPORT --\r\n");
    fflush(logger->file);
}

bool debug_init(const char* app_dir) {
    memset(&s_logger, 0, sizeof(s_logger));

    /*
     * Always use <executable_dir>/debug (same notion as settings/plugins).
     * If the caller passes no path, resolve via settings_get_executable_dir().
     * mkdir_debug_folder creates the directory when missing.
     */
    gchar* resolved_exe = NULL;
    const char* base_dir = (app_dir && app_dir[0]) ? app_dir : NULL;
    if (!base_dir) {
        resolved_exe = settings_get_executable_dir();
        base_dir = resolved_exe ? (const char*)resolved_exe : NULL;
    }
    if (!base_dir || !base_dir[0]) {
        g_free(resolved_exe);
        return false;
    }

#ifdef _WIN32
    if (snprintf(s_debug_dir, sizeof(s_debug_dir), "%s\\%s", base_dir, DEBUG_DIR_NAME) >= (int)sizeof(s_debug_dir)) {
        g_free(resolved_exe);
        return false;
    }
#else
    if (snprintf(s_debug_dir, sizeof(s_debug_dir), "%s/%s", base_dir, DEBUG_DIR_NAME) >= (int)sizeof(s_debug_dir)) {
        g_free(resolved_exe);
        return false;
    }
#endif

    if (mkdir_debug_folder(s_debug_dir) != 0) {
        g_free(resolved_exe);
        return false;
    }

    int next = debug_get_next_index();
    char path[4600];
#ifdef _WIN32
    if (snprintf(path, sizeof(path), "%s\\%s%d%s", s_debug_dir, DEBUG_REPORT_PREFIX, next, DEBUG_REPORT_SUFFIX) >= (int)sizeof(path)) {
        g_free(resolved_exe);
        return false;
    }
#else
    if (snprintf(path, sizeof(path), "%s/%s%d%s", s_debug_dir, DEBUG_REPORT_PREFIX, next, DEBUG_REPORT_SUFFIX) >= (int)sizeof(path)) {
        g_free(resolved_exe);
        return false;
    }
#endif /* DEBUG_LOGGER_H */

    s_logger.file = fopen(path, "w");
    if (!s_logger.file) {
        memset(&s_logger, 0, sizeof(s_logger));
        g_free(resolved_exe);
        return false;
    }

    s_logger.log_index = (uint32_t)next;
    s_logger.session_id = debug_mix_session_id();
    write_header(&s_logger, base_dir);
    g_free(resolved_exe);
    return true;
}

void debug_shutdown(void) {
    DEBUG_LOG_LOCK();
    if (s_logger.file) {
        fprintf(s_logger.file, "\r\n-- END SESSION REPORT --\r\n");
        fflush(s_logger.file);
        fclose(s_logger.file);
        s_logger.file = NULL;
    }
    s_logger.log_index = 0;
    s_logger.session_id = 0;
    DEBUG_LOG_UNLOCK();
}

void debug_flush(void) {
    DEBUG_LOG_LOCK();
    if (s_logger.file) {
        fflush(s_logger.file);
    }
    DEBUG_LOG_UNLOCK();
}

bool debug_get_current_log_path(char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        return false;
    }
    DEBUG_LOG_LOCK();
    if (!s_logger.file || s_logger.log_index < 1u || s_logger.log_index > 10u) {
        DEBUG_LOG_UNLOCK();
        return false;
    }
    unsigned idx = (unsigned)s_logger.log_index;
#ifdef _WIN32
    int n = snprintf(buf, buf_size, "%s\\%s%u%s", s_debug_dir, DEBUG_REPORT_PREFIX, idx, DEBUG_REPORT_SUFFIX);
#else
    int n = snprintf(buf, buf_size, "%s/%s%u%s", s_debug_dir, DEBUG_REPORT_PREFIX, idx, DEBUG_REPORT_SUFFIX);
#endif
    DEBUG_LOG_UNLOCK();
    return n > 0 && (size_t)n < buf_size;
}

static void debug_vlog(DebugLoggerState* logger, const char* type, const char* fmt, va_list ap) {
    if (!logger || !logger->file || !fmt) {
        return;
    }

    char code[4];
    normalize_type_code(type, code);

    time_t now = time(NULL);
    struct tm* tm_local = localtime(&now);
    char tbuf[64];
    strftime(tbuf, sizeof(tbuf), "%I:%M:%S %p", tm_local);

    char msg[4096];
    vsnprintf(msg, sizeof(msg), fmt, ap);
    msg[sizeof(msg) - 1u] = '\0';

    DEBUG_LOG_LOCK();
    if (logger->file) {
        fprintf(logger->file, "-%s- | %s | %s\n", code, tbuf, msg);
        fflush(logger->file);
    }
    DEBUG_LOG_UNLOCK();
}

void debug_log(const char* type, const char* fmt, ...) {
    if (!fmt) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    debug_vlog(&s_logger, type, fmt, ap);
    va_end(ap);
}
