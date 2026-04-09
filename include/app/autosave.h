/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef AUTOSAVE_H
#define AUTOSAVE_H

#include "document.h"
#include <glib.h>
#include <time.h>

/**
 * Autosave recovery entry
 */
typedef struct {
    gchar* autosave_path; /* Path to autosave file */
    gchar* original_path; /* Original file path (if document was opened from disk) */
    time_t timestamp;     /* Timestamp when autosave was created */
    guint width;          /* Document width */
    guint height;         /* Document height */
    guint layer_count;    /* Number of layers */
} AutosaveRecoveryEntry;

/**
 * Initialize the autosave system
 * Should be called at application startup
 */
void autosave_init(void);

/**
 * Shutdown the autosave system
 * Should be called at application shutdown
 */
void autosave_shutdown(void);

/**
 * Set the file recovery save interval in seconds (30-2700)
 * If the autosave timer is already running, it is restarted with the new interval
 * @param seconds Interval in seconds (clamped to 30-2700)
 */
void autosave_set_interval(guint seconds);

/**
 * Register a document for autosave tracking
 * @param doc The document to track
 * @return Unique autosave ID for this document, or NULL on error
 */
gchar* autosave_register_document(ImageDocument* doc);

/**
 * Unregister a document from autosave tracking
 * @param doc The document to unregister
 */
void autosave_unregister_document(ImageDocument* doc);

/**
 * Save a document's state to autosave storage
 * @param doc The document to save
 * @return TRUE on success, FALSE on failure
 */
gboolean autosave_save_document(ImageDocument* doc);

/**
 * Load a document from autosave file
 * @param autosave_path Path to the autosave file
 * @return Newly created ImageDocument, or NULL on error
 */
ImageDocument* autosave_load_document(const gchar* autosave_path);

/**
 * Delete an autosave file
 * @param autosave_path Path to the autosave file
 */
void autosave_delete_file(const gchar* autosave_path);

/**
 * Scan autosave directory for recovery files
 * @return GList of AutosaveRecoveryEntry* entries (caller must free with autosave_free_recovery_list)
 */
GList* autosave_scan_recovery_files(void);

/**
 * Free a list of recovery entries
 * @param list GList of AutosaveRecoveryEntry* entries
 */
void autosave_free_recovery_list(GList* list);

/**
 * Free a single recovery entry
 * @param entry The recovery entry to free
 */
void autosave_free_recovery_entry(AutosaveRecoveryEntry* entry);

/**
 * Get the autosave path for a document
 * @param doc The document
 * @return Autosave file path, or NULL if document not registered
 */
const gchar* autosave_get_path(ImageDocument* doc);

/**
 * Mark document as saved (clean up autosave file)
 * @param doc The document that was saved
 */
void autosave_mark_saved(ImageDocument* doc);

/**
 * Compare function for sorting recovery entries (internal use)
 */
gint autosave_compare_recovery_entries(AutosaveRecoveryEntry* a, AutosaveRecoveryEntry* b);

#endif /* AUTOSAVE_H */
