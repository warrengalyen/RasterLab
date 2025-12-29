#ifndef IMAGE_FORMAT_PLUGIN_H
#define IMAGE_FORMAT_PLUGIN_H

/**
 * Image Format Plugin System
 *
 * This header defines the stable C ABI interface for image format plugins.
 * Plugins implement these interfaces to provide image loading/saving functionality
 * without requiring recompilation of the core application.
 *
 * Version: 1.0
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct ImageDocument ImageDocument;
typedef struct ImageLayer ImageLayer;

/**
 * Current API version - increment when breaking changes are made
 */
#define IMAGE_FORMAT_PLUGIN_API_VERSION 1

/**
 * Error codes returned by plugin operations
 */
typedef enum {
    PLUGIN_ERROR_NONE = 0,
    PLUGIN_ERROR_INVALID_PARAMETERS = 1,
    PLUGIN_ERROR_FILE_NOT_FOUND = 2,
    PLUGIN_ERROR_FILE_READ_ERROR = 3,
    PLUGIN_ERROR_FILE_WRITE_ERROR = 4,
    PLUGIN_ERROR_UNSUPPORTED_FORMAT = 5,
    PLUGIN_ERROR_CORRUPT_FILE = 6,
    PLUGIN_ERROR_OUT_OF_MEMORY = 7,
    PLUGIN_ERROR_UNSUPPORTED_FEATURE = 8,
    PLUGIN_ERROR_UNKNOWN = 99
} PluginError;

/**
 * Save options for plugins
 */
typedef struct {
    /* Quality setting (0-100, -1 for default) */
    int32_t quality;

    /* Compression level (0-9, -1 for default) */
    int32_t compression_level;

    /* Save with alpha channel (if supported) */
    bool preserve_alpha;

    /* Flatten layers to single layer */
    bool flatten_layers;

    /* Reserved for future use - must be zero */
    uint32_t reserved[8];
} SaveOptions;

/**
 * Pixel buffer descriptor for accessing image data
 * This provides a stable interface to image pixel data
 */
typedef struct {
    /* Pointer to pixel data (ARGB32 format, pre-multiplied alpha) */
    uint8_t* pixels;

    /* Width in pixels */
    uint32_t width;

    /* Height in pixels */
    uint32_t height;

    /* Stride (bytes per row) */
    uint32_t stride;

    /* Number of channels (3 = RGB, 4 = RGBA) */
    uint32_t channels;

    /* Bits per channel (typically 8) */
    uint32_t bit_depth;

    /* Whether data has alpha channel */
    bool has_alpha;
} PixelBuffer;

/**
 * Layer descriptor for plugin access
 */
typedef struct {
    /* Layer name (UTF-8, null-terminated) */
    const char* name;

    /* Pixel buffer for layer content */
    PixelBuffer* buffer;

    /* Layer opacity (0.0 - 1.0) */
    double opacity;

    /* Whether layer is visible */
    bool visible;

    /* Layer offset X */
    int32_t offset_x;

    /* Layer offset Y */
    int32_t offset_y;

    /* Reserved for future use */
    uint32_t reserved[4];
} LayerDescriptor;

/**
 * Host API provided to plugins
 * Plugins use these functions to interact with the host application
 */
