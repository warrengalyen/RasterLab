/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#include "i18n.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include "debug_logger.h"

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "rasterlab"
#endif

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/share/locale"
#endif

typedef struct {
    const char* locale; /* e.g. es_ES */
    const char* label;  /* native language + region */
} LocaleDisplayEntry;

/* Display names in native language (language + country/region). */
static const LocaleDisplayEntry k_locale_labels[] = {
    {"de_DE", "Deutsch (Deutschland)"},
    {"en_GB", "English (United Kingdom)"},
    {"en_US", "English (United States)"},
    {"es_ES", "Español (España)"},
    {"es_MX", "Español (México)"},
    {"fr_FR", "Français (France)"},
    {"it_IT", "Italiano (Italia)"},
    {"ja_JP", "日本語 (日本)"},
    {"ko_KR", "한국어 (대한민국)"},
    {"nl_NL", "Nederlands (Nederland)"},
    {"pl_PL", "Polski (Polska)"},
    {"pt_BR", "Português (Brasil)"},
    {"pt_PT", "Português (Portugal)"},
    {"ru_RU", "Русский (Россия)"},
    {"sv_SE", "Svenska (Sverige)"},
    {"uk_UA", "Українська (Україна)"},
    {"zh_CN", "中文 (中国)"},
    {"zh_TW", "中文 (台灣)"},
};

static const char* lookup_locale_label(const char* locale) {
    char buf[32];
    size_t i;
    size_t j;

    if (!locale || !locale[0]) {
        return NULL;
    }
    /* Normalize to ll_CC */
    for (i = 0; locale[i] && i < sizeof(buf) - 1; i++) {
        buf[i] = (locale[i] == '-') ? '_' : (char)g_ascii_tolower((guchar)locale[i]);
    }
    buf[i] = '\0';
    if (i >= 2) {
        buf[0] = (char)g_ascii_tolower((guchar)buf[0]);
        buf[1] = (char)g_ascii_tolower((guchar)buf[1]);
    }
    if (i >= 5 && buf[2] == '_') {
        buf[3] = (char)g_ascii_toupper((guchar)buf[3]);
        buf[4] = (char)g_ascii_toupper((guchar)buf[4]);
    }
    for (j = 0; j < G_N_ELEMENTS(k_locale_labels); j++) {
        if (strcmp(buf, k_locale_labels[j].locale) == 0) {
            return k_locale_labels[j].label;
        }
    }
    return NULL;
}

/**
 * True if bind_root/<locale_tag>/LC_MESSAGES/<GETTEXT_PACKAGE>.mo exists.
 */
static gboolean i18n_mo_exists_at(const gchar* bind_root, const gchar* locale_tag) {
    gchar* p;
    gboolean ok;

    if (!bind_root || !locale_tag || !locale_tag[0]) {
        return FALSE;
    }
    p = g_build_filename(bind_root, locale_tag, "LC_MESSAGES", GETTEXT_PACKAGE ".mo", NULL);
    ok = g_file_test(p, G_FILE_TEST_EXISTS);
    g_free(p);
    return ok;
}

/**
 * Pick bindtextdomain() base: app_dir/languages or app_dir/locale (GNU layout).
 * If both exist, prefer the tree that contains the requested catalog; otherwise prefer locale/.
 */
static gchar* i18n_pick_bind_locale_root(const gchar* app_dir, const gchar* locale_code_or_null) {
    gchar* languages_root;
    gchar* locale_root;
    gboolean have_lang;
    gboolean have_loc;

    if (!app_dir || !app_dir[0]) {
        return NULL;
    }

    languages_root = g_build_filename(app_dir, "languages", NULL);
    locale_root = g_build_filename(app_dir, "locale", NULL);
    have_lang = g_file_test(languages_root, G_FILE_TEST_IS_DIR);
    have_loc = g_file_test(locale_root, G_FILE_TEST_IS_DIR);

    if (locale_code_or_null && locale_code_or_null[0]) {
        if (have_loc && i18n_mo_exists_at(locale_root, locale_code_or_null)) {
            g_free(languages_root);
            return locale_root;
        }
        if (have_lang && i18n_mo_exists_at(languages_root, locale_code_or_null)) {
            g_free(locale_root);
            return languages_root;
        }
    }

    /* Default root when no exact match (or no locale requested): prefer locale/ over languages/. */
    if (have_loc) {
        g_free(languages_root);
        return locale_root;
    }
    if (have_lang) {
        g_free(locale_root);
        return languages_root;
    }

    g_free(languages_root);
    g_free(locale_root);
    return NULL;
}

