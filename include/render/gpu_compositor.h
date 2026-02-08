#ifndef GPU_COMPOSITOR_H
#define GPU_COMPOSITOR_H

#include "document.h"
#include "render/tile.h"
#include <glib.h>

G_BEGIN_DECLS

/**
 * GPU-accelerated tile compositing using OpenGL via GLFW
 *
 * This module provides hardware-accelerated compositing for tiles, uploading
 * layer textures to the GPU and compositing them using fragment shaders.
 * This can significantly improve performance for opacity slider live preview
 * and other operations that require real-time compositing.
 *
 * The GPU compositor runs on a hidden GLFW window/context and renders to
 * framebuffer objects (FBOs), then reads back the composited pixels.
 */

/**
 * GPU device information structure
 * Used for enumerating available GPUs for user selection
 */
typedef struct {
    gchar* name;        /* Human-readable GPU name */
    gchar* vendor;      /* GPU vendor (NVIDIA, AMD, Intel, etc.) */
    gchar* renderer;    /* OpenGL renderer string */
    gint index;         /* Index in the device list */
    gboolean is_default; /* TRUE if this is the system default GPU */
} GPUDeviceInfo;

/**
 * Opaque GPU compositor handle
 */
typedef struct GPUCompositor GPUCompositor;

/**
 * Check if GPU acceleration is available on this system
 * This checks for GLFW and OpenGL support without initializing anything.
 * @return TRUE if GPU acceleration can be used, FALSE otherwise
 */
gboolean gpu_compositor_is_available(void);

/**
 * Get a list of available GPU devices
 * @param count Output: number of devices found
 * @return Array of GPUDeviceInfo structures, or NULL if none found.
 *         Caller must free with gpu_compositor_free_device_list().
 */
GPUDeviceInfo* gpu_compositor_get_device_list(gint* count);

/**
 * Free a device list returned by gpu_compositor_get_device_list()
 * @param devices Device list to free
 * @param count Number of devices in the list
 */
void gpu_compositor_free_device_list(GPUDeviceInfo* devices, gint count);

/**
 * Create a GPU compositor instance
 * Initializes GLFW, creates a hidden OpenGL context, and sets up shaders.
 * @param device_name Preferred GPU device name (NULL = use system default)
 * @return GPU compositor handle, or NULL on error
 */
GPUCompositor* gpu_compositor_create(const gchar* device_name);

/**
 * Destroy a GPU compositor and release all resources
 * @param compositor GPU compositor handle
 */
void gpu_compositor_destroy(GPUCompositor* compositor);

/**
 * Check if the GPU compositor is properly initialized and ready
 * @param compositor GPU compositor handle
 * @return TRUE if ready to composite, FALSE otherwise
 */
gboolean gpu_compositor_is_ready(GPUCompositor* compositor);

/**
 * Get information about the active GPU
 * @param compositor GPU compositor handle
 * @return GPU device info (owned by compositor, do not free), or NULL
 */
const GPUDeviceInfo* gpu_compositor_get_active_device(GPUCompositor* compositor);

/**
 * Composite a tile using GPU acceleration
 * This uploads layer textures to the GPU, composites them using shaders,
 * and reads back the result to the tile's pixel buffer.
 * @param compositor GPU compositor handle
 * @param doc Document containing layers
 * @param tile Tile with allocated pixel_buffer
 * @param tile_x Tile X coordinate
 * @param tile_y Tile Y coordinate
 * @return TRUE if successful, FALSE on error (falls back to CPU)
 */
gboolean gpu_compositor_composite_tile(GPUCompositor* compositor,
                                       ImageDocument* doc,
                                       Tile* tile,
                                       gint tile_x,
                                       gint tile_y);

/**
 * Upload a layer's surface data to GPU texture cache
 * Call this when a layer's surface changes to update the GPU texture.
 * The texture is cached and reused for subsequent compositing.
 * @param compositor GPU compositor handle
 * @param layer Layer to upload
 * @return TRUE if successful
 */
gboolean gpu_compositor_upload_layer(GPUCompositor* compositor, ImageLayer* layer);

/**
 * Invalidate a layer's GPU texture cache
 * Call this when a layer is modified to force re-upload on next composite.
 * @param compositor GPU compositor handle
 * @param layer Layer to invalidate
 */
void gpu_compositor_invalidate_layer(GPUCompositor* compositor, ImageLayer* layer);

/**
 * Clear all cached GPU textures
 * Call this when document changes significantly (e.g., new document loaded).
 * @param compositor GPU compositor handle
 */
void gpu_compositor_clear_cache(GPUCompositor* compositor);

/**
 * Get GPU compositor statistics for debugging/performance monitoring
 * @param compositor GPU compositor handle
 * @param tiles_composited Output: total tiles composited by GPU
 * @param textures_cached Output: number of textures currently cached
 * @param memory_used Output: estimated GPU memory usage in bytes
 */
void gpu_compositor_get_stats(GPUCompositor* compositor,
                              guint64* tiles_composited,
                              guint* textures_cached,
                              gsize* memory_used);

G_END_DECLS

#endif /* GPU_COMPOSITOR_H */
