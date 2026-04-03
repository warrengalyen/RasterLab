#include "document.h"
#include "i18n.h"
#include "app/settings.h"
#if HAVE_LCMS2
#include "color_manager.h"
#include "color_manager/display_profile.h"
#include "color_manager/icc_utils.h"
#include <gdk/gdk.h>
#endif
#include "command.h"
#include "io/image_io.h"
#include "render/compositor.h"
#include "render/gpu_compositor.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "render/text_layer.h"
#include "render/tile.h"
#include "render/tile_thread_pool.h"
#include "render/tile_worker.h"
#include "selection.h"
#include "selection/selection_mask.h"
#include "tool_manager.h"
#include "tool_options.h"
#include "tools.h"
#include "tools/tool_colorpicker.h"
#include "tools/tool_crop.h"
#include "tools/tool_text.h"
#include "tools/tool_ellipse_select.h"
#include "tools/tool_lasso_select.h"
#include "tools/tool_magic_wand_select.h"
#include "tools/tool_move.h"
#include "tools/tool_polygon_select.h"
#include "tools/tool_rect_select.h"
#include "ui.h"
#include "ui/layers_panel.h"
#include "ui/widgets/canvas_ruler.h"
#include "ui/workspace.h"
#include "undo/undo_disk.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug_logger.h"

/* Forward declarations */
static void on_scroll_adjustment_changed(GtkAdjustment* adjustment, gpointer user_data);
static void on_scrolled_window_adjustment_notify(GObject* object, GParamSpec* pspec, gpointer user_data);
static gboolean on_viewport_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_viewport_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_viewport_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data);
static gboolean on_viewport_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);
/** Queue only the ruler strips containing the mouse marker (old and new) for minimal redraw. */
static void queue_ruler_marker_areas(ImageDocument* doc, gdouble old_cx, gdouble old_cy, gdouble new_cx, gdouble new_cy);

#if HAVE_LCMS2
typedef struct {
    ColorTransform* transform;
    ColorProfile* srgb_profile;
    ColorProfile* display_profile;
    gchar* display_id;
    gchar* custom_profile_path;
    gint mode;
    gint intent;
    gboolean bpc;
} DisplayTransformCache;

static void display_xform_cache_free(DisplayTransformCache* cache) {
    if (!cache)
        return;
    cm_transform_destroy(cache->transform);
    cm_profile_destroy(cache->srgb_profile);
    cm_profile_destroy(cache->display_profile);
    g_free(cache->display_id);
    g_free(cache->custom_profile_path);
    g_free(cache);
}

static gboolean display_xform_cache_valid(const DisplayTransformCache* cache,
                                          const gchar* display_id, gint mode,
                                          gint intent, gboolean bpc,
                                          const gchar* custom_profile_path) {
    if (!cache || !cache->transform)
        return FALSE;
    if (cache->mode != mode || cache->intent != intent || cache->bpc != bpc)
        return FALSE;
    if (g_strcmp0(cache->display_id, display_id) != 0)
        return FALSE;
    if (mode == CM_MODE_CUSTOM_PROFILE &&
        g_strcmp0(cache->custom_profile_path, custom_profile_path) != 0)
        return FALSE;
    return TRUE;
}
#endif

/**
 * Animation timer callback - updates selection marching ants
 */
static gboolean on_selection_animation_timer(gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;

    if (!doc) {
        return FALSE; /* Stop timer if document is gone */
    }

    /* Check if drawing_area is still valid - if NULL, document is being destroyed */
    if (!doc->drawing_area || !GTK_IS_WIDGET(doc->drawing_area)) {
        doc->selection_animation_timer_id = 0; /* Clear timer ID */
        return FALSE;                          /* Stop timer if widget is gone */
    }

    /* Check if animation is enabled for rect select tool */
    ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
    gboolean animate_enabled = opts ? tool_options_get_rect_select_animate(opts) : TRUE;

    /* Update animation for mask-based selection if animation is enabled */
    if (animate_enabled && doc->selection_mask && !selection_mask_is_empty(doc->selection_mask)) {
        /* Advance animation phase (0-3 for 4-pixel dashes) */
        doc->selection_animation_phase = (doc->selection_animation_phase + 1) % 4;
    }

    /* Queue redraw to show animation */
    if (doc->drawing_area && GTK_IS_WIDGET(doc->drawing_area)) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Keep timer running - it needs to stay active for when selections are created */
    return TRUE;
}

/**
 * Callback for scroll adjustment changes - triggers redraw
 */
static void on_scroll_adjustment_changed(GtkAdjustment* adjustment, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    LayersPanel* layers_panel;
    GdkWindow* gdk_window;

    (void)adjustment; /* Unused */

    if (doc && doc->drawing_area && GTK_IS_WIDGET(doc->drawing_area)) {
        /* Invalidate the layout/viewport */
        if (doc->viewport && GTK_IS_WIDGET(doc->viewport)) {
            gdk_window = gtk_widget_get_window(doc->viewport);
            if (gdk_window) {
                gdk_window_invalidate_rect(gdk_window, NULL, TRUE);
            }
            gtk_widget_queue_draw(doc->viewport);
        }

        /* Invalidate the drawing area window */
        gdk_window = gtk_widget_get_window(doc->drawing_area);
        if (gdk_window) {
            gdk_window_invalidate_rect(gdk_window, NULL, TRUE);
        }

        gtk_widget_queue_draw(doc->drawing_area);

        /* Update overview widget selection rectangle when scrolling */
        layers_panel = (LayersPanel*)g_object_get_data(G_OBJECT(doc->drawing_area), "layers_panel");
        if (layers_panel && layers_panel->current_doc == doc) {
            /* Get workspace from window to access overview widget */
            GtkWidget* window = gtk_widget_get_toplevel(doc->drawing_area);
            if (GTK_IS_WINDOW(window)) {
                Workspace* workspace = (Workspace*)g_object_get_data(G_OBJECT(window), "workspace");
                if (workspace) {
                    GtkWidget* overview_widget = workspace_get_overview_widget(workspace);
                    if (overview_widget) {
                        gtk_widget_queue_draw(overview_widget);
                    }
                }
            }
        }
    }
}

/**
 * Callback for when scroll adjustments are created/updated
 */
static void on_scrolled_window_adjustment_notify(GObject* object, GParamSpec* pspec, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    GtkScrolledWindow* scrolled_window = GTK_SCROLLED_WINDOW(object);
    GtkAdjustment* adj;

    (void)pspec; /* Unused */

    if (!doc || !scrolled_window) {
        return;
    }

    /* Get the adjustment that was just created/updated */
    if (g_strcmp0(pspec->name, "hadjustment") == 0) {
        adj = gtk_scrolled_window_get_hadjustment(scrolled_window);
    } else if (g_strcmp0(pspec->name, "vadjustment") == 0) {
        adj = gtk_scrolled_window_get_vadjustment(scrolled_window);
    } else {
        return;
    }

    /* Connect to value-changed signal if adjustment exists and not already connected */
    if (adj && !g_signal_handler_find(adj, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                      G_CALLBACK(on_scroll_adjustment_changed), doc)) {
        g_signal_connect(adj, "value-changed",
                         G_CALLBACK(on_scroll_adjustment_changed), doc);
    }
}

/**
 * Forward declarations for mouse event handlers
 */
static gboolean on_drawing_area_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_drawing_area_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static gboolean on_drawing_area_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data);
static gboolean on_drawing_area_enter_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);
static gboolean on_drawing_area_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);

/**
 * Drawing area draw callback
 */
/**
 * Drawing area draw callback - TILE-BASED RENDERING
 *
 * OLD BEHAVIOR: Drew entire composite surface at once
 * NEW BEHAVIOR: Loops over tiles and draws only visible tiles
 *
 * This dramatically improves performance for large images by:
 * - Only compositing dirty tiles instead of entire surface
 * - Only drawing tiles that are visible in the viewport
 * - Caching composited tiles to avoid recompositing
 */
static gboolean on_drawing_area_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    double x1, y1, x2, y2;
    gint clip_width, clip_height;
    gint viewport_x, viewport_y, viewport_w, viewport_h;
    gint start_tile_x, start_tile_y, end_tile_x, end_tile_y;
    gint tx, ty;
    Tile* tile;
    gdouble zoom;
    gboolean use_gpu_compositing = FALSE;

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so just draw empty background */
    if (!doc || !doc->drawing_area) {
        cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
        clip_width = (gint)(x2 - x1);
        clip_height = (gint)(y2 - y1);
        draw_checkered_background(cr, clip_width, clip_height);
        return FALSE;
    }

    /* Check if GPU compositing should be used based on settings */
    if (doc->gpu_compositor && gpu_compositor_is_ready(doc->gpu_compositor)) {
        gpointer ctx_data = g_object_get_data(G_OBJECT(widget), "app_context");
        if (ctx_data) {
            AppContext* ctx = (AppContext*)ctx_data;
            if (ctx->settings && settings_get_gpu_acceleration_enabled(ctx->settings)) {
                use_gpu_compositing = TRUE;
            }
        }
    }

#if HAVE_LCMS2
    /* Display color management: sRGB -> system or custom profile.
     * Skipped when mode is CM_MODE_NONE (no transform, small performance gain).
     * Applied for both CPU and GPU compositing (GPU writes to tile->pixel_buffer via glReadPixels).
     * The transform is cached in doc->display_xform_cache and reused across draw calls
     * as long as the CMS settings and target monitor remain unchanged. */
    ColorTransform* display_xform = NULL;
    {
        gpointer ctx_data = g_object_get_data(G_OBJECT(widget), "app_context");
        if (ctx_data) {
            AppContext* ctx = (AppContext*)ctx_data;
            Settings* settings = ctx ? ctx->settings : NULL;
            gint cm_mode = settings ? settings_get_cm_mode(settings) : CM_MODE_NONE;

            if (settings && cm_mode != CM_MODE_NONE) {
                gchar* cur_display_id = NULL;
                GdkWindow* win = gtk_widget_get_window(widget);
                if (win) {
                    GdkDisplay* gdk_display = gdk_window_get_display(win);
                    GdkMonitor* monitor = gdk_display_get_monitor_at_window(gdk_display, win);
                    if (monitor && gdk_display) {
                        gint n = (gint)gdk_display_get_n_monitors(gdk_display);
                        for (gint i = 0; i < n; i++) {
                            if (gdk_display_get_monitor(gdk_display, i) == monitor) {
                                cur_display_id = g_strdup_printf("monitor-%d", i);
                                break;
                            }
                        }
                    }
                }
                if (cur_display_id) {
                    gint intent = settings_get_cm_rendering_intent(settings);
                    gboolean bpc = settings_get_cm_black_point_compensation(settings);
                    const gchar* custom_path = (cm_mode == CM_MODE_CUSTOM_PROFILE)
                                                   ? settings_get_cm_display_profile(settings, cur_display_id)
                                                   : NULL;

                    DisplayTransformCache* cache = (DisplayTransformCache*)doc->display_xform_cache;
                    if (display_xform_cache_valid(cache, cur_display_id, cm_mode, intent, bpc, custom_path)) {
                        display_xform = cache->transform;
                        g_free(cur_display_id);
                    } else {
                        display_xform_cache_free(cache);
                        doc->display_xform_cache = NULL;

                        ColorProfile* display_prof = cm_get_display_profile(settings, cur_display_id);
                        if (display_prof) {
                            ColorProfile* srgb_prof = cm_profile_create_srgb();
                            if (srgb_prof) {
                                ColorTransform* xform = cm_transform_create_with_intent(
                                    srgb_prof, display_prof, CM_PIXELFORMAT_RGBA8,
                                    intent, bpc ? true : false);
                                if (xform) {
                                    DisplayTransformCache* new_cache = g_new0(DisplayTransformCache, 1);
                                    new_cache->transform = xform;
                                    new_cache->srgb_profile = srgb_prof;
                                    new_cache->display_profile = display_prof;
                                    new_cache->display_id = cur_display_id;
                                    new_cache->custom_profile_path = g_strdup(custom_path);
                                    new_cache->mode = cm_mode;
                                    new_cache->intent = intent;
                                    new_cache->bpc = bpc;
                                    doc->display_xform_cache = new_cache;
                                    display_xform = xform;
                                } else {
                                    cm_profile_destroy(srgb_prof);
                                    cm_profile_destroy(display_prof);
                                    g_free(cur_display_id);
                                }
                            } else {
                                cm_profile_destroy(display_prof);
                                g_free(cur_display_id);
                            }
                        } else {
                            g_free(cur_display_id);
                        }
                    }
                }
            } else {
                /* CMS disabled — drop any stale cache */
                if (doc->display_xform_cache) {
                    display_xform_cache_free((DisplayTransformCache*)doc->display_xform_cache);
                    doc->display_xform_cache = NULL;
                }
            }
        }
    }