static gboolean i18n_locale_tag_is_spanish_family(const gchar* tag) {
    if (!tag || !tag[0]) {
        return FALSE;
    }
    if (g_ascii_tolower((guchar)tag[0]) != 'e') {
        return FALSE;
    }
    if (g_ascii_tolower((guchar)tag[1]) != 's') {
        return FALSE;
    }
    return tag[2] == '\0' || tag[2] == '_' || tag[2] == '-';
}

/**
 * Map saved LANGUAGE (e.g. es_MX) to a tag that actually exists on disk (e.g. es_419).
 * Caller must g_free the result when non-NULL.
 */
static gchar* i18n_effective_language_env_tag(const gchar* bind_root, const gchar* requested) {
    static const gchar* spanish_try[] = {"es_MX", "es_419", "es_ES", "es", NULL};
    gsize i;

    if (!requested || !requested[0]) {
        return NULL;
    }
    if (!bind_root || !g_file_test(bind_root, G_FILE_TEST_IS_DIR)) {
        return g_strdup(requested);
    }
    if (i18n_mo_exists_at(bind_root, requested)) {
        return g_strdup(requested);
    }
    if (!i18n_locale_tag_is_spanish_family(requested)) {
        return g_strdup(requested);
    }
    for (i = 0; spanish_try[i]; i++) {
        if (i18n_mo_exists_at(bind_root, spanish_try[i])) {
            return g_strdup(spanish_try[i]);
        }
    }
    return g_strdup(requested);
}

static void collect_mo_from_dir(const gchar* base, GPtrArray* out, GHashTable* seen) {
    GDir* dir;
    const gchar* name;

    dir = g_dir_open(base, 0, NULL);
    if (!dir) {
        return;
    }
    while ((name = g_dir_read_name(dir))) {
        gchar* mo;

        if (name[0] == '.') {
            continue;
        }
        if (g_hash_table_lookup(seen, name)) {
            continue;
        }
        mo = g_build_filename(base, name, "LC_MESSAGES", GETTEXT_PACKAGE ".mo", NULL);
        if (g_file_test(mo, G_FILE_TEST_EXISTS)) {
            g_hash_table_insert(seen, g_strdup(name), GINT_TO_POINTER(1));
            g_ptr_array_add(out, g_strdup(name));
        }
        g_free(mo);
    }
    g_dir_close(dir);
}

