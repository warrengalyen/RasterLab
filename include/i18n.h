/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef RASTERLAB_I18N_H
#define RASTERLAB_I18N_H

#include <glib.h>
#include <locale.h>

#ifdef HAVE_GETTEXT
#include <libintl.h>
#define _(String) gettext(String)
#else
#define _(String) (String)
#endif

#define N_(String) (String)

/**
 * Apply gettext domain and optional LANGUAGE for UI locale.
 * @param app_dir Application directory (contains languages/ when using portable MOs)
 * @param locale_code_or_null Locale tag (e.g. es_ES) or NULL for system default
 */
void i18n_apply_locale(const gchar* app_dir, const gchar* locale_code_or_null);

/**
 * List locale directory names under app_dir/languages that contain LC_MESSAGES/rasterlab.mo.
 * Caller must g_ptr_array_unref; strings are freed with the array.
 */
GPtrArray* i18n_collect_mo_locales(const gchar* app_dir);

/**
 * Label for a language menu row: native language + region, or "System default" for NULL.
 * Caller must g_free the result.
 */
gchar* i18n_locale_menu_label(const gchar* locale_code);

#endif /* RASTERLAB_I18N_H */