#endif

    /* Process completed tiles from Cairo-safe worker pool
       Workers have finished compositing into pixel_buffer, now upload to Cairo.
       Pass display transform so 100% zoom tiles get display CMS when uploaded async. */
    if (doc->tile_worker_pool) {
        guint uploaded = tile_worker_pool_process_uploads(doc->tile_worker_pool,
#if HAVE_LCMS2
                                                          display_xform
#else
                                                          NULL
#endif
        );
        if (uploaded > 0) {
            g_debug("Uploaded %u tiles to Cairo surfaces", uploaded);
        }
    }

    /* Poll completed tiles from legacy thread pool (if enabled) */
    if (doc->tile_thread_pool) {
        CompletedTile completed;
        gint updated_count = 0;

        while (tile_thread_pool_pop_completed(doc->tile_thread_pool, &completed)) {
            /* Safety: check tile still exists and surface is valid */
            if (!completed.tile || !doc->tile_grid || !completed.surface) {
                if (completed.surface) {
                    cairo_surface_destroy(completed.surface);
                }
                continue;
            }

            /* Safety: verify Cairo surface is valid */
            if (cairo_surface_status(completed.surface) != CAIRO_STATUS_SUCCESS) {
                g_warning("Discarding invalid Cairo surface from worker thread");
                cairo_surface_destroy(completed.surface);
                continue;
            }

            /* Apply result if generation ID matches (not stale) */
            if (tile_apply_completed_result(completed.tile,
                                            completed.surface,
                                            completed.generation_id)) {
                /* Successfully applied, queue redraw of this tile */
                gint draw_x = completed.tile->px;
                gint draw_y = completed.tile->py;
                gint draw_w = completed.tile->w;
                gint draw_h = completed.tile->h;

                gtk_widget_queue_draw_area(widget,
                                           (gint)(draw_x * doc->zoom_factor),
                                           (gint)(draw_y * doc->zoom_factor),
                                           (gint)(draw_w * doc->zoom_factor),
                                           (gint)(draw_h * doc->zoom_factor));
                updated_count++;
            } else {
                /* Result was stale, discard it */
                cairo_surface_destroy(completed.surface);
            }
        }

        if (updated_count > 0) {
            g_debug("Applied %d completed tiles to main cache", updated_count);
        }
    }

    zoom = doc->zoom_factor;

    /* Get the clip region to determine what needs to be drawn */
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    clip_width = (gint)(x2 - x1);
    clip_height = (gint)(y2 - y1);

/* DEBUG: Set to 1 to detect and print when clip/scroll mismatch (seam condition) */
#define DEBUG_PRINT_CLIP 0
#if DEBUG_PRINT_CLIP
    {
        double dbg_scroll_x = 0, dbg_scroll_y = 0;
        if (doc->scrolled_window && GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
            GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
            GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
            if (hadj)
                dbg_scroll_x = gtk_adjustment_get_value(hadj);
            if (vadj)
                dbg_scroll_y = gtk_adjustment_get_value(vadj);
        }
        /* Only print when clip and scroll diverge by more than 1 pixel */
        double diff_x = x1 - dbg_scroll_x;
        double diff_y = y1 - dbg_scroll_y;
        if (diff_x < -1 || diff_x > 1 || diff_y < -1 || diff_y > 1) {
            g_print("MISMATCH: clip=(%.0f,%.0f) scroll=(%.0f,%.0f) diff=(%.0f,%.0f)\n",
                    x1, y1, dbg_scroll_x, dbg_scroll_y, diff_x, diff_y);
        }
    }
#endif

    /* Draw the document if image is loaded */
    if (doc->layers && g_list_length(doc->layers) > 0) {
        /* CRITICAL: Use CLIP position for viewport calculation, not scroll position!
         * Debug analysis revealed that clip.x/y often differs from scroll.x/y
         * (clip can be offset when the drawing area is centered in the viewport).
         * We must draw content for the CLIP region, which is what Cairo will actually show. */
        viewport_x = (gint)floor(x1 / zoom);
        viewport_y = (gint)floor(y1 / zoom);
        viewport_w = (gint)ceil(clip_width / zoom) + 1;
        viewport_h = (gint)ceil(clip_height / zoom) + 1;

        /* Save Cairo state before applying transforms */
        cairo_save(cr);

        /* Apply zoom transform */
        if (zoom != 1.0) {
            cairo_scale(cr, zoom, zoom);
        }

/* DEBUG: Set to 1 to disable checkered background and use solid color. */
#define DEBUG_SOLID_BACKGROUND 0

#if DEBUG_SOLID_BACKGROUND
        /* Draw solid background for ENTIRE document area (0,0 to width,height).
         * This ensures complete coverage regardless of clip/scroll mismatch. */
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_rectangle(cr, 0, 0, doc->width, doc->height);
        cairo_fill(cr);
#else
        /* Draw checkered background for visible area */
        draw_checkered_background_offset(cr, viewport_x, viewport_y, viewport_w, viewport_h);
#endif

        /* Render based on zoom level */
        if (zoom > 1.0) {
            /* Zoomed IN: Use layer-based rendering for best quality */
#if HAVE_LCMS2
            if (display_xform && viewport_w > 0 && viewport_h > 0) {
                /* Render viewport to offscreen buffer, apply display transform, then draw */
                cairo_surface_t* offscreen = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, viewport_w, viewport_h);
                if (offscreen && cairo_surface_status(offscreen) == CAIRO_STATUS_SUCCESS) {
                    cairo_t* offscreen_cr = cairo_create(offscreen);
                    if (offscreen_cr) {
                        cairo_translate(offscreen_cr, -viewport_x, -viewport_y);
                        document_render_layers_at_zoom(doc, offscreen_cr, viewport_x, viewport_y, viewport_w, viewport_h);
                        cairo_destroy(offscreen_cr);
                        cairo_surface_flush(offscreen);
                        unsigned char* data = cairo_image_surface_get_data(offscreen);
                        int stride = cairo_image_surface_get_stride(offscreen);
                        if (data && stride >= viewport_w * 4) {
                            for (gint y = 0; y < viewport_h; y++) {
                                cm_apply_transform_argb32(display_xform, data + (size_t)y * stride, (size_t)viewport_w);
                            }
                            cairo_surface_mark_dirty(offscreen);
                        }
                        cairo_save(cr);
                        cairo_translate(cr, viewport_x, viewport_y);
                        cairo_set_source_surface(cr, offscreen, 0, 0);
                        cairo_rectangle(cr, 0, 0, viewport_w, viewport_h);
                        cairo_fill(cr);
                        cairo_restore(cr);
                    }
                    cairo_surface_destroy(offscreen);
                } else if (offscreen) {
                    cairo_surface_destroy(offscreen);
                }
            } else
#endif
            {
                document_render_layers_at_zoom(doc, cr, viewport_x, viewport_y, viewport_w, viewport_h);
            }
        } else if (zoom < 1.0 && doc->tile_grid) {
            /* Zoomed OUT: Use pre-computed tile mipmaps for fast rendering */
            gint tile_size = doc->tile_grid->tile_size;

            /* Calculate which tiles are visible at this zoom level */
            start_tile_x = viewport_x / tile_size;
            start_tile_y = viewport_y / tile_size;
            end_tile_x = (viewport_x + viewport_w) / tile_size;
            end_tile_y = (viewport_y + viewport_h) / tile_size;

            /* Clamp to grid bounds */
            if (start_tile_x < 0)
                start_tile_x = 0;
            if (start_tile_y < 0)
                start_tile_y = 0;
            if (end_tile_x >= doc->tile_grid->tiles_x)
                end_tile_x = doc->tile_grid->tiles_x - 1;
            if (end_tile_y >= doc->tile_grid->tiles_y)
                end_tile_y = doc->tile_grid->tiles_y - 1;

            /* Ensure tiles are composited (mipmaps generated during compositing) */
            for (ty = start_tile_y; ty <= end_tile_y; ty++) {
                for (tx = start_tile_x; tx <= end_tile_x; tx++) {
                    tile = tile_grid_get_tile(doc->tile_grid, tx, ty);
                    if (tile && tile->dirty) {
                        /* Composite tile - use GPU if enabled, otherwise CPU */
                        if (use_gpu_compositing) {
                            tile_worker_composite_pixels_gpu(doc, tile, tx, ty);
                        } else {
                            tile_worker_composite_pixels(doc, tile, tx, ty);
                        }
                        if (tile->surface) {
                            cairo_surface_destroy(tile->surface);
                            tile->surface = NULL;
                        }
                        if (tile->pixel_buffer) {
#if HAVE_LCMS2
                            if (display_xform) {
                                cm_apply_transform_argb32(display_xform, (uint8_t*)tile->pixel_buffer,
                                                          (size_t)(tile->w * tile->h));
                            }
#endif
                            tile->surface = cairo_image_surface_create_for_data(
                                tile->pixel_buffer,
                                CAIRO_FORMAT_ARGB32,
                                tile->w,
                                tile->h,
                                tile->stride);
                            if (cairo_surface_status(tile->surface) == CAIRO_STATUS_SUCCESS) {
                                /* Mark surface as dirty to inform Cairo that pixel data
                                 * was modified externally (by SIMD blending). This is
                                 * CRITICAL - without it, Cairo may use stale cached data. */
                                cairo_surface_mark_dirty(tile->surface);
                                tile->dirty = FALSE;
                                tile->pending_upload = FALSE;
                                /* Generate mipmaps for this tile */
                                tile_generate_mipmaps(tile);
                            }
                        }
                    } else if (tile && tile->mipmaps_dirty && tile->surface) {
                        /* Tile content is valid but mipmaps need regeneration */
                        tile_generate_mipmaps(tile);
                    }
                }
            }

            /* Draw tiles using mipmaps */
            for (ty = start_tile_y; ty <= end_tile_y; ty++) {
                for (tx = start_tile_x; tx <= end_tile_x; tx++) {
                    tile = tile_grid_get_tile(doc->tile_grid, tx, ty);
                    if (tile) {
                        cairo_surface_t* mip_surface = tile_get_mipmap_for_zoom(tile, zoom, NULL);

                        if (mip_surface) {
                            gint mip_w = cairo_image_surface_get_width(mip_surface);
                            gint mip_h = cairo_image_surface_get_height(mip_surface);

                            cairo_save(cr);

                            /* Disable shape anti-aliasing to prevent semi-transparent
                             * pixels at tile boundaries when rendering at fractional
                             * screen coordinates. This eliminates visible gaps/seams. */
                            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

                            /* Move to tile position in document coordinates */
                            cairo_translate(cr, tile->px, tile->py);

                            /* Set source surface at local origin */
                            cairo_set_source_surface(cr, mip_surface, 0, 0);
                            cairo_pattern_t* pattern = cairo_get_source(cr);
                            cairo_pattern_set_filter(pattern, CAIRO_FILTER_BILINEAR);
                            cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);

                            /* Scale the pattern to stretch mipmap to fill tile area.
                             * Pattern matrix transforms LOCAL user coords to pattern coords:
                             * local (0,0) -> pattern (0,0)
                             * local (tile->w, tile->h) -> pattern (mip_w, mip_h) */
                            cairo_matrix_t matrix;
                            gdouble scale_x = (gdouble)mip_w / (gdouble)tile->w;
                            gdouble scale_y = (gdouble)mip_h / (gdouble)tile->h;
                            cairo_matrix_init_scale(&matrix, scale_x, scale_y);
                            cairo_pattern_set_matrix(pattern, &matrix);

                            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

                            /* Draw exact tile rectangle at local origin */
                            cairo_rectangle(cr, 0, 0, tile->w, tile->h);
                            cairo_fill(cr);

                            cairo_restore(cr);
                        }
                    }
                }
            }
        } else {
            /* At 100% zoom, use tiles for performance */
            if (doc->tile_grid) {
                gboolean need_sync_composite = FALSE;

                /* AGGRESSIVE FIX: Draw ALL tiles for the entire document.
                 * This matches what we do for the background (which has no seam).
                 * The clip will limit what's actually rendered, but all tile positions
                 * will have correct content drawn, eliminating the seam. */
                start_tile_x = 0;
                start_tile_y = 0;
                end_tile_x = doc->tile_grid->tiles_x - 1;
                end_tile_y = doc->tile_grid->tiles_y - 1;

                /* Check for dirty visible tiles and composite them SYNCHRONOUSLY.
                 * This prevents flickering during layer movement where stale tile content
                 * would briefly show the layer at its old position before async compositing
                 * completes. The worker pool is used for prefetching off-screen tiles only. */
                for (ty = start_tile_y; ty <= end_tile_y && !need_sync_composite; ty++) {
                    for (tx = start_tile_x; tx <= end_tile_x && !need_sync_composite; tx++) {
                        tile = tile_grid_get_tile(doc->tile_grid, tx, ty);
                        if (tile && tile->dirty) {
                            need_sync_composite = TRUE;
                        }
                    }
                }

                /* Synchronous compositing for visible dirty tiles to prevent flicker */
                if (need_sync_composite) {
                    for (ty = start_tile_y; ty <= end_tile_y; ty++) {
                        for (tx = start_tile_x; tx <= end_tile_x; tx++) {
                            tile = tile_grid_get_tile(doc->tile_grid, tx, ty);
                            if (tile && tile->dirty) {
                                /* Composite pixels - use GPU if enabled, otherwise CPU */
                                if (use_gpu_compositing) {
                                    tile_worker_composite_pixels_gpu(doc, tile, tx, ty);
                                } else {
                                    tile_worker_composite_pixels(doc, tile, tx, ty);
                                }

                                /* Upload pixel buffer to Cairo surface */
                                if (tile->surface) {
                                    cairo_surface_destroy(tile->surface);
                                    tile->surface = NULL;
                                }

                                if (tile->pixel_buffer) {
#if HAVE_LCMS2
                                    if (display_xform) {
                                        cm_apply_transform_argb32(display_xform, (uint8_t*)tile->pixel_buffer,
                                                                  (size_t)(tile->w * tile->h));
                                    }
#endif
                                    tile->surface = cairo_image_surface_create_for_data(
                                        tile->pixel_buffer,
                                        CAIRO_FORMAT_ARGB32,
                                        tile->w,
                                        tile->h,
                                        tile->stride);

                                    if (cairo_surface_status(tile->surface) == CAIRO_STATUS_SUCCESS) {
                                        /* Mark surface as dirty to inform Cairo that pixel data
                                         * was modified externally (by SIMD blending). */
                                        cairo_surface_mark_dirty(tile->surface);
                                        tile->dirty = FALSE;
                                        tile->pending_upload = FALSE;
                                    }
                                }
                            }
                        }
                    }
                }

                /* Enqueue OFF-SCREEN dirty tiles to worker pool for prefetching */
                if (doc->tile_worker_pool) {
                    /* Set viewport center for priority queue */
                    gint viewport_center_x = viewport_x + viewport_w / 2;
                    gint viewport_center_y = viewport_y + viewport_h / 2;
                    tile_worker_pool_set_viewport_center(doc->tile_worker_pool,
                                                         viewport_center_x,
                                                         viewport_center_y);

                    /* Enqueue tiles in a larger region around the viewport for prefetching */
                    gint prefetch_margin = 2; /* tiles */
                    gint pf_start_x = (start_tile_x > prefetch_margin) ? start_tile_x - prefetch_margin : 0;
                    gint pf_start_y = (start_tile_y > prefetch_margin) ? start_tile_y - prefetch_margin : 0;
                    gint pf_end_x = end_tile_x + prefetch_margin;
                    gint pf_end_y = end_tile_y + prefetch_margin;

                    if (pf_end_x >= doc->tile_grid->tiles_x)
                        pf_end_x = doc->tile_grid->tiles_x - 1;
                    if (pf_end_y >= doc->tile_grid->tiles_y)
                        pf_end_y = doc->tile_grid->tiles_y - 1;

                    for (ty = pf_start_y; ty <= pf_end_y; ty++) {
                        for (tx = pf_start_x; tx <= pf_end_x; tx++) {
                            /* Skip visible tiles - already composited synchronously */
                            if (tx >= start_tile_x && tx <= end_tile_x &&
                                ty >= start_tile_y && ty <= end_tile_y) {
                                continue;
                            }

                            tile = tile_grid_get_tile(doc->tile_grid, tx, ty);
                            if (tile && tile->dirty) {
                                tile_worker_pool_enqueue(doc->tile_worker_pool, doc, tile, tx, ty);
                            }
                        }
                    }
                }

/* DEBUG: Set to 1 to draw colored debug rectangles instead of tiles.
                 * This helps identify if the seam is caused by tile content or coordinate issues. */
#define DEBUG_TILE_COLORS 0

                /* Reset clip to allow drawing full tile range (bypasses GTK's partial clip) */
                cairo_reset_clip(cr);
                cairo_rectangle(cr, 0, 0, doc->width, doc->height);
                cairo_clip(cr);

                /* Second pass: Draw all visible tiles (including stale content for pending tiles) */
                for (ty = start_tile_y; ty <= end_tile_y; ty++) {
                    for (tx = start_tile_x; tx <= end_tile_x; tx++) {
                        tile = tile_grid_get_tile(doc->tile_grid, tx, ty);

                        if (tile) {
#if DEBUG_TILE_COLORS
                            /* Debug mode: Draw colored rectangles based on tile position */
                            double r = (double)((tx * 37) % 255) / 255.0;
                            double g = (double)((ty * 73) % 255) / 255.0;
                            double b = (double)(((tx + ty) * 53) % 255) / 255.0;
                            cairo_set_source_rgb(cr, r, g, b);
                            cairo_rectangle(cr, tile->px, tile->py, tile->w, tile->h);
                            cairo_fill(cr);
#else
                            if (tile->surface) {
                                cairo_set_source_surface(cr, tile->surface, tile->px, tile->py);
                                /* Use NEAREST filter to prevent subpixel interpolation at tile edges. */
                                cairo_pattern_t* tile_pattern = cairo_get_source(cr);
                                cairo_pattern_set_filter(tile_pattern, CAIRO_FILTER_NEAREST);
                                /* Use PAD extend mode to prevent edge sampling artifacts */
                                cairo_pattern_set_extend(tile_pattern, CAIRO_EXTEND_PAD);
                                cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                                /* Use explicit rectangle + fill instead of paint() to avoid clip edge artifacts */
                                cairo_rectangle(cr, tile->px, tile->py, tile->w, tile->h);
                                cairo_fill(cr);
                            }
#endif
                        }
                    }
                }
            }
        }

        /* Text layer direct rendering pass (zoom <= 1.0 only).
         *
         * At zoom > 1.0, document_render_layers_at_zoom() already renders text
         * layers inline in the correct Z-order via text_layer_render().
         *
         * At zoom <= 1.0, the tile/mipmap paths use tile_worker_composite_pixels()
         * which intentionally skips LAYER_TYPE_TEXT to prevent stale-position
         * ghosting.  We render every visible text layer here instead, after all
         * tile content is on screen, so the glyphs are always at the position
         * stored in TextLayer.box_x/box_y — even during interactive drag before
         * the tile cache is rebuilt on mouse-up. */
        if (zoom <= 1.0) {
            GList* tl_iter;
            for (tl_iter = doc->layers; tl_iter; tl_iter = tl_iter->next) {
                ImageLayer* tl_layer = (ImageLayer*)tl_iter->data;
                if (!tl_layer || !tl_layer->visible || tl_layer->opacity <= 0.0)
                    continue;
                if (tl_layer->layer_type != LAYER_TYPE_TEXT || !tl_layer->text_data)
                    continue;

                cairo_save(cr);
                cairo_translate(cr, tl_layer->offset_x, tl_layer->offset_y);
                cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

                if (tl_layer->opacity < 1.0) {
                    cairo_push_group(cr);
                    text_layer_render((TextLayer*)tl_layer->text_data, cr);
                    cairo_pop_group_to_source(cr);
                    cairo_paint_with_alpha(cr, tl_layer->opacity);
                } else {
                    text_layer_render((TextLayer*)tl_layer->text_data, cr);
                }

                cairo_restore(cr);
            }
        }

        /* Restore Cairo state after layer rendering (undoes zoom transform) */
        cairo_restore(cr);
    } else {
        /* Draw checkered background for empty canvas */
        draw_checkered_background(cr, clip_width, clip_height);
    }

    /* Display transform is owned by doc->display_xform_cache — no per-draw cleanup needed. */

    /* Draw rect select tool preview during drag */
    tool_rect_select_draw_preview(doc, cr, zoom);

    /* Draw ellipse select tool preview during drag */
    tool_ellipse_select_draw_preview(doc, cr, zoom);

    /* Draw polygon select tool preview */
    tool_polygon_select_draw_preview(doc, cr, zoom);

    /* Draw lasso select tool preview */
    tool_lasso_select_draw_preview(doc, cr, zoom);

    /* Draw magic wand select tool preview */
    tool_magic_wand_select_draw_preview(doc, cr, zoom);

    /* Render selection overlays after all content is drawn */
    if (doc->selection_mask && !selection_mask_is_empty(doc->selection_mask)) {
        selection_mask_render_outline(cr, doc->selection_mask,
                                      doc->selection_animation_phase, zoom, FALSE);
    }

    return FALSE;
}