void i18n_apply_locale(const gchar* app_dir, const gchar* locale_code_or_null) {
#ifdef HAVE_GETTEXT
    gchar* user_base;
    gchar* effective_lang = NULL;
    const gchar* bind_path;

    user_base = i18n_pick_bind_locale_root(app_dir, locale_code_or_null);
    if (user_base) {
#ifdef _WIN32
        /* MinGW gettext resolves catalogs more reliably with an absolute bind path. */
        {
            gchar* abs = g_canonicalize_filename(user_base, NULL);

            if (abs) {
                g_free(user_base);
                user_base = abs;
            }
        }
#endif
        bind_path = user_base;
    } else {
        bind_path = LOCALEDIR;
    }

    if (locale_code_or_null && locale_code_or_null[0]) {
        effective_lang = i18n_effective_language_env_tag(user_base, locale_code_or_null);
        g_setenv("LANGUAGE", effective_lang, TRUE);
        g_setenv("LANG", effective_lang, TRUE);
    } else {
        g_unsetenv("LANGUAGE");
    }

    setlocale(LC_ALL, "");

    bindtextdomain(GETTEXT_PACKAGE, bind_path);
    /* GTK uses UTF-8; ensure translated strings are not recoded incorrectly (esp. Windows). */
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    debug_log("DBG", "i18n: bindtextdomain domain=%s path=%s (user tree=%s)",
              GETTEXT_PACKAGE,
              bind_path,
              user_base ? "yes" : "no (using LOCALEDIR)");
    debug_log("DBG", "i18n: getenv LANGUAGE=%s LANG=%s",
              g_getenv("LANGUAGE") ? g_getenv("LANGUAGE") : "(null)",
              g_getenv("LANG") ? g_getenv("LANG") : "(null)");
    if (locale_code_or_null && locale_code_or_null[0]) {
        debug_log("DBG", "i18n: settings locale tag %s", locale_code_or_null);
        if (effective_lang && g_strcmp0(effective_lang, locale_code_or_null) != 0) {
            debug_log("DBG", 
                "i18n: LANGUAGE effective=%s (catalog for %s not under bind path; using existing MO directory)",
                effective_lang,
                locale_code_or_null);
        }
    } else {
        debug_log("DBG", "i18n: LANGUAGE unset — using system locale / default messages");
    }

    g_free(effective_lang);
    g_free(user_base);
#else
    (void)app_dir;
    (void)locale_code_or_null;
    debug_log("DBG", "i18n: gettext disabled at build time (HAVE_GETTEXT off)");
#endif
}

static gint str_ptr_cmp(gconstpointer a, gconstpointer b) {
    return strcmp(*(const gchar* const*)a, *(const gchar* const*)b);
}

GPtrArray* i18n_collect_mo_locales(const gchar* app_dir) {
    GPtrArray* out = g_ptr_array_new_with_free_func(g_free);
    GHashTable* seen;
    gchar* base_lang;
    gchar* base_loc;

    if (!app_dir) {
        debug_log("DBG", "i18n: collect locales skipped (app_dir is NULL)");
        return out;
    }

    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    base_lang = g_build_filename(app_dir, "languages", NULL);
    base_loc = g_build_filename(app_dir, "locale", NULL);

    if (g_file_test(base_lang, G_FILE_TEST_IS_DIR)) {
        collect_mo_from_dir(base_lang, out, seen);
    }
    if (g_file_test(base_loc, G_FILE_TEST_IS_DIR)) {
        collect_mo_from_dir(base_loc, out, seen);
    }

    g_hash_table_destroy(seen);
    g_free(base_lang);
    g_free(base_loc);
    g_ptr_array_sort(out, str_ptr_cmp);

    if (out->len == 0) {
        debug_log("DBG", "i18n: no %s catalogs under \"%s/languages\" or \"%s/locale\" (expected .../<locale>/LC_MESSAGES/%s.mo)",
                  GETTEXT_PACKAGE, app_dir, app_dir, GETTEXT_PACKAGE);
    } else {
        GString* s = g_string_new(NULL);
        guint i;

        for (i = 0; i < out->len; i++) {
            if (i) {
                g_string_append_c(s, ',');
            }
            g_string_append(s, (const gchar*)g_ptr_array_index(out, i));
        }
        debug_log("DBG", "i18n: found %u language(s) in app dir: [%s]", out->len, s->str);
        g_string_free(s, TRUE);
    }

    return out;
}

gchar* i18n_locale_menu_label(const gchar* locale_code) {
    const char* known;

    if (!locale_code || !locale_code[0]) {
        return g_strdup(_("System default"));
    }
    known = lookup_locale_label(locale_code);
    if (known) {
        /* Labels are already in native language + region; do not run through gettext. */
        return g_strdup(known);
    }
    /* Fallback: show tag as stored (e.g. uncommon locale) */
    return g_strdup(locale_code);
}