typedef struct ImageFormatHostAPI {
    /* API version - must match IMAGE_FORMAT_PLUGIN_API_VERSION */
    uint32_t api_version;

    /* Plugin version (set by plugin) */
    uint32_t plugin_version;

    /* Memory allocation helpers */
    void* (*malloc)(size_t size);
    void* (*calloc)(size_t nmemb, size_t size);
    void* (*realloc)(void* ptr, size_t size);
    void (*free)(void* ptr);

    /* Logging callbacks */
    void (*log_error)(const char* format, ...);
    void (*log_warning)(const char* format, ...);
    void (*log_info)(const char* format, ...);
    void (*log_debug)(const char* format, ...);

    /* Document manipulation helpers */
    /* Create a new document with specified dimensions */
    ImageDocument* (*document_create)(uint32_t width, uint32_t height, bool has_alpha);

    /* Create a new layer and add it to document */
    ImageLayer* (*layer_create)(ImageDocument* doc, const char* name,
                                uint32_t width, uint32_t height, bool has_alpha);

    /* Get pixel buffer for a layer (for writing pixel data) */
    bool (*layer_get_pixel_buffer)(ImageLayer* layer, PixelBuffer* out_buffer);

    /* Get pixel buffer for document composite (flattened view) */
    bool (*document_get_composite_pixels)(ImageDocument* doc, PixelBuffer* out_buffer);

    /* Set document metadata */
    void (*document_set_metadata)(ImageDocument* doc, uint32_t width, uint32_t height,
                                  uint32_t channels, uint32_t bit_depth, bool has_alpha);

    /* Get number of layers in document */
    uint32_t (*document_get_layer_count)(ImageDocument* doc);

    /* Get layer at index (0 = bottom layer) */
    LayerDescriptor* (*document_get_layer_descriptor)(ImageDocument* doc, uint32_t index);

    /* Release layer descriptor (call after using LayerDescriptor) */
    void (*layer_descriptor_free)(LayerDescriptor* desc);

    /* Reserved for future API additions - must be NULL */
    void* reserved[16];
} ImageFormatHostAPI;

/**
 * Format information structure
 */
typedef struct {
    /* Format name (e.g., "PNG", "JPEG") */
    const char* name;

    /* File extensions (comma-separated, e.g., "png", "jpg,jpeg") */
    const char* extensions;

    /* Whether format supports alpha channel */
    bool supports_alpha;

    /* Whether format supports multiple layers (optional) */
    bool supports_layers;

    /* Priority for extension conflicts (higher = preferred) */
    int32_t priority;

    /* Reserved for future use */
    uint32_t reserved[4];
} FormatInfo;

/**
 * Plugin callbacks structure
 * Plugins must implement these functions
 */
typedef struct {
    /* Check if plugin can load a file by header bytes */
    /* Returns true if file appears to be in this format */
    bool (*can_load)(const char* filename, const uint8_t* header, size_t header_size);

    /* Load image from file into document */
    /* Returns true on success, false on error */
    PluginError (*load)(ImageDocument* doc, const char* filename);

    /* Check if plugin can save to a file */
    /* Returns true if plugin supports saving with this filename/extension */
    bool (*can_save)(const char* filename);

    /* Save document to file */
    /* Returns PLUGIN_ERROR_NONE on success */
    PluginError (*save)(ImageDocument* doc, const char* filename, const SaveOptions* opts);

    /* Optional: Get format information */
    /* If NULL, will use default info from plugin registration */
    FormatInfo* (*get_format_info)(void);

    /* Optional: Plugin cleanup/shutdown */
    /* Called when plugin is unloaded */
    void (*cleanup)(void);
} ImageFormatPluginCallbacks;

/**
 * Plugin structure returned by plugin_init
 */
typedef struct {
    /* Plugin version (for API compatibility checking) */
    uint32_t plugin_version;

    /* Format information */
    FormatInfo format_info;

    /* Callbacks */
    ImageFormatPluginCallbacks callbacks;

    /* Reserved for future use */
    uint32_t reserved[8];
} ImageFormatPlugin;

/**
 * Plugin entry point - REQUIRED
 *
 * Every plugin must export this function:
 *   bool plugin_init(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);
 *
 * @param host Host API structure provided by the application
 * @param out_plugin Output structure to populate with plugin information and callbacks
 * @return true if plugin initialized successfully, false otherwise
 */
/* Function signature:
bool plugin_init(const ImageFormatHostAPI* host, ImageFormatPlugin* out_plugin);
*/

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_FORMAT_PLUGIN_H */