/**
 * Viewport draw callback - draws overlays on top of everything (including outside canvas)
 */
static gboolean on_viewport_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    GtkAllocation drawing_area_alloc;
    gdouble scroll_x = 0, scroll_y = 0;

    (void)widget; /* Unused */

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Get scroll position from adjustments */
    if (doc->scrolled_window && GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        if (hadj)
            scroll_x = gtk_adjustment_get_value(hadj);
        if (vadj)
            scroll_y = gtk_adjustment_get_value(vadj);
    }

    /* Get drawing area allocation to find its position in viewport */
    gtk_widget_get_allocation(doc->drawing_area, &drawing_area_alloc);

    /* Save cairo state */
    cairo_save(cr);

    /* Translate to drawing area position within viewport, accounting for scroll offset.
     * The drawing_area_alloc gives position relative to bin_window, but we're drawing
     * on the view_window. Subtract scroll offset to get correct view_window coordinates. */
    cairo_translate(cr, drawing_area_alloc.x - scroll_x, drawing_area_alloc.y - scroll_y);

    /* Draw move tool outline overlay (in drawing area coordinates) */
    tool_move_draw_preview(doc, cr, doc->zoom_factor);

    /* Draw crop tool overlay (above canvas, handles not clipped at edges) */
    tool_crop_draw_preview(doc, cr, doc->zoom_factor);

    /* Draw text tool bounding-box overlay */
    tool_text_draw_preview(doc, cr, doc->zoom_factor);

    cairo_restore(cr);

    /* Draw GPU stats overlay if enabled */
    if (doc->drawing_area) {
        /* Get AppContext to check settings */
        gpointer ctx_data = g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
        if (ctx_data) {
            AppContext* ctx = (AppContext*)ctx_data;
            if (ctx->settings && settings_get_show_gpu_stats(ctx->settings)) {
                /* Draw semi-transparent background */
                cairo_save(cr);
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.7);
                cairo_rectangle(cr, 10, 10, 300, 110);
                cairo_fill(cr);

                /* Draw border */
                cairo_set_source_rgba(cr, 0.3, 0.8, 0.3, 0.8);
                cairo_set_line_width(cr, 1.0);
                cairo_rectangle(cr, 10, 10, 300, 110);
                cairo_stroke(cr);

                /* Draw text */
                cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, 12);

                if (doc->gpu_compositor) {
                    guint64 tiles_composited = 0;
                    guint textures_cached = 0;
                    gsize memory_used = 0;

                    gpu_compositor_get_stats(doc->gpu_compositor,
                                             &tiles_composited,
                                             &textures_cached,
                                             &memory_used);

                    const GPUDeviceInfo* gpu_info = gpu_compositor_get_active_device(doc->gpu_compositor);
                    gboolean is_ready = gpu_compositor_is_ready(doc->gpu_compositor);

                    cairo_set_source_rgba(cr, 0.3, 1.0, 0.3, 1.0);

                    gchar* line1 = g_strdup_printf("GPU: %s", gpu_info ? gpu_info->name : "Unknown");
                    gchar* line2 = g_strdup_printf("Status: %s", is_ready ? "Ready" : "Not Ready");
                    gchar* line3 = g_strdup_printf("Tiles Composited: %" G_GUINT64_FORMAT, tiles_composited);
                    gchar* line4 = g_strdup_printf("Textures Cached: %u", textures_cached);
                    gchar* line5 = g_strdup_printf("GPU Memory: %.2f MB", (gdouble)memory_used / (1024.0 * 1024.0));

                    cairo_move_to(cr, 18, 28);
                    cairo_show_text(cr, line1);
                    cairo_move_to(cr, 18, 46);
                    cairo_show_text(cr, line2);
                    cairo_move_to(cr, 18, 64);
                    cairo_show_text(cr, line3);
                    cairo_move_to(cr, 18, 82);
                    cairo_show_text(cr, line4);
                    cairo_move_to(cr, 18, 100);
                    cairo_show_text(cr, line5);

                    g_free(line1);
                    g_free(line2);
                    g_free(line3);
                    g_free(line4);
                    g_free(line5);
                } else {
                    /* GPU compositor is NULL - show why */
                    cairo_set_source_rgba(cr, 1.0, 0.5, 0.3, 1.0); /* Orange for warning */

                    gboolean gpu_available = gpu_compositor_is_available();

                    cairo_move_to(cr, 18, 28);
                    cairo_show_text(cr, "GPU Compositor: NOT INITIALIZED");
                    cairo_move_to(cr, 18, 46);
                    cairo_show_text(cr, gpu_available ? "GLFW/OpenGL: Available" : "GLFW/OpenGL: NOT Available");
                    cairo_move_to(cr, 18, 64);
                    cairo_show_text(cr, settings_get_gpu_acceleration_enabled(ctx->settings)
                                            ? "Setting: Enabled"
                                            : "Setting: DISABLED");
                    cairo_move_to(cr, 18, 82);
                    cairo_show_text(cr, "Check console for errors");
                }

                cairo_restore(cr);
            }
        }
    }

    return FALSE; /* Let other handlers run */
}

/**
 * Convert widget coordinates to image coordinates
 */
static void widget_to_image_coords(ImageDocument* doc, gdouble widget_x, gdouble widget_y,
                                   gint* image_x, gint* image_y) {
    gdouble scaled_x, scaled_y;

    if (!doc || !image_x || !image_y) {
        return;
    }

    /* Unscale by zoom factor */
    scaled_x = widget_x / doc->zoom_factor;
    scaled_y = widget_y / doc->zoom_factor;

    /* Round to nearest integer to prevent pixel shifting */
    *image_x = (gint)(scaled_x + 0.5);
    *image_y = (gint)(scaled_y + 0.5);
}

