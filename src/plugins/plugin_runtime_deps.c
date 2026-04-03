/**
 * Verify that third-party shared libraries expected next to the executable exist
 * before registering format plugins (portable deployment checks).
 *
 * Windows: GCC/MinGW only; DLL names use lib*-prefixed imports as copied
 * into the runtime directory.
 *
 * Unix (Linux, macOS): looks for the same logical libraries as shared objects or
 * dylibs next to the executable (typical for relocatable bundles).
 */

#include "plugins/plugin_runtime_deps.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

static gboolean
file_exists_in_dir(const gchar* app_dir, const gchar* filename) {
    gchar* path = g_build_filename(app_dir, filename, NULL);
    gboolean ok = g_file_test(path, G_FILE_TEST_IS_REGULAR);
    g_free(path);
    return ok;
}

static gboolean
any_file_exists_in_dir(const gchar* app_dir, const gchar* const* names, gsize n) {
    for (gsize i = 0; i < n; i++) {
        if (file_exists_in_dir(app_dir, names[i])) {
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean
all_files_exist_in_dir(const gchar* app_dir, const gchar* const* names, gsize n) {
    for (gsize i = 0; i < n; i++) {
        if (!file_exists_in_dir(app_dir, names[i])) {
            return FALSE;
        }
    }
    return TRUE;
}

#ifdef _WIN32

gboolean
plugin_runtime_deps_zlib_ok(const gchar* app_dir) {
    static const gchar* alt[] = {"zlib1.dll", "libz.dll"};
    return any_file_exists_in_dir(app_dir, alt, G_N_ELEMENTS(alt));
}

gboolean
plugin_runtime_deps_jpeg_ok(const gchar* app_dir) {
    static const gchar* alt[] = {"libjpeg-8.dll", "libjpeg.dll"};
    return any_file_exists_in_dir(app_dir, alt, G_N_ELEMENTS(alt));
}

gboolean
plugin_runtime_deps_png_ok(const gchar* app_dir) {
    static const gchar* req[] = {"zlib1.dll", "libpng16.dll"};
    return all_files_exist_in_dir(app_dir, req, G_N_ELEMENTS(req));
}

gboolean
plugin_runtime_deps_webp_ok(const gchar* app_dir) {
    static const gchar* req[] = {"libwebp.dll", "libwebpdemux.dll", "libwebpmux.dll"};
    return all_files_exist_in_dir(app_dir, req, G_N_ELEMENTS(req));
}

gboolean
plugin_runtime_deps_tiff_ok(const gchar* app_dir) {
    if (!file_exists_in_dir(app_dir, "libtiff-6.dll")) {
        return FALSE;
    }
    /* zlib + jpeg: accept alternate jpeg DLL name used by some layouts */
    if (!file_exists_in_dir(app_dir, "zlib1.dll")) {
        return FALSE;
    }
    if (file_exists_in_dir(app_dir, "libjpeg-8.dll") || file_exists_in_dir(app_dir, "libjpeg.dll")) {
        return TRUE;
    }
    return FALSE;
}

gboolean
plugin_runtime_deps_heic_ok(const gchar* app_dir) {
    if (!file_exists_in_dir(app_dir, "libde265.dll")) {
        return FALSE;
    }
    static const gchar* heif_alt[] = {"libheif.dll", "heif.dll"};
    return any_file_exists_in_dir(app_dir, heif_alt, G_N_ELEMENTS(heif_alt));
}

static gboolean
win_dir_name_matches_shared_lib(const gchar* name) {
    return g_str_has_suffix(name, ".dll");
}

static gboolean
win_dir_has_prefix_dll(const gchar* app_dir, const gchar* prefix) {
    GDir* d = g_dir_open(app_dir, 0, NULL);
    if (!d) {
        return FALSE;
    }
    gboolean found = FALSE;
    const gchar* name;
    while ((name = g_dir_read_name(d)) != NULL) {
        if (g_str_has_prefix(name, prefix) && win_dir_name_matches_shared_lib(name)) {
            found = TRUE;
            break;
        }
    }
    g_dir_close(d);
    return found;
}

/* MinGW/GCC: libOpenEXR-4_0.dll — libOpenEXR- is also a prefix of Core/Util; exclude those */
static gboolean
win_is_openexr_imf_dll_name(const gchar* name) {
    if (!win_dir_name_matches_shared_lib(name)) {
        return FALSE;
    }
    if (!g_str_has_prefix(name, "libOpenEXR-")) {
        return FALSE;
    }
    if (g_str_has_prefix(name, "libOpenEXRCore-") || g_str_has_prefix(name, "libOpenEXRUtil-")) {
        return FALSE;
    }
    return TRUE;
}

static gboolean
win_dir_has_openexr_imf_dll(const gchar* app_dir) {
    GDir* d = g_dir_open(app_dir, 0, NULL);
    if (!d) {
        return FALSE;
    }
    gboolean found = FALSE;
    const gchar* name;
    while ((name = g_dir_read_name(d)) != NULL) {
        if (win_is_openexr_imf_dll_name(name)) {
            found = TRUE;
            break;
        }
    }
    g_dir_close(d);
    return found;
}

gboolean
plugin_runtime_deps_exr_ok(const gchar* app_dir) {
    /* zlib (OpenEXR uses it; often zlib1.dll from this build, libz.dll from some layouts) */
    static const gchar* zlib_alt[] = {"zlib1.dll", "libz.dll"};
    if (!any_file_exists_in_dir(app_dir, zlib_alt, G_N_ELEMENTS(zlib_alt))) {
        return FALSE;
    }
    /* Same OpenEXR stack as Unix: main Imf DLL, C core, Imath, IlmThread */
    if (!win_dir_has_openexr_imf_dll(app_dir)) {
        return FALSE;
    }
    if (!win_dir_has_prefix_dll(app_dir, "libOpenEXRCore-")) {
        return FALSE;
    }
    if (!win_dir_has_prefix_dll(app_dir, "libImath-")) {
        return FALSE;
    }
    if (!win_dir_has_prefix_dll(app_dir, "libIlmThread-")) {
        return FALSE;
    }
    return TRUE;
}

gboolean
plugin_runtime_deps_libde265_ok(const gchar* app_dir) {
    if (file_exists_in_dir(app_dir, "libde265.dll")) {
        return TRUE;
    }
    return win_dir_has_prefix_dll(app_dir, "libde265");
}

gboolean
plugin_runtime_deps_libaom_ok(const gchar* app_dir) {
    static const gchar* alt[] = {"libaom.dll", "aom.dll"};
    return any_file_exists_in_dir(app_dir, alt, G_N_ELEMENTS(alt));
}

gboolean
plugin_runtime_deps_libheif_ok(const gchar* app_dir) {
    static const gchar* alt[] = {"libheif.dll", "heif.dll"};
    return any_file_exists_in_dir(app_dir, alt, G_N_ELEMENTS(alt));
}

gboolean
plugin_runtime_deps_lcms2_ok(const gchar* app_dir) {
    static const gchar* alt[] = {"liblcms2.dll", "lcms2.dll"};
    return any_file_exists_in_dir(app_dir, alt, G_N_ELEMENTS(alt));
}

#else /* Unix (Linux, macOS): shared libs copied next to binary for portable installs */

static gboolean
unix_name_is_shared_lib(const gchar* name) {
    if (g_str_has_suffix(name, ".dylib")) {
        return TRUE;
    }
    if (g_str_has_suffix(name, ".so") || strstr(name, ".so.")) {
        return TRUE;
    }
    return FALSE;
}

static gboolean
unix_dir_has_shlib_prefix(const gchar* app_dir, const gchar* prefix) {
    GDir* d = g_dir_open(app_dir, 0, NULL);
    if (!d) {
        return FALSE;
    }
    gboolean found = FALSE;
    const gchar* name;
    while ((name = g_dir_read_name(d)) != NULL) {
        if (g_str_has_prefix(name, prefix) && unix_name_is_shared_lib(name)) {
            found = TRUE;
            break;
        }
    }
    g_dir_close(d);
    return found;
}

static gboolean
unix_has_zlib_shlib(const gchar* app_dir) {
    GDir* d = g_dir_open(app_dir, 0, NULL);
    if (!d) {
        return FALSE;
    }
    gboolean found = FALSE;
    const gchar* name;
    while ((name = g_dir_read_name(d)) != NULL) {
        if (!unix_name_is_shared_lib(name)) {
            continue;
        }
        /* Linux: libz.so, libz.so.1 — macOS: libz.dylib, libz.1.dylib (not libzip/libzstd) */
        if (g_str_has_prefix(name, "libz.so")) {
            found = TRUE;
            break;
        }
        if (g_str_has_prefix(name, "libz.") && g_str_has_suffix(name, ".dylib")) {
            found = TRUE;
            break;
        }
    }
    g_dir_close(d);
    return found;
}

gboolean
plugin_runtime_deps_zlib_ok(const gchar* app_dir) {
    return unix_has_zlib_shlib(app_dir);
}

gboolean
plugin_runtime_deps_jpeg_ok(const gchar* app_dir) {
    return unix_dir_has_shlib_prefix(app_dir, "libjpeg");
}

gboolean
plugin_runtime_deps_png_ok(const gchar* app_dir) {
    if (!unix_has_zlib_shlib(app_dir)) {
        return FALSE;
    }
    return unix_dir_has_shlib_prefix(app_dir, "libpng");
}

gboolean
plugin_runtime_deps_webp_ok(const gchar* app_dir) {
    if (!unix_dir_has_shlib_prefix(app_dir, "libwebpdemux")) {
        return FALSE;
    }
    if (!unix_dir_has_shlib_prefix(app_dir, "libwebpmux")) {
        return FALSE;
    }
    /* Main libwebp: libwebp.so* but not demux/mux */
    GDir* d = g_dir_open(app_dir, 0, NULL);
    if (!d) {
        return FALSE;
    }
    gboolean found = FALSE;
    const gchar* name;
    while ((name = g_dir_read_name(d)) != NULL) {
        if (!unix_name_is_shared_lib(name) || !g_str_has_prefix(name, "libwebp")) {
            continue;
        }
        if (g_str_has_prefix(name, "libwebpdemux") || g_str_has_prefix(name, "libwebpmux")) {
            continue;
        }
        found = TRUE;
        break;
    }
    g_dir_close(d);
    return found;
}

gboolean
plugin_runtime_deps_tiff_ok(const gchar* app_dir) {
    if (!unix_has_zlib_shlib(app_dir)) {
        return FALSE;
    }
    if (!unix_dir_has_shlib_prefix(app_dir, "libjpeg")) {
        return FALSE;
    }
    return unix_dir_has_shlib_prefix(app_dir, "libtiff");
}

gboolean
plugin_runtime_deps_heic_ok(const gchar* app_dir) {
    if (!unix_dir_has_shlib_prefix(app_dir, "libde265")) {
        return FALSE;
    }
    GDir* d = g_dir_open(app_dir, 0, NULL);
    if (!d) {
        return FALSE;
    }
    gboolean found = FALSE;
    const gchar* name;
    while ((name = g_dir_read_name(d)) != NULL) {
        if (!unix_name_is_shared_lib(name)) {
            continue;
        }
        if (g_str_has_prefix(name, "libheif") || g_str_has_prefix(name, "heif")) {
            found = TRUE;
            break;
        }
    }
    g_dir_close(d);
    return found;
}

/* libOpenEXR-*.so is a prefix of libOpenEXRCore-*; detect Imf (GCC-style lib* names) */
static gboolean
unix_is_openexr_imf_shlib_name(const gchar* name) {
    if (!unix_name_is_shared_lib(name)) {
        return FALSE;
    }
    if (!g_str_has_prefix(name, "libOpenEXR-")) {
        return FALSE;
    }
    if (g_str_has_prefix(name, "libOpenEXRCore-") || g_str_has_prefix(name, "libOpenEXRUtil-")) {
        return FALSE;
    }
    return TRUE;
}

static gboolean
unix_dir_has_openexr_imf_shlib(const gchar* app_dir) {
    GDir* d = g_dir_open(app_dir, 0, NULL);
    if (!d) {
        return FALSE;
    }
    gboolean found = FALSE;
    const gchar* name;
    while ((name = g_dir_read_name(d)) != NULL) {
        if (unix_is_openexr_imf_shlib_name(name)) {
            found = TRUE;
            break;
        }
    }
    g_dir_close(d);
    return found;
}

gboolean
plugin_runtime_deps_exr_ok(const gchar* app_dir) {
    if (!unix_has_zlib_shlib(app_dir)) {
        return FALSE;
    }
    if (!unix_dir_has_openexr_imf_shlib(app_dir)) {
        return FALSE;
    }
    if (!unix_dir_has_shlib_prefix(app_dir, "libOpenEXRCore-")) {
        return FALSE;
    }
    if (!unix_dir_has_shlib_prefix(app_dir, "libImath-")) {
        return FALSE;
    }
    if (!unix_dir_has_shlib_prefix(app_dir, "libIlmThread-")) {
        return FALSE;
    }
    return TRUE;
}

gboolean
plugin_runtime_deps_libde265_ok(const gchar* app_dir) {
    return unix_dir_has_shlib_prefix(app_dir, "libde265");
}

gboolean
plugin_runtime_deps_libaom_ok(const gchar* app_dir) {
    return unix_dir_has_shlib_prefix(app_dir, "libaom");
}

gboolean
plugin_runtime_deps_libheif_ok(const gchar* app_dir) {
    return unix_dir_has_shlib_prefix(app_dir, "libheif");
}

gboolean
plugin_runtime_deps_lcms2_ok(const gchar* app_dir) {
    return unix_dir_has_shlib_prefix(app_dir, "liblcms2");
}

#endif /* !_WIN32 */
