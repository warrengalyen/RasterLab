#ifndef IMAGE_IO_H
#define IMAGE_IO_H

#include "document.h"
#include "image_format_plugin.h"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load an image file using the plugin system
 * @param doc Document to load image into
 * @param filename Path to the image file
 * @return TRUE on success, FALSE on failure
 */
gboolean image_io_load(ImageDocument* doc, const char* filename);

/**
 * Save an image file using the plugin system
 * @param doc Document to save
 * @param filename Path to save the file
 * @param opts Save options (can be NULL for defaults)
 * @return TRUE on success, FALSE on failure
 */
gboolean image_io_save(ImageDocument* doc, const char* filename, const SaveOptions* opts);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_IO_H */