/**
 * Drawing area button press callback
 */
static gboolean on_drawing_area_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    gpointer ctx_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;

    (void)widget; /* Unused */

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so ignore the event */
    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Get app context from drawing area data and extract tool registry */
    ctx_data = g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (!ctx_data) {
        return FALSE;
    }

    /* Access tool registry through doc if available */
    /* This is a minimal integration - in real code, pass registry more directly */

    /* For now, use a safer approach: store tool_registry directly */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->mouse_down) {
        return FALSE;
    }

    /* Convert to image coordinates */
    widget_to_image_coords(doc, event->x, event->y, &tool_event.x, &tool_event.y);
    tool_event.button      = event->button;
    tool_event.state       = event->state;
    tool_event.click_count = (event->type == GDK_2BUTTON_PRESS) ? 2 : 1;

    /* Call tool handler */
    active_tool->mouse_down(active_tool, doc, &tool_event);

    /* Request redraw */
    gtk_widget_queue_draw(doc->drawing_area);

    return TRUE;
}

/**
 * Drawing area button release callback
 */
static gboolean on_drawing_area_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;

    (void)widget; /* Unused */

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so ignore the event */
    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->mouse_up) {
        return FALSE;
    }

    /* Convert to image coordinates */
    widget_to_image_coords(doc, event->x, event->y, &tool_event.x, &tool_event.y);
    tool_event.button = event->button;
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_up(active_tool, doc, &tool_event);

    /* Request redraw */
    gtk_widget_queue_draw(doc->drawing_area);

    return TRUE;
}

/**
 * Drawing area motion notify callback
 */
static gboolean on_drawing_area_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;
    AppContext* ctx = NULL;

    (void)widget; /* Unused */

    /* Safety check: if document is NULL or drawing_area is NULL,
     * the document is being closed, so ignore the event */
    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Convert to image coordinates */
    widget_to_image_coords(doc, event->x, event->y, &tool_event.x, &tool_event.y);
    tool_event.button = 0; /* No button pressed during motion */
    tool_event.state = event->state;

    /* Update ruler mouse indicator (canvas coordinates); minimal invalidation for marker strips only */
    queue_ruler_marker_areas(doc, doc->prev_mouse_canvas_x, doc->prev_mouse_canvas_y,
                             (gdouble)tool_event.x, (gdouble)tool_event.y);
    doc->prev_mouse_canvas_x = doc->mouse_canvas_x = (gdouble)tool_event.x;
    doc->prev_mouse_canvas_y = doc->mouse_canvas_y = (gdouble)tool_event.y;

    /* Update cursor position in statusbar */
    ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (ctx) {
        ui_update_cursor_position(ctx, doc, tool_event.x, tool_event.y);
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->mouse_move) {
        return FALSE;
    }

    /* Call tool handler */
    active_tool->mouse_move(active_tool, doc, &tool_event);

    /* Request redraw only if the tool draws on the canvas. Color picker only
     * updates the preview widget and does not modify the canvas on move. */
    if (active_tool->type != TOOL_COLOR_PICKER) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Update statusbar selection/crop size during drag/preview */
    if (ctx) {
        ui_update_status_bar_select_size(ctx, doc);
    }

    return TRUE;
}

/**
 * Drawing area enter notify callback - set cursor for active tool
 */
static gboolean on_drawing_area_enter_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    GdkWindow* window;

    (void)event; /* Unused */

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Grab focus for keyboard events */
    gtk_widget_grab_focus(doc->drawing_area);

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || !active_tool->cursor) {
        return FALSE;
    }

    /* Set cursor on drawing area window */
    window = gtk_widget_get_window(doc->drawing_area);
    if (window) {
        gdk_window_set_cursor(window, active_tool->cursor);
    }

    return FALSE;
}

/**
 * Drawing area leave notify callback - no longer used for hiding position
 * (position is now hidden when leaving viewport).
 * Clears color picker preview when cursor leaves canvas and color picker is active.
 */
static gboolean on_drawing_area_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    (void)widget;
    (void)event;

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }
    ToolRegistry* registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!registry) {
        return FALSE;
    }
    Tool* active = tool_manager_get_active(registry);
    if (active && active->type == TOOL_COLOR_PICKER) {
        AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
        if (ctx && ctx->tool_options_panel) {
            tool_options_panel_set_color_picker_preview(ctx->tool_options_panel, FALSE, 0, 0, 0, 0);
        }
        tool_colorpicker_reset_preview_throttle();
    }
    return FALSE;
}

/** Strip width/height for mouse marker invalidation (covers 1px line + rounding) */
#define RULER_MARKER_STRIP 3
#define RULER_SIZE_PX 24

/**
 * Queue only the ruler strips containing the mouse marker (old and new positions).
 * Uses gtk_widget_queue_draw_area to avoid full ruler redraw on mouse move.
 */
static void queue_ruler_marker_areas(ImageDocument* doc, gdouble old_cx, gdouble old_cy, gdouble new_cx, gdouble new_cy) {
    GtkAdjustment* hadj_obj;
    GtkAdjustment* vadj_obj;
    gdouble hadj = 0.0, vadj = 0.0, zoom;
    int w, h, x, y, x1, y1, width, height;
    gboolean old_valid = (old_cx > -1e8 && old_cy > -1e8);
    gboolean new_valid = (new_cx > -1e8 && new_cy > -1e8);

    if (!doc->scrolled_window || !GTK_IS_SCROLLED_WINDOW(doc->scrolled_window))
        return;
    hadj_obj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
    vadj_obj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
    if (!hadj_obj || !vadj_obj)
        return;
    hadj = gtk_adjustment_get_value(hadj_obj);
    vadj = gtk_adjustment_get_value(vadj_obj);
    zoom = document_get_zoom(doc);
    if (zoom <= 0.0)
        zoom = 1.0;

    if (doc->ruler_h && gtk_widget_get_visible(doc->ruler_h)) {
        w = gtk_widget_get_allocated_width(doc->ruler_h);
        h = gtk_widget_get_allocated_height(doc->ruler_h);
        if (old_valid) {
            x = (int)(RULER_SIZE_PX - hadj + old_cx * zoom);
            x1 = x - 1;
            if (x1 < 0)
                x1 = 0;
            width = RULER_MARKER_STRIP;
            if (x1 + width > w)
                width = w - x1;
            if (width > 0)
                gtk_widget_queue_draw_area(doc->ruler_h, x1, 0, width, h);
        }
        if (new_valid) {
            x = (int)(RULER_SIZE_PX - hadj + new_cx * zoom);
            x1 = x - 1;
            if (x1 < 0)
                x1 = 0;
            width = RULER_MARKER_STRIP;
            if (x1 + width > w)
                width = w - x1;
            if (width > 0)
                gtk_widget_queue_draw_area(doc->ruler_h, x1, 0, width, h);
        }
    }
    if (doc->ruler_v && gtk_widget_get_visible(doc->ruler_v)) {
        w = gtk_widget_get_allocated_width(doc->ruler_v);
        h = gtk_widget_get_allocated_height(doc->ruler_v);
        if (old_valid) {
            y = (int)(-vadj + old_cy * zoom);
            y1 = y - 1;
            if (y1 < 0)
                y1 = 0;
            height = RULER_MARKER_STRIP;
            if (y1 + height > h)
                height = h - y1;
            if (height > 0)
                gtk_widget_queue_draw_area(doc->ruler_v, 0, y1, w, height);
        }
        if (new_valid) {
            y = (int)(-vadj + new_cy * zoom);
            y1 = y - 1;
            if (y1 < 0)
                y1 = 0;
            height = RULER_MARKER_STRIP;
            if (y1 + height > h)
                height = h - y1;
            if (height > 0)
                gtk_widget_queue_draw_area(doc->ruler_v, 0, y1, w, height);
        }
    }
}

/**
 * Viewport leave notify callback - hide cursor position
 */
static gboolean on_viewport_leave_notify(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    AppContext* ctx = NULL;

    (void)widget; /* Unused */
    (void)event;  /* Unused */

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    /* Hide cursor position in statusbar */
    ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (ctx) {
        ui_hide_cursor_position(ctx);
    }

    /* Clear ruler mouse indicator; invalidate only the previous marker strips */
    queue_ruler_marker_areas(doc, doc->prev_mouse_canvas_x, doc->prev_mouse_canvas_y, -1e9, -1e9);
    doc->mouse_canvas_x = doc->prev_mouse_canvas_x = -1e9;
    doc->mouse_canvas_y = doc->prev_mouse_canvas_y = -1e9;

    return FALSE;
}

/**
 * Viewport button press callback - for hand tool panning anywhere in viewport
 */
static gboolean on_viewport_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;

    (void)widget; /* Unused */

    /* Safety check */
    if (!doc || !doc->drawing_area || !doc->scrolled_window) {
        return FALSE;
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || active_tool->type != TOOL_HAND || !active_tool->mouse_down) {
        return FALSE; /* Only handle hand tool */
    }

    /* For hand tool, pass viewport coordinates directly */
    /* Viewport coordinates are relative to the viewport widget and are stable */
    tool_event.x = (gint)event->x;
    tool_event.y = (gint)event->y;

    tool_event.button = event->button;
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_down(active_tool, doc, &tool_event);

    return TRUE;
}

/**
 * Viewport button release callback - for hand tool panning
 */
static gboolean on_viewport_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;

    (void)widget; /* Unused */

    /* Safety check */
    if (!doc || !doc->drawing_area || !doc->scrolled_window) {
        return FALSE;
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || active_tool->type != TOOL_HAND || !active_tool->mouse_up) {
        return FALSE; /* Only handle hand tool */
    }

    /* For hand tool, pass viewport coordinates directly */
    /* Viewport coordinates are relative to the viewport widget and are stable */
    tool_event.x = (gint)event->x;
    tool_event.y = (gint)event->y;

    tool_event.button = event->button;
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_up(active_tool, doc, &tool_event);

    return TRUE;
}

/**
 * Viewport motion notify callback - for hand tool panning and cursor position tracking
 */
static gboolean on_viewport_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;
    ToolRegistry* tool_registry = NULL;
    Tool* active_tool = NULL;
    MouseEvent tool_event;
    AppContext* ctx = NULL;
    GtkAllocation drawing_area_alloc;
    gint image_x, image_y;
    gdouble widget_x, widget_y;

    /* Safety check */
    if (!doc || !doc->drawing_area || !doc->scrolled_window) {
        return FALSE;
    }

    /* Get drawing area allocation to find its position in viewport */
    gtk_widget_get_allocation(doc->drawing_area, &drawing_area_alloc);

    /* Convert viewport coordinates to drawing area coordinates
     * The drawing area is centered in the viewport, so we need to account for its offset */
    widget_x = event->x - drawing_area_alloc.x;
    widget_y = event->y - drawing_area_alloc.y;

    /* Convert to image coordinates (can be negative if outside canvas) */
    widget_to_image_coords(doc, widget_x, widget_y, &image_x, &image_y);

    /* Update ruler mouse indicator (canvas coordinates); minimal invalidation for marker strips only */
    queue_ruler_marker_areas(doc, doc->prev_mouse_canvas_x, doc->prev_mouse_canvas_y,
                             (gdouble)image_x, (gdouble)image_y);
    doc->prev_mouse_canvas_x = doc->mouse_canvas_x = (gdouble)image_x;
    doc->prev_mouse_canvas_y = doc->mouse_canvas_y = (gdouble)image_y;

    /* Update cursor position in statusbar */
    ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (ctx) {
        ui_update_cursor_position(ctx, doc, image_x, image_y);
    }

    /* Get tool registry from drawing area data */
    tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    /* Get active tool */
    active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || active_tool->type != TOOL_HAND || !active_tool->mouse_move) {
        return FALSE; /* Only handle hand tool */
    }

    /* For hand tool, pass viewport coordinates directly */
    /* Viewport coordinates are relative to the viewport widget and are stable */
    tool_event.x = (gint)event->x;
    tool_event.y = (gint)event->y;

    tool_event.button = 0; /* No button pressed during motion */
    tool_event.state = event->state;

    /* Call tool handler */
    active_tool->mouse_move(active_tool, doc, &tool_event);

    return TRUE;
}

/**
 * Create a new image document
 */
ImageDocument* document_new(const gchar* filename, gboolean create_worker_pool, guint undo_levels) {
    ImageDocument* doc = (ImageDocument*)g_malloc(sizeof(ImageDocument));

    doc->filename = g_strdup(filename);
    doc->file_path = NULL;
    doc->modified = FALSE;
    doc->drawing_area = NULL;
    doc->scrolled_window = NULL;
    doc->viewport = NULL;
    doc->canvas_container = NULL;
    doc->ruler_unit = RULER_UNIT_PIXEL;
    doc->ruler_dpi = RULER_DPI_DEFAULT;
    doc->ruler_h = NULL;
    doc->ruler_v = NULL;
    doc->mouse_canvas_x = -1e9;
    doc->mouse_canvas_y = -1e9;
    doc->prev_mouse_canvas_x = -1e9;
    doc->prev_mouse_canvas_y = -1e9;

    /* Initialize image metadata */
    doc->width = 0;
    doc->height = 0;
    doc->channels = 0;
    doc->bit_depth = 0;
    doc->has_alpha = FALSE;
    doc->load_icc_profile = NULL;
    doc->original_icc_data = NULL;
    doc->original_icc_size = 0;
    doc->display_xform_cache = NULL;

    /* Initialize rendering pipeline */
    doc->layers = NULL;
    doc->selected_layer = NULL;
    doc->composite_surface = NULL;
    doc->composite_dirty = TRUE;
    dirty_rect_init(&doc->dirty_region);
    doc->dirty_region_list = dirty_region_list_create(); /* Coalescing for optimized tile invalidation */
    doc->tile_grid = NULL;                               /* Will be created when image is loaded */
    doc->tile_thread_pool = NULL;                        /* Will be created when image is loaded */
    doc->zoom_factor = 1.0;
    doc->zoom_mode = 0; /* 0=manual zoom */

    /* Initialize mask-based selection (empty) */
    /* Will be allocated when document dimensions are known */
    doc->selection_mask = NULL;
    doc->selection_animation_phase = 0;
    doc->selection_animation_timer_id = 0; /* Timer ID (0 means not active) */

    /* Create tile worker pool if requested (for on-screen rendering) */
    if (create_worker_pool) {
        doc->tile_worker_pool = tile_worker_pool_create(0);
        if (!doc->tile_worker_pool) {
            g_warning("Failed to create tile worker pool, will use single-threaded compositing");
        }
    } else {
        doc->tile_worker_pool = NULL;
    }

    /* Initialize GPU compositor to NULL - will be created on demand based on settings */
    doc->gpu_compositor = NULL;

    /* Initialize undo/redo stacks with configurable undo levels (0 = unlimited) */
    doc->undo_stack = command_stack_new(undo_levels > 0 ? undo_levels : 0);
    doc->redo_stack = command_stack_new(undo_levels > 0 ? undo_levels : 0);
    doc->undo_journal = NULL; /* Will be created when settings are available */

    return doc;
}

/**
 * Free an image document
 */
void document_free(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    if (doc->filename) {
        g_free(doc->filename);
    }

    if (doc->file_path) {
        g_free(doc->file_path);
    }

    /* Mark document as being freed by setting layers to NULL first
     * This allows command destroy callbacks to detect that the document is being freed */
    GList* layers_to_free = doc->layers;
    doc->layers = NULL;

    /* Free undo journal BEFORE freeing undo stacks */
    if (doc->undo_journal) {
        undo_journal_free(doc->undo_journal);
        doc->undo_journal = NULL;
    }

    /* Free undo/redo stacks BEFORE freeing layers
     * This ensures command destroy callbacks can safely check layer ownership
     * and free any layers they own (e.g., layers in undo state)
     * Note: doc->layers is now NULL, so destroy callbacks know the document is being freed */
    if (doc->undo_stack) {
        command_stack_free(doc->undo_stack);
        doc->undo_stack = NULL;
    }
    if (doc->redo_stack) {
        command_stack_free(doc->redo_stack);
        doc->redo_stack = NULL;
    }

    /* Free all layers */
    for (GList* iter = layers_to_free; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(layers_to_free);

#if HAVE_LCMS2
    if (doc->display_xform_cache) {
        display_xform_cache_free((DisplayTransformCache*)doc->display_xform_cache);
        doc->display_xform_cache = NULL;
    }
#endif

    if (doc->original_icc_data) {
        free(doc->original_icc_data);
        doc->original_icc_data = NULL;
        doc->original_icc_size = 0;
    }

    /* Free composite surface */
    if (doc->composite_surface) {
        cairo_surface_flush(doc->composite_surface);
        cairo_surface_destroy(doc->composite_surface);
        doc->composite_surface = NULL;
    }

    /* Free dirty region list */
    if (doc->dirty_region_list) {
        dirty_region_list_free(doc->dirty_region_list);
        doc->dirty_region_list = NULL;
    }

    /* Free tile grid */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }

    /* Shutdown Cairo-safe worker pool before freeing document */
    if (doc->tile_worker_pool) {
        debug_log("DBG", "Shutting down tile worker pool...");
        tile_worker_pool_destroy(doc->tile_worker_pool);
        doc->tile_worker_pool = NULL;
    }

    /* Shutdown legacy thread pool (if enabled) */
    if (doc->tile_thread_pool) {
        debug_log("DBG", "Shutting down legacy tile thread pool...");
        tile_thread_pool_destroy(doc->tile_thread_pool);
        doc->tile_thread_pool = NULL;
    }

    /* Shutdown GPU compositor if enabled */
    if (doc->gpu_compositor) {
        debug_log("DBG", "Shutting down GPU compositor...");
        gpu_compositor_destroy(doc->gpu_compositor);
        doc->gpu_compositor = NULL;
    }

    /* Remove selection animation timer if it's still running */
    if (doc->selection_animation_timer_id > 0) {
        g_source_remove(doc->selection_animation_timer_id);
        doc->selection_animation_timer_id = 0;
    }

    /* Free selection mask if it exists */
    if (doc->selection_mask) {
        selection_mask_free(doc->selection_mask);
        doc->selection_mask = NULL;
    }

    g_free(doc);
}

/**
 * Key press event handler for modifiers used by rectangular select tool
 */
static gboolean on_drawing_area_key_press(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    ToolRegistry* tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    Tool* active_tool = tool_manager_get_active(tool_registry);

    /* Text tool: dispatch key events when in edit mode.  Must run before the
     * selection-tool filter below so edit-mode keys are not silently dropped. */
    if (active_tool && active_tool->type == TOOL_TEXT && active_tool->key_press) {
        if (active_tool->key_press(active_tool, doc, event))
            return TRUE;
    }

    if (!active_tool || (active_tool->type != TOOL_RECT_SELECT && active_tool->type != TOOL_ELLIPSE_SELECT &&
                         active_tool->type != TOOL_POLYGON_SELECT && active_tool->type != TOOL_LASSO_SELECT &&
                         active_tool->type != TOOL_MAGIC_WAND)) {
        return FALSE;
    }

    /* Enter/Return: finalize magic wand selection */
    if ((event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) &&
        active_tool->type == TOOL_MAGIC_WAND && active_tool->user_data) {
        MagicWandSelectToolState* state = (MagicWandSelectToolState*)active_tool->user_data;
        if (state->has_start_point && state->preview_mask && !state->has_been_finalized) {
            tool_magic_wand_select_finalize(active_tool, doc);
            gtk_widget_queue_draw(doc->drawing_area);
            return TRUE;
        }
    }

    /* ESC: cancel magic wand preview */
    if (event->keyval == GDK_KEY_Escape && active_tool->type == TOOL_MAGIC_WAND && active_tool->user_data) {
        MagicWandSelectToolState* state = (MagicWandSelectToolState*)active_tool->user_data;
        if (state->has_start_point) {
            tool_magic_wand_select_reset(active_tool);
            gtk_widget_queue_draw(doc->drawing_area);
            return TRUE;
        }
    }

    /* ESC: cancel unclosed polygon selection */
    if (event->keyval == GDK_KEY_Escape && active_tool->type == TOOL_POLYGON_SELECT && active_tool->user_data) {
        PolygonSelectToolState* state = (PolygonSelectToolState*)active_tool->user_data;
        if (!state->closed && state->points && state->points->len > 0) {
            tool_polygon_select_reset(active_tool);
            gtk_widget_queue_draw(doc->drawing_area);
            return TRUE;
        }
    }

    /* ESC: cancel in-progress lasso selection */
    if (event->keyval == GDK_KEY_Escape && active_tool->type == TOOL_LASSO_SELECT && active_tool->user_data) {
        LassoSelectToolState* state = (LassoSelectToolState*)active_tool->user_data;
        if (!state->completed && state->points && state->points->len > 0) {
            tool_lasso_select_reset(active_tool);
            gtk_widget_queue_draw(doc->drawing_area);
            return TRUE;
        }
    }

    /* Check if this is a modifier key press (Shift or Alt) */
    gboolean is_shift = (event->keyval == GDK_KEY_Shift_L || event->keyval == GDK_KEY_Shift_R);
    gboolean is_alt = (event->keyval == GDK_KEY_Alt_L || event->keyval == GDK_KEY_Alt_R);

    if (!is_shift && !is_alt) {
        return FALSE;
    }

    /* Now check the new state AFTER this key is pressed */
    /* We need to check what modifiers will be active */
    gboolean shift_will_be_pressed = (event->state & GDK_SHIFT_MASK) != 0 || is_shift;
    gboolean alt_will_be_pressed = (event->state & GDK_MOD1_MASK) != 0 || is_alt;

    SelectionCombineMode temp_mode = SELECTION_COMBINE_NEW;

    /* Determine temporary mode based on modifier state */
    if (shift_will_be_pressed && alt_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_INTERSECT;
    } else if (shift_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_ADD;
    } else if (alt_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_SUBTRACT;
    } else {
        temp_mode = SELECTION_COMBINE_NEW;
    }

    /* Temporarily change the tool options mode */
    if (active_tool->type == TOOL_RECT_SELECT) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
        if (opts) {
            opts->rect_select_combine = temp_mode;
        }
    } else if (active_tool->type == TOOL_ELLIPSE_SELECT) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_ELLIPSE_SELECT);
        if (opts) {
            opts->ellipse_select_combine = temp_mode;
        }
    } else if (active_tool->type == TOOL_POLYGON_SELECT) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_POLYGON_SELECT);
        if (opts) {
            opts->polygon_select_combine = temp_mode;
        }
    } else if (active_tool->type == TOOL_LASSO_SELECT) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_LASSO_SELECT);
        if (opts) {
            opts->lasso_select_combine = temp_mode;
        }
    } else if (active_tool->type == TOOL_MAGIC_WAND) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_MAGIC_WAND);
        if (opts) {
            opts->magicwand_combine = temp_mode;
        }
    }

    /* Update UI buttons to show the temporary mode */
    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (ctx && ctx->tool_options_panel) {
        tool_options_panel_set_combine_mode(ctx->tool_options_panel, temp_mode);
    }

    return FALSE; /* Let GTK handle the event normally */
}

/**
 * Key release event handler for modifiers used by rectangular select tool
 */
static gboolean on_drawing_area_key_release(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
    ImageDocument* doc = (ImageDocument*)user_data;

    if (!doc || !doc->drawing_area) {
        return FALSE;
    }

    ToolRegistry* tool_registry = (ToolRegistry*)g_object_get_data(G_OBJECT(doc->drawing_area), "tool_registry");
    if (!tool_registry) {
        return FALSE;
    }

    Tool* active_tool = tool_manager_get_active(tool_registry);
    if (!active_tool || (active_tool->type != TOOL_RECT_SELECT && active_tool->type != TOOL_ELLIPSE_SELECT &&
                         active_tool->type != TOOL_POLYGON_SELECT && active_tool->type != TOOL_LASSO_SELECT &&
                         active_tool->type != TOOL_MAGIC_WAND)) {
        return FALSE;
    }

    /* Check if this is a modifier key release (Shift or Alt) */
    gboolean is_shift = (event->keyval == GDK_KEY_Shift_L || event->keyval == GDK_KEY_Shift_R);
    gboolean is_alt = (event->keyval == GDK_KEY_Alt_L || event->keyval == GDK_KEY_Alt_R);

    if (!is_shift && !is_alt) {
        return FALSE;
    }

    /* Now check the state AFTER this key is released */
    /* event->state still contains the OLD state (before release), so we need to subtract the released key */
    gboolean shift_will_be_pressed = (event->state & GDK_SHIFT_MASK) != 0 && !is_shift;
    gboolean alt_will_be_pressed = (event->state & GDK_MOD1_MASK) != 0 && !is_alt;

    SelectionCombineMode temp_mode = SELECTION_COMBINE_NEW;

    /* Determine temporary mode based on remaining modifier state */
    if (shift_will_be_pressed && alt_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_INTERSECT;
    } else if (shift_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_ADD;
    } else if (alt_will_be_pressed) {
        temp_mode = SELECTION_COMBINE_SUBTRACT;
    } else {
        temp_mode = SELECTION_COMBINE_NEW;
    }

    /* Restore the original or new combine mode */
    if (active_tool->type == TOOL_RECT_SELECT) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_RECT_SELECT);
        if (opts) {
            opts->rect_select_combine = temp_mode;
        }
    } else if (active_tool->type == TOOL_ELLIPSE_SELECT) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_ELLIPSE_SELECT);
        if (opts) {
            opts->ellipse_select_combine = temp_mode;
        }
    } else if (active_tool->type == TOOL_POLYGON_SELECT) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_POLYGON_SELECT);
        if (opts) {
            opts->polygon_select_combine = temp_mode;
        }
    } else if (active_tool->type == TOOL_LASSO_SELECT) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_LASSO_SELECT);
        if (opts) {
            opts->lasso_select_combine = temp_mode;
        }
    } else if (active_tool->type == TOOL_MAGIC_WAND) {
        ToolOptions* opts = tool_options_get_for_tool(TOOL_MAGIC_WAND);
        if (opts) {
            opts->magicwand_combine = temp_mode;
        }
    }

    /* Update UI buttons to show the new mode */
    AppContext* ctx = (AppContext*)g_object_get_data(G_OBJECT(doc->drawing_area), "app_context");
    if (ctx && ctx->tool_options_panel) {
        tool_options_panel_set_combine_mode(ctx->tool_options_panel, temp_mode);
    }

    return FALSE; /* Let GTK handle the event normally */
}

/**
 * Create a drawing area widget for the document
 */
GtkWidget* document_create_drawing_area(ImageDocument* doc) {
    GtkWidget* grid;
    GtkWidget* h_ruler;
    GtkWidget* v_ruler;
    GtkWidget* scrolled_window;
    GtkWidget* viewport;
    GtkWidget* drawing_area;

    /* Create scrolled window */
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    /* Allow viewport to shrink and center content */
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window),
                                        GTK_SHADOW_NONE);

    /* Create viewport to hold the drawing area */
    viewport = gtk_viewport_new(NULL, NULL);
    gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);

    /* Set app_paintable to take full control of drawing */
    gtk_widget_set_app_paintable(viewport, TRUE);

    /* Set a name for CSS targeting */
    gtk_widget_set_name(viewport, "canvas-viewport");

    /* Store viewport reference in document for later updates */
    doc->viewport = viewport;

    gtk_container_add(GTK_CONTAINER(scrolled_window), viewport);
    gtk_widget_show(viewport);

    /* Create drawing area */
    drawing_area = gtk_drawing_area_new();
    /* Start with default size - will be updated when image loads */
    gtk_widget_set_size_request(drawing_area, 800, 600);

    /* Set app_paintable to take full control of drawing */
    gtk_widget_set_app_paintable(drawing_area, TRUE);

    /* Prevent drawing area from expanding to fill viewport */
    gtk_widget_set_hexpand(drawing_area, FALSE);
    gtk_widget_set_vexpand(drawing_area, FALSE);

    /* IMPORTANT: Use START alignment (top-left) instead of CENTER.
     * Centering causes an offset between the drawing area and viewport origin,
     * which creates a mismatch between clip position and scroll position.
     * This mismatch causes visible seams when GTK's scroll blitting is active.
     * NOTE: setting GTK_ALIGN_START instead of GTK_ALIGN_CENTER breaks centering when 
             zooming out, or using Fit zoom modes. */
    gtk_widget_set_halign(drawing_area, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(drawing_area, GTK_ALIGN_CENTER);

    gtk_container_add(GTK_CONTAINER(viewport), drawing_area);
    gtk_widget_show(drawing_area);

    /* Enable mouse events on drawing area */
    gtk_widget_set_events(drawing_area,
                          gtk_widget_get_events(drawing_area) |
                              GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_BUTTON1_MOTION_MASK |
                              GDK_ENTER_NOTIFY_MASK |
                              GDK_LEAVE_NOTIFY_MASK);

    /* Connect draw signal */
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_drawing_area_draw), doc);

    /* Connect mouse event signals */
    g_signal_connect(drawing_area, "button-press-event",
                     G_CALLBACK(on_drawing_area_button_press), doc);
    g_signal_connect(drawing_area, "button-release-event",
                     G_CALLBACK(on_drawing_area_button_release), doc);
    g_signal_connect(drawing_area, "motion-notify-event",
                     G_CALLBACK(on_drawing_area_motion_notify), doc);
    g_signal_connect(drawing_area, "enter-notify-event",
                     G_CALLBACK(on_drawing_area_enter_notify), doc);
    g_signal_connect(drawing_area, "leave-notify-event",
                     G_CALLBACK(on_drawing_area_leave_notify), doc);

    /* Enable mouse events on viewport for hand tool panning and cursor position tracking */
    gtk_widget_set_events(viewport,
                          gtk_widget_get_events(viewport) |
                              GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_LEAVE_NOTIFY_MASK);

    /* Connect viewport mouse events for hand tool and cursor tracking */
    g_signal_connect(viewport, "button-press-event",
                     G_CALLBACK(on_viewport_button_press), doc);
    g_signal_connect(viewport, "button-release-event",
                     G_CALLBACK(on_viewport_button_release), doc);
    g_signal_connect(viewport, "motion-notify-event",
                     G_CALLBACK(on_viewport_motion_notify), doc);
    g_signal_connect(viewport, "leave-notify-event",
                     G_CALLBACK(on_viewport_leave_notify), doc);

    /* Connect viewport draw signal for overlays (move tool outline, etc.) that extend beyond canvas */
    g_signal_connect_after(viewport, "draw",
                           G_CALLBACK(on_viewport_draw), doc);

    /* Store references in document */
    doc->drawing_area = drawing_area;
    doc->scrolled_window = scrolled_window;

    /* Layout: grid with horizontal ruler full width (0,0), vertical ruler (0,1), scrolled window (1,1) */
    grid = gtk_grid_new();

    h_ruler = canvas_ruler_new(CANVAS_RULER_HORIZONTAL);
    v_ruler = canvas_ruler_new(CANVAS_RULER_VERTICAL);
    doc->ruler_h = h_ruler;
    doc->ruler_v = v_ruler;
    canvas_ruler_set_document(CANVAS_RULER(h_ruler), doc);
    canvas_ruler_set_document(CANVAS_RULER(v_ruler), doc);

    gtk_widget_set_hexpand(scrolled_window, TRUE);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_widget_set_hexpand(h_ruler, TRUE);
    gtk_widget_set_vexpand(h_ruler, FALSE);
    gtk_widget_set_hexpand(v_ruler, FALSE);
    gtk_widget_set_vexpand(v_ruler, TRUE);

    /* Horizontal ruler spans full width so no space on the left */
    gtk_grid_attach(GTK_GRID(grid), h_ruler, 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), v_ruler, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), scrolled_window, 1, 1, 1, 1);

    doc->canvas_container = grid;

    /* Start animation timer for selection marching ants (using standard speed) */
    doc->selection_animation_timer_id = g_timeout_add(ANT_DASH_SPEED_SLOW, on_selection_animation_timer, doc);

    /* Connect to scroll adjustment signals to trigger redraws when scrolling */
    /* Note: Adjustments might be NULL initially, so we'll connect when they're created */
    if (GTK_IS_SCROLLED_WINDOW(scrolled_window)) {
        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolled_window));
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled_window));

        if (hadj) {
            g_signal_connect(hadj, "value-changed",
                             G_CALLBACK(on_scroll_adjustment_changed), doc);
        }
        if (vadj) {
            g_signal_connect(vadj, "value-changed",
                             G_CALLBACK(on_scroll_adjustment_changed), doc);
        }

        /* Also connect to notify signal to catch when adjustments are created */
        g_signal_connect(scrolled_window, "notify::hadjustment",
                         G_CALLBACK(on_scrolled_window_adjustment_notify), doc);
        g_signal_connect(scrolled_window, "notify::vadjustment",
                         G_CALLBACK(on_scrolled_window_adjustment_notify), doc);
    }

    /* Enable key events for modifier key handling (especially for rect select tool hotkeys) */
    /* Note: We need to enable focus on the drawing area to receive key events */
    gtk_widget_set_can_focus(drawing_area, TRUE);
    gtk_widget_set_events(drawing_area,
                          gtk_widget_get_events(drawing_area) |
                              GDK_KEY_PRESS_MASK |
                              GDK_KEY_RELEASE_MASK);

    /* Connect key event signals */
    g_signal_connect(drawing_area, "key-press-event",
                     G_CALLBACK(on_drawing_area_key_press), doc);
    g_signal_connect(drawing_area, "key-release-event",
                     G_CALLBACK(on_drawing_area_key_release), doc);

    gtk_widget_show(scrolled_window);
    gtk_widget_show(h_ruler);
    gtk_widget_show(v_ruler);
    gtk_widget_show(grid);

    return grid;
}

/**
 * Set the document as modified
 */
void document_set_modified(ImageDocument* doc, gboolean modified) {
    if (!doc) {
        return;
    }

    doc->modified = modified;
}

/**
 * Get the document filename
 */
const gchar* document_get_filename(ImageDocument* doc) {
    if (!doc) {
        return NULL;
    }

    return doc->filename;
}

/**
 * Initialize document rendering structures after image dimensions are set
 * This should be called after loading an image
 */
gboolean document_init_rendering_structures(ImageDocument* doc) {
    if (!doc || doc->width == 0 || doc->height == 0) {
        return FALSE;
    }

    /* Free old tile grid if exists */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }

    /* Create tile grid for tile-based rendering
       Tile size of 128 is a good balance between memory and performance */
    doc->tile_grid = tile_grid_create(doc->width, doc->height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid");
        return FALSE;
    }

    /* Create mask-based selection (initially empty) */
    if (doc->selection_mask) {
        selection_mask_free(doc->selection_mask);
    }
    doc->selection_mask = selection_mask_new(doc->width, doc->height);
    if (!doc->selection_mask) {
        g_warning("Failed to create selection mask");
        return FALSE;
    }

    /* Create Cairo-safe tile worker pool for asynchronous rendering if not already created */
    /* Workers composite into pixel buffers only, main thread handles Cairo surfaces */
    if (!doc->tile_worker_pool) {
        doc->tile_worker_pool = tile_worker_pool_create(0);
        if (!doc->tile_worker_pool) {
            g_warning("Failed to create tile worker pool, will use single-threaded compositing");
        } else {
            debug_log("DBG", "Tile compositing: Using worker threads (Cairo-safe pixel buffer approach)");
        }
    }

    /* Initialize GPU compositor if enabled in settings and available */
    if (!doc->gpu_compositor && gpu_compositor_is_available()) {
        /* Get settings from global app settings (if available) */
        gchar* exe_dir = settings_get_executable_dir();
        Settings* app_settings = settings_load(exe_dir);
        g_free(exe_dir);

        if (app_settings && settings_get_gpu_acceleration_enabled(app_settings)) {
            const gchar* gpu_device = settings_get_gpu_device_name(app_settings);
            doc->gpu_compositor = gpu_compositor_create(gpu_device);
            if (doc->gpu_compositor) {
                const GPUDeviceInfo* gpu_info = gpu_compositor_get_active_device(doc->gpu_compositor);
                if (gpu_info) {
                    debug_log("DBG", "GPU acceleration: Enabled (%s)", gpu_info->name);
                }
            } else {
                g_warning("GPU compositor creation failed, falling back to CPU compositing");
            }
        } else {
            debug_log("DBG", "GPU acceleration: Disabled by settings");
        }

        if (app_settings) {
            settings_free(app_settings);
        }
    }

    /* Legacy thread pool (disabled - kept for reference) */
    doc->tile_thread_pool = NULL;

    return TRUE;
}

/**
 * Load an image from file into the document using the plugin system
 */
gboolean document_load_image_from_file(ImageDocument* doc, const gchar* file_path) {
    gboolean result;

    if (!doc || !file_path) {
        return FALSE;
    }

    /* Free old layers if exists */
    for (GList* iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = NULL;

    /* Load image using plugin system */
    result = image_io_load(doc, file_path, NULL, NULL);

    if (!result) {
        return FALSE;
    }

    /* Initialize rendering structures after dimensions are set */
    if (!document_init_rendering_structures(doc)) {
        return FALSE;
    }

    /* Ensure we have at least one layer selected */
    ImageLayer* layer_0 = document_get_layer(doc, 0);
    if (layer_0) {
        document_set_selected_layer(doc, layer_0);
    }

    /* Mark composite as needing re-render */
    document_invalidate_composite(doc);

    /* Update drawing area size to match image dimensions */
    if (doc->drawing_area) {
        /* Set exact size for the drawing area based on image dimensions */
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);

        /* Queue redraw to display the image */
        gtk_widget_queue_draw(doc->drawing_area);
    }

    return TRUE;
}

/**
 * Get image width
 */
guint document_get_width(ImageDocument* doc) {
    if (!doc) {
        return 0;
    }

    return doc->width;
}

/**
 * Get image height
 */
guint document_get_height(ImageDocument* doc) {
    if (!doc) {
        return 0;
    }

    return doc->height;
}

/**
 * Get image metadata string
 */
gchar* document_get_image_info(ImageDocument* doc) {
    if (!doc || doc->width == 0) {
        return g_strdup(_("No image loaded"));
    }

    return g_strdup_printf(_("%ux%u, %d-bit %s%s (zoom: %.0f%%)"),
                           doc->width, doc->height,
                           doc->bit_depth,
                           doc->channels == 3 ? _("RGB") : _("RGBA"),
                           doc->has_alpha ? _(" (with alpha)") : "",
                           doc->zoom_factor * 100.0);
}

/**
 * Set zoom factor for the document
 */
void document_set_zoom(ImageDocument* doc, gdouble zoom_factor) {
    if (!doc) {
        return;
    }

    /* Clamp zoom to reasonable range (1% - 3200%) */
    if (zoom_factor < 0.01) {
        zoom_factor = 0.01;
    } else if (zoom_factor > 32.0) {
        zoom_factor = 32.0;
    }

    doc->zoom_factor = zoom_factor;

    /* Update drawing area and trigger redraw */
    if (doc->drawing_area) {
        gint scaled_width = (gint)(doc->width * zoom_factor);
        gint scaled_height = (gint)(doc->height * zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, scaled_width, scaled_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Rulers adapt tick density to zoom; redraw so spacing updates */
    if (doc->ruler_h && gtk_widget_get_visible(doc->ruler_h))
        gtk_widget_queue_draw(doc->ruler_h);
    if (doc->ruler_v && gtk_widget_get_visible(doc->ruler_v))
        gtk_widget_queue_draw(doc->ruler_v);
}

/**
 * Get current zoom factor
 */
gdouble document_get_zoom(ImageDocument* doc) {
    if (!doc) {
        return 1.0;
    }

    return doc->zoom_factor;
}

void document_set_ruler_unit(ImageDocument* doc, RulerUnit unit) {
    if (!doc)
        return;
    doc->ruler_unit = unit;
    if (doc->ruler_h && gtk_widget_get_visible(doc->ruler_h))
        gtk_widget_queue_draw(doc->ruler_h);
    if (doc->ruler_v && gtk_widget_get_visible(doc->ruler_v))
        gtk_widget_queue_draw(doc->ruler_v);
}

RulerUnit document_get_ruler_unit(ImageDocument* doc) {
    if (!doc)
        return RULER_UNIT_PIXEL;
    return doc->ruler_unit;
}

gdouble document_get_ruler_dpi(ImageDocument* doc) {
    if (!doc || doc->ruler_dpi <= 0.0)
        return RULER_DPI_DEFAULT;
    return doc->ruler_dpi;
}

/**
 * Execute an undo command
 */
gboolean document_undo(ImageDocument* doc) {
    Command* cmd;

    if (!doc || !doc->undo_stack) {
        return FALSE;
    }

    cmd = command_stack_pop(doc->undo_stack);
    if (!cmd) {
        return FALSE;
    }

    /* Execute the undo */
    command_undo(cmd, (struct ImageDocument*)doc);

    /* Push to redo stack */
    command_stack_push(doc->redo_stack, cmd);

    /* Clear journal's redo stack if it exists (since we're creating a new branch) */
    if (doc->undo_journal) {
        undo_journal_clear_redo(doc->undo_journal);
    }

    /* Mark composite for redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Revert command sets modified from snapshot; other undos mark dirty */
    if (cmd->type != COMMAND_DOCUMENT_REVERT) {
        doc->modified = TRUE;
    }

    return TRUE;
}

/**
 * Execute a redo command
 */
gboolean document_redo(ImageDocument* doc) {
    Command* cmd;

    if (!doc || !doc->redo_stack) {
        return FALSE;
    }

    cmd = command_stack_pop(doc->redo_stack);
    if (!cmd) {
        return FALSE;
    }

    /* Execute the redo (apply again) */
    command_execute(cmd, (struct ImageDocument*)doc);

    /* Push back to undo stack */
    command_stack_push(doc->undo_stack, cmd);

    /* Mark composite for redraw */
    if (doc->drawing_area) {
        gtk_widget_queue_draw(doc->drawing_area);
    }

    if (cmd->type != COMMAND_DOCUMENT_REVERT) {
        doc->modified = TRUE;
    }

    return TRUE;
}

/**
 * Check if undo is available
 */
gboolean document_can_undo(ImageDocument* doc) {
    if (!doc || !doc->undo_stack) {
        return FALSE;
    }

    return !command_stack_is_empty(doc->undo_stack);
}

/**
 * Check if redo is available
 */
gboolean document_can_redo(ImageDocument* doc) {
    if (!doc || !doc->redo_stack) {
        return FALSE;
    }

    return !command_stack_is_empty(doc->redo_stack);
}

/**
 * Save document as PNG with alpha channel
 */
gboolean document_save_as_png(ImageDocument* doc, const gchar* filename) {
    cairo_surface_t* composite;
    GdkPixbuf* pixbuf;
    GError* error = NULL;
    gboolean result = FALSE;

    if (!doc || !filename) {
        g_warning("Invalid parameters for document_save_as_png");
        return FALSE;
    }

    /* Get a fresh composite surface for export (includes all layers) */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        g_warning("No composite surface to save");
        return FALSE;
    }

    /* Convert to pixbuf with alpha channel */
    pixbuf = cairo_surface_to_pixbuf(composite, TRUE);
    if (!pixbuf) {
        g_warning("Failed to convert surface to pixbuf");
        return FALSE;
    }

    /* Verify pixbuf has alpha channel (should always be true when keep_alpha=TRUE) */
    if (!gdk_pixbuf_get_has_alpha(pixbuf)) {
        g_warning("Pixbuf does not have alpha channel - this should not happen");
    }

    /* Save as PNG with alpha channel preserved
       gdk_pixbuf_save automatically saves alpha if pixbuf has it */
    result = gdk_pixbuf_save(pixbuf, filename, "png", &error, NULL);

    if (!result) {
        g_warning("Failed to save PNG: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
    } else {
        // printf("Saved PNG: %s\n", filename);
        /* Update document path */
        if (doc->file_path) {
            g_free(doc->file_path);
        }
        doc->file_path = g_strdup(filename);
        doc->modified = FALSE;
    }

    g_object_unref(pixbuf);

    /* Clean up the export surface */
    cairo_surface_destroy(composite);

    return result;
}

/**
 * Save document as JPEG (flattened with white background)
 */
gboolean document_save_as_jpeg(ImageDocument* doc, const gchar* filename, gint quality) {
    cairo_surface_t* composite;
    cairo_surface_t* flattened;
    GdkPixbuf* pixbuf;
    GError* error = NULL;
    gboolean result = FALSE;
    gchar quality_str[4];

    if (!doc || !filename) {
        g_warning("Invalid parameters for document_save_as_jpeg");
        return FALSE;
    }

    /* Clamp quality to valid range */
    if (quality < 0)
        quality = 0;
    if (quality > 100)
        quality = 100;

    /* Get a fresh composite surface for export (includes all layers) */
    composite = document_export_composite_surface(doc);
    if (!composite) {
        g_warning("No composite surface to save");
        return FALSE;
    }

    /* Flatten to white background */
    flattened = compositor_flatten_to_white_background(composite, doc->width, doc->height);

    /* Clean up the export surface */
    cairo_surface_destroy(composite);
    if (!flattened) {
        g_warning("Failed to flatten image");
        return FALSE;
    }

    /* Convert to pixbuf (no alpha) */
    pixbuf = cairo_surface_to_pixbuf(flattened, FALSE);
    cairo_surface_destroy(flattened);

    if (!pixbuf) {
        g_warning("Failed to convert surface to pixbuf");
        return FALSE;
    }

    /* Save as JPEG with quality parameter */
    g_snprintf(quality_str, sizeof(quality_str), "%d", quality);
    result = gdk_pixbuf_save(pixbuf, filename, "jpeg", &error, "quality", quality_str, NULL);

    if (!result) {
        g_warning("Failed to save JPEG: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
    } else {
        // printf("Saved JPEG: %s (quality=%d)\n", filename, quality);
        /* Update document path */
        if (doc->file_path) {
            g_free(doc->file_path);
        }
        doc->file_path = g_strdup(filename);
        doc->modified = FALSE;
    }

    g_object_unref(pixbuf);

    return result;
}

/**
 * Save document with auto-detection by file extension using plugin system
 * @param doc Document to save
 * @param filename Filename to save to
 * @param opts Save options (can be NULL to use defaults)
 */
gboolean document_save_as(ImageDocument* doc, const gchar* filename, const SaveOptions* opts) {
    return document_save_as_with_error(doc, filename, opts, NULL);
}

gboolean document_save_as_with_error(ImageDocument* doc, const gchar* filename, const SaveOptions* opts, PluginError* error_out) {
    SaveOptions default_opts;

    if (!doc || !filename) {
        if (error_out) {
            *error_out = PLUGIN_ERROR_INVALID_PARAMETERS;
        }
        return FALSE;
    }

    /* Use provided options or defaults */
    if (opts) {
        /* Use plugin system to save with provided options */
        /* Note: plugin_data should already be allocated and initialized by caller */
        return image_io_save(doc, filename, opts, error_out);
    }

    /* Set default save options */
    memset(&default_opts, 0, sizeof(SaveOptions));
    default_opts.quality = -1;           /* Use default */
    default_opts.compression_level = -1; /* Use default */
    default_opts.preserve_alpha = doc->has_alpha ? true : false;
    default_opts.flatten_layers = FALSE; /* Keep layers for now */
    default_opts.plugin_data = NULL;

    /* Use plugin system to save */
    return image_io_save(doc, filename, &default_opts, error_out);
}

/**
 * Mark document as saved
 */
void document_mark_saved(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    doc->modified = FALSE;
    // printf("Document marked as saved\n");
}

/**
 * Check if document is dirty
 */
gboolean document_is_dirty(ImageDocument* doc) {
    if (!doc) {
        return FALSE;
    }

    return doc->modified;
}

/* Discrete zoom levels (as integer percentages) */
static const int zoom_levels[] = {
    1, 2, 3, 4, 8, 12, 16, 20, 25, 33, 50, 67, 75, 100,
    150, 200, 300, 400, 500, 600, 700, 800, 1200, 1600, 2400, 3200};
static const int num_zoom_levels = sizeof(zoom_levels) / sizeof(zoom_levels[0]);

/**
 * Find the closest zoom level index for the given zoom factor
 */
static int find_closest_zoom_level(double zoom_factor) {
    int zoom_percent = (int)(zoom_factor * 100.0 + 0.5); /* Round to nearest integer */
    int closest_index = 0;
    int min_diff = abs(zoom_percent - zoom_levels[0]);

    for (int i = 1; i < num_zoom_levels; i++) {
        int diff = abs(zoom_percent - zoom_levels[i]);
        if (diff < min_diff) {
            min_diff = diff;
            closest_index = i;
        }
    }

    return closest_index;
}

gboolean document_zoom_can_zoom_in(ImageDocument* doc) {
    int current_index;
    if (!doc) {
        return FALSE;
    }
    current_index = find_closest_zoom_level(doc->zoom_factor);
    return current_index < num_zoom_levels - 1;
}

gboolean document_zoom_can_zoom_out(ImageDocument* doc) {
    int current_index;
    if (!doc) {
        return FALSE;
    }
    current_index = find_closest_zoom_level(doc->zoom_factor);
    return current_index > 0;
}

/**
 * Zoom in
 */
void document_zoom_in(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    int current_index = find_closest_zoom_level(doc->zoom_factor);

    /* Move to next zoom level if not at maximum */
    if (current_index < num_zoom_levels - 1) {
        current_index++;
        doc->zoom_factor = zoom_levels[current_index] / 100.0;
        doc->zoom_mode = 0; /* Manual zoom */
    }

    document_set_zoom(doc, doc->zoom_factor);
}

/**
 * Zoom out
 */
void document_zoom_out(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    int current_index = find_closest_zoom_level(doc->zoom_factor);

    /* Move to previous zoom level if not at minimum */
    if (current_index > 0) {
        current_index--;
        doc->zoom_factor = zoom_levels[current_index] / 100.0;
        doc->zoom_mode = 0; /* Manual zoom */
    }

    document_set_zoom(doc, doc->zoom_factor);
}

/**
 * Zoom fit (fit to viewport - canvas extends to edges while maintaining aspect ratio)
 */
void document_zoom_fit(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    if (doc->width <= 0 || doc->height <= 0) {
        return;
    }

    /* Get the viewport size (visible area) */
    gint viewport_width = 0;
    gint viewport_height = 0;

    if (doc->viewport && gtk_widget_get_visible(doc->viewport)) {
        /* Get allocated size of viewport (visible area) */
        viewport_width = gtk_widget_get_allocated_width(doc->viewport);
        viewport_height = gtk_widget_get_allocated_height(doc->viewport);
    }

    /* If viewport is not available or not yet allocated, use a default size */
    if (viewport_width <= 0 || viewport_height <= 0) {
        viewport_width = 800;
        viewport_height = 600;
    }

    /* Calculate zoom factors for width and height */
    gdouble zoom_w = (gdouble)viewport_width / (gdouble)doc->width;
    gdouble zoom_h = (gdouble)viewport_height / (gdouble)doc->height;

    /* Use the smaller zoom factor to ensure canvas fits inside viewport
     * while maintaining aspect ratio. This makes the canvas extend to
     * the edges of the viewport in at least one dimension. */
    gdouble zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;

    /* Clamp zoom to reasonable range (10% - 400%) */
    if (zoom < 0.1) {
        zoom = 0.1;
    } else if (zoom > 4.0) {
        zoom = 4.0;
    }

    doc->zoom_factor = zoom;
    doc->zoom_mode = 1; /* Fit image mode */
    document_set_zoom(doc, zoom);
}

/**
 * Fit image to viewport width
 */
void document_zoom_fit_width(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    if (doc->width <= 0) {
        return;
    }

    /* Get the viewport size (visible area) */
    gint viewport_width = 0;

    if (doc->viewport && gtk_widget_get_visible(doc->viewport)) {
        /* Get allocated width of viewport (visible area) */
        viewport_width = gtk_widget_get_allocated_width(doc->viewport);
    }

    /* If viewport is not available or not yet allocated, use a default size */
    if (viewport_width <= 0) {
        viewport_width = 800;
    }

    /* Calculate zoom factor for width */
    gdouble zoom = (gdouble)viewport_width / (gdouble)doc->width;

    /* Clamp zoom to reasonable range (1% - 3200%) */
    if (zoom < 0.01) {
        zoom = 0.01;
    } else if (zoom > 32.0) {
        zoom = 32.0;
    }

    doc->zoom_factor = zoom;
    doc->zoom_mode = 2; /* Fit width mode */
    document_set_zoom(doc, zoom);
}

/**
 * Fit image to viewport height
 */
void document_zoom_fit_height(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    if (doc->height <= 0) {
        return;
    }

    /* Get the viewport size (visible area) */
    gint viewport_height = 0;

    if (doc->viewport && gtk_widget_get_visible(doc->viewport)) {
        /* Get allocated height of viewport (visible area) */
        viewport_height = gtk_widget_get_allocated_height(doc->viewport);
    }

    /* If viewport is not available or not yet allocated, use a default size */
    if (viewport_height <= 0) {
        viewport_height = 600;
    }

    /* Calculate zoom factor for height */
    gdouble zoom = (gdouble)viewport_height / (gdouble)doc->height;

    /* Clamp zoom to reasonable range (1% - 3200%) */
    if (zoom < 0.01) {
        zoom = 0.01;
    } else if (zoom > 32.0) {
        zoom = 32.0;
    }

    doc->zoom_factor = zoom;
    doc->zoom_mode = 3; /* Fit height mode */
    document_set_zoom(doc, zoom);
}

/**
 * Reset zoom to 100%
 */
void document_zoom_reset(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    doc->zoom_factor = 1.0;
    doc->zoom_mode = 0; /* Manual zoom */
    document_set_zoom(doc, 1.0);
}

/**
 * Set zoom to a specific percentage
 */
void document_zoom_to(ImageDocument* doc, gdouble zoom_percent) {
    if (!doc) {
        return;
    }

    /* Convert percentage to zoom factor (e.g., 100.0 -> 1.0, 200.0 -> 2.0) */
    gdouble zoom = zoom_percent / 100.0;

    /* Clamp to reasonable range */
    if (zoom < 0.03125) {
        zoom = 0.03125; /* 3.125% minimum */
    } else if (zoom > 32.0) {
        zoom = 32.0; /* 3200% maximum */
    }

    doc->zoom_factor = zoom;
    doc->zoom_mode = 0; /* Manual zoom */
    document_set_zoom(doc, zoom);
}

/**
 * Resize the canvas
 */
gboolean document_resize_canvas(ImageDocument* doc, guint new_width, guint new_height,
                                gdouble resolution, CanvasAnchorPosition anchor) {
    guint old_width, old_height;
    gint offset_x, offset_y;
    gint delta_width, delta_height;
    GList* iter;
    ImageLayer* layer;
    cairo_surface_t* new_surface;
    cairo_t* cr;

    (void)resolution; /* Reserved for future use */

    if (!doc || new_width == 0 || new_height == 0) {
        return FALSE;
    }

    old_width = doc->width;
    old_height = doc->height;

    /* If dimensions haven't changed, nothing to do */
    if (old_width == new_width && old_height == new_height) {
        return TRUE;
    }

    delta_width = (gint)new_width - (gint)old_width;
    delta_height = (gint)new_height - (gint)old_height;

    /* Calculate offsets based on anchor position */
    switch (anchor) {
        case CANVAS_ANCHOR_TOP_LEFT:
            offset_x = 0;
            offset_y = 0;
            break;
        case CANVAS_ANCHOR_TOP_CENTER:
            offset_x = delta_width / 2;
            offset_y = 0;
            break;
        case CANVAS_ANCHOR_TOP_RIGHT:
            offset_x = delta_width;
            offset_y = 0;
            break;
        case CANVAS_ANCHOR_MIDDLE_LEFT:
            offset_x = 0;
            offset_y = delta_height / 2;
            break;
        case CANVAS_ANCHOR_CENTER:
            offset_x = delta_width / 2;
            offset_y = delta_height / 2;
            break;
        case CANVAS_ANCHOR_MIDDLE_RIGHT:
            offset_x = delta_width;
            offset_y = delta_height / 2;
            break;
        case CANVAS_ANCHOR_BOTTOM_LEFT:
            offset_x = 0;
            offset_y = delta_height;
            break;
        case CANVAS_ANCHOR_BOTTOM_CENTER:
            offset_x = delta_width / 2;
            offset_y = delta_height;
            break;
        case CANVAS_ANCHOR_BOTTOM_RIGHT:
            offset_x = delta_width;
            offset_y = delta_height;
            break;
        case CANVAS_ANCHOR_NONE:
        default:
            offset_x = 0;
            offset_y = 0;
            break;
    }

    /* Adjust layer offsets based on anchor position */
    /* Layers keep their original size, only their position on the canvas changes */
    for (iter = doc->layers; iter; iter = iter->next) {
        layer = (ImageLayer*)iter->data;
        if (!layer) {
            continue;
        }

        /* Update layer offset to position it correctly on the new canvas */
        layer->offset_x += offset_x;
        layer->offset_y += offset_y;

        /* Invalidate layer cache */
        layer_invalidate_cache(layer);
    }

    /* Update document dimensions */
    doc->width = new_width;
    doc->height = new_height;

    /* Update drawing area size */
    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    /* Recreate tile grid with new dimensions */
    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(new_width, new_height, 128);
    if (!doc->tile_grid) {
        g_warning("Failed to create tile grid after canvas resize");
    }

    /* Invalidate composite */
    document_invalidate_composite(doc);

    return TRUE;
}

/* --- Full document content snapshot (revert / undo integration) --- */

void document_clear_content_replace_caches(ImageDocument* doc) {
    if (!doc) {
        return;
    }

    if (doc->gpu_compositor) {
        gpu_compositor_clear_cache(doc->gpu_compositor);
    }

#if HAVE_LCMS2
    if (doc->load_icc_profile) {
        icc_destroy((cmsHPROFILE)doc->load_icc_profile);
        doc->load_icc_profile = NULL;
    }
    if (doc->display_xform_cache) {
        display_xform_cache_free((DisplayTransformCache*)doc->display_xform_cache);
        doc->display_xform_cache = NULL;
    }
#endif
}

struct DocumentContentSnapshot {
    guint width, height, channels, bit_depth;
    gboolean has_alpha;
    gboolean modified_flag;
    void* original_icc_data;
    size_t original_icc_size;
    GList* layers;
    guint selected_layer_index;
    SelectionMask* selection_mask;
};

void document_content_snapshot_free(DocumentContentSnapshot* snap) {
    GList* iter;

    if (!snap) {
        return;
    }

    for (iter = snap->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(snap->layers);

    if (snap->selection_mask) {
        selection_mask_free(snap->selection_mask);
    }

    if (snap->original_icc_data) {
        free(snap->original_icc_data);
    }

    g_free(snap);
}

DocumentContentSnapshot* document_content_snapshot_capture(ImageDocument* doc) {
    DocumentContentSnapshot* snap;
    GList* iter;

    if (!doc || doc->width == 0 || doc->height == 0) {
        return NULL;
    }

    snap = (DocumentContentSnapshot*)g_malloc0(sizeof(DocumentContentSnapshot));
    if (!snap) {
        return NULL;
    }

    snap->width = doc->width;
    snap->height = doc->height;
    snap->channels = doc->channels;
    snap->bit_depth = doc->bit_depth;
    snap->has_alpha = doc->has_alpha;
    snap->modified_flag = doc->modified;

    if (doc->original_icc_data && doc->original_icc_size > 0) {
        snap->original_icc_data = malloc(doc->original_icc_size);
        if (!snap->original_icc_data) {
            g_free(snap);
            return NULL;
        }
        memcpy(snap->original_icc_data, doc->original_icc_data, doc->original_icc_size);
        snap->original_icc_size = doc->original_icc_size;
    }

    for (iter = doc->layers; iter; iter = iter->next) {
        ImageLayer* copy = layer_duplicate_deep((ImageLayer*)iter->data, NULL);
        if (!copy) {
            document_content_snapshot_free(snap);
            return NULL;
        }
        snap->layers = g_list_append(snap->layers, copy);
    }

    if (doc->selected_layer && doc->layers) {
        gint pos = g_list_index(doc->layers, doc->selected_layer);
        snap->selected_layer_index = (pos >= 0) ? (guint)pos : 0;
    } else {
        guint n = g_list_length(doc->layers);
        snap->selected_layer_index = n > 0 ? n - 1 : 0;
    }

    if (doc->selection_mask) {
        snap->selection_mask = selection_mask_duplicate(doc->selection_mask);
        if (!snap->selection_mask) {
            document_content_snapshot_free(snap);
            return NULL;
        }
    } else {
        snap->selection_mask = selection_mask_new((int)doc->width, (int)doc->height);
        if (!snap->selection_mask) {
            document_content_snapshot_free(snap);
            return NULL;
        }
    }

    return snap;
}

gboolean document_content_snapshot_apply(ImageDocument* doc, const DocumentContentSnapshot* snap) {
    GList* iter;
    GList* new_layers = NULL;
    ImageLayer* lyr;
    guint n_layers;
    SelectionMask* new_sel = NULL;

    if (!doc || !snap) {
        return FALSE;
    }

    document_clear_content_replace_caches(doc);

    for (iter = snap->layers; iter; iter = iter->next) {
        lyr = layer_duplicate_deep((ImageLayer*)iter->data, NULL);
        if (!lyr) {
            for (iter = new_layers; iter; iter = iter->next) {
                layer_free((ImageLayer*)iter->data);
            }
            g_list_free(new_layers);
            return FALSE;
        }
        new_layers = g_list_append(new_layers, lyr);
    }

    new_sel = selection_mask_duplicate(snap->selection_mask);
    if (!new_sel) {
        for (iter = new_layers; iter; iter = iter->next) {
            layer_free((ImageLayer*)iter->data);
        }
        g_list_free(new_layers);
        return FALSE;
    }

    for (iter = doc->layers; iter; iter = iter->next) {
        lyr = (ImageLayer*)iter->data;
        if (doc->tile_grid && lyr) {
            tile_grid_invalidate_layer_cache(doc->tile_grid, lyr);
        }
    }

    for (iter = doc->layers; iter; iter = iter->next) {
        layer_free((ImageLayer*)iter->data);
    }
    g_list_free(doc->layers);
    doc->layers = new_layers;

    doc->width = snap->width;
    doc->height = snap->height;
    doc->channels = snap->channels;
    doc->bit_depth = snap->bit_depth;
    doc->has_alpha = snap->has_alpha;

    if (doc->original_icc_data) {
        free(doc->original_icc_data);
        doc->original_icc_data = NULL;
        doc->original_icc_size = 0;
    }
    if (snap->original_icc_data && snap->original_icc_size > 0) {
        doc->original_icc_data = malloc(snap->original_icc_size);
        if (doc->original_icc_data) {
            memcpy(doc->original_icc_data, snap->original_icc_data, snap->original_icc_size);
            doc->original_icc_size = snap->original_icc_size;
        }
    }

    if (doc->selection_mask) {
        selection_mask_free(doc->selection_mask);
    }
    doc->selection_mask = new_sel;

    n_layers = g_list_length(doc->layers);
    if (n_layers > 0 && snap->selected_layer_index < n_layers) {
        doc->selected_layer = (ImageLayer*)g_list_nth_data(doc->layers, snap->selected_layer_index);
    } else if (n_layers > 0) {
        doc->selected_layer = (ImageLayer*)g_list_nth_data(doc->layers, n_layers - 1);
    } else {
        doc->selected_layer = NULL;
    }

    if (doc->composite_surface) {
        cairo_surface_flush(doc->composite_surface);
        cairo_surface_destroy(doc->composite_surface);
        doc->composite_surface = NULL;
    }
    doc->composite_dirty = TRUE;
    dirty_rect_init(&doc->dirty_region);
    if (doc->dirty_region_list) {
        dirty_region_list_clear(doc->dirty_region_list);
    }

    if (doc->tile_grid) {
        tile_grid_free(doc->tile_grid);
        doc->tile_grid = NULL;
    }
    doc->tile_grid = tile_grid_create(doc->width, doc->height, 128);
    if (!doc->tile_grid) {
        g_warning("document_content_snapshot_apply: failed to create tile grid");
        return FALSE;
    }

    for (iter = doc->layers; iter; iter = iter->next) {
        tile_grid_invalidate_layer_cache(doc->tile_grid, (ImageLayer*)iter->data);
    }

    if (doc->undo_journal) {
        undo_journal_clear_all(doc->undo_journal);
    }

    doc->modified = snap->modified_flag;

    document_invalidate_composite(doc);

    if (doc->drawing_area) {
        gint display_width = (gint)(doc->width * doc->zoom_factor);
        gint display_height = (gint)(doc->height * doc->zoom_factor);
        gtk_widget_set_size_request(doc->drawing_area, display_width, display_height);
        gtk_widget_queue_draw(doc->drawing_area);
    }

    if (doc->ruler_h && gtk_widget_get_visible(doc->ruler_h)) {
        gtk_widget_queue_draw(doc->ruler_h);
    }
    if (doc->ruler_v && gtk_widget_get_visible(doc->ruler_v)) {
        gtk_widget_queue_draw(doc->ruler_v);
    }

    return TRUE;
}
