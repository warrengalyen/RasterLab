#include "command.h"
#include "document.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "render/render_utils.h"
#include "render/tile.h"
#include "selection/selection_mask.h"
#include "selection/selection_render.h"
#include "tool_options.h"
#include "tools/tool_fill.h"
#include "ui/tools_panel.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations */
typedef struct AppContext AppContext;
typedef struct ImageDocument ImageDocument;
extern void ui_update_menu_and_button_states(AppContext* ctx);
extern void ui_update_window_title(AppContext* ctx, ImageDocument* doc);

/**
 * Create a cursor from resource
 */
static GdkCursor* create_paintbucket_cursor(void) {
    GdkDisplay* display;
    GdkPixbuf* pixbuf;
    GdkCursor* cursor;
    GError* error = NULL;
    GBytes* bytes;
    GInputStream* stream;

    display = gdk_display_get_default();
    if (!display) {
        return NULL;
    }

    /* Load cursor file from resource as bytes */
    bytes = g_resources_lookup_data("/cursors/paintbucket_cursor.cur",
                                    G_RESOURCE_LOOKUP_FLAGS_NONE,
                                    &error);
    if (!bytes) {
        if (error) {
            g_warning("Failed to load paintbucket cursor resource: %s", error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    /* Create input stream from bytes */
    stream = g_memory_input_stream_new_from_bytes(bytes);

    /* Load pixbuf from stream */
    pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);

    g_object_unref(stream);
    g_bytes_unref(bytes);

    if (!pixbuf) {
        if (error) {
            g_warning("Failed to parse paintbucket cursor: %s", error->message);
            g_error_free(error);
        }
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    /* Get pixbuf dimensions for hotspot calculation */
    gint width = gdk_pixbuf_get_width(pixbuf);
    gint height = gdk_pixbuf_get_height(pixbuf);

    /* Create cursor from pixbuf with hotspot at center */
    cursor = gdk_cursor_new_from_pixbuf(display, pixbuf, width / 2, height / 2);
    g_object_unref(pixbuf);

    if (!cursor) {
        return gdk_cursor_new_for_display(display, GDK_CROSSHAIR);
    }

    return cursor;
}

/**
 * Fill Tool state
 */
typedef struct {
    struct ImageLayer* active_layer;  /* Layer being filled */
    TileUndoTransaction* transaction; /* Tile-based undo transaction */
} PaintBucketToolState;

/**
 * Queue entry for flood fill
 */
typedef struct {
    gint x;
    gint y;
} FloodFillPoint;

/**
 * Calculate pixel distance using the specified comparison mode.
 * Returns 0.0 for identical pixels; max varies by mode.
 */
static gdouble color_distance_for_mode(guint8 r1, guint8 g1, guint8 b1, guint8 a1,
                                       guint8 r2, guint8 g2, guint8 b2, guint8 a2,
                                       FillCompareMode mode) {
    switch (mode) {
        case FILL_COMPARE_COLOR: {
            gdouble dr = (gdouble)r1 - (gdouble)r2;
            gdouble dg = (gdouble)g1 - (gdouble)g2;
            gdouble db = (gdouble)b1 - (gdouble)b2;
            return sqrt(dr * dr + dg * dg + db * db);
        }
        case FILL_COMPARE_COLOR_AND_OPACITY: {
            gdouble dr = (gdouble)r1 - (gdouble)r2;
            gdouble dg = (gdouble)g1 - (gdouble)g2;
            gdouble db = (gdouble)b1 - (gdouble)b2;
            gdouble da = (gdouble)a1 - (gdouble)a2;
            return sqrt(dr * dr + dg * dg + db * db + da * da * 0.5);
        }
        case FILL_COMPARE_LUMINANCE: {
            gdouble lum1 = 0.2126 * r1 + 0.7152 * g1 + 0.0722 * b1;
            gdouble lum2 = 0.2126 * r2 + 0.7152 * g2 + 0.0722 * b2;
            return fabs(lum1 - lum2);
        }
        case FILL_COMPARE_RED:
            return fabs((gdouble)r1 - (gdouble)r2);
        case FILL_COMPARE_GREEN:
            return fabs((gdouble)g1 - (gdouble)g2);
        case FILL_COMPARE_BLUE:
            return fabs((gdouble)b1 - (gdouble)b2);
        case FILL_COMPARE_ALPHA:
            return fabs((gdouble)a1 - (gdouble)a2);
        default: {
            gdouble dr = (gdouble)r1 - (gdouble)r2;
            gdouble dg = (gdouble)g1 - (gdouble)g2;
            gdouble db = (gdouble)b1 - (gdouble)b2;
            return sqrt(dr * dr + dg * dg + db * db);
        }
    }
}

/**
 * Return the maximum possible distance value for a given comparison mode,
 * used to scale tolerance (0-100) to a meaningful threshold.
 */
static gdouble max_distance_for_mode(FillCompareMode mode) {
    switch (mode) {
        case FILL_COMPARE_COLOR:
            return 441.67; /* sqrt(255^2 * 3) */
        case FILL_COMPARE_COLOR_AND_OPACITY:
            return 477.06; /* sqrt(255^2 * 3.5) */
        case FILL_COMPARE_LUMINANCE:
        case FILL_COMPARE_RED:
        case FILL_COMPARE_GREEN:
        case FILL_COMPARE_BLUE:
        case FILL_COMPARE_ALPHA:
            return 255.0;
        default:
            return 441.67;
    }
}

/**
 * Check if a pixel matches the target color within tolerance using the given mode.
 */
static gboolean color_matches(guint8 r1, guint8 g1, guint8 b1, guint8 a1,
                              guint8 r2, guint8 g2, guint8 b2, guint8 a2,
                              gfloat tolerance, FillCompareMode mode) {
    gdouble max_dist = max_distance_for_mode(mode);
    gdouble threshold = (tolerance / 100.0) * max_dist;
    gdouble distance = color_distance_for_mode(r1, g1, b1, a1, r2, g2, b2, a2, mode);
    return distance <= threshold;
}

/**
 * Calculate blend factor for antialiasing based on pixel distance.
 * Returns 0.0 (outside tolerance) to 1.0 (exact match).
 */
static gdouble calculate_blend_factor(guint8 r1, guint8 g1, guint8 b1, guint8 a1,
                                      guint8 r2, guint8 g2, guint8 b2, guint8 a2,
                                      gfloat tolerance, FillCompareMode mode) {
    gdouble max_dist = max_distance_for_mode(mode);
    gdouble threshold = (tolerance / 100.0) * max_dist;
    gdouble distance = color_distance_for_mode(r1, g1, b1, a1, r2, g2, b2, a2, mode);

    if (distance > threshold) {
        return 0.0;
    }

    /* Linear blend: 1.0 at exact match, 0.0 at threshold */
    return 1.0 - (distance / threshold);
}

/**
 * Flood fill implementation with tolerance, contiguous/global, and antialiasing options
 * Now supports selection masking - only fills pixels inside the active selection
 */
static void paint_bucket_flood_fill(cairo_surface_t* surface, struct ImageDocument* doc,
                                    gint start_x, gint start_y, gint layer_offset_x, gint layer_offset_y,
                                    gfloat tolerance, gboolean contiguous, gboolean antialiased,
                                    FillCompareMode compare_mode) {
    gint width, height, stride;
    guchar* surface_data;
    gboolean* visited;
    GQueue* queue;
    FloodFillPoint point;
    GdkRGBA fill_color;
    guint8 fill_r, fill_g, fill_b, fill_a;
    guint8 target_r, target_g, target_b, target_a;
    gint x, y;
    gint i, j;

    if (!surface) {
        return;
    }

    /* Get surface dimensions */
    width = cairo_image_surface_get_width(surface);
    height = cairo_image_surface_get_height(surface);
    stride = cairo_image_surface_get_stride(surface);

    /* Validate coordinates */
    if (start_x < 0 || start_x >= width || start_y < 0 || start_y >= height) {
        return;
    }

    /* Check if selection exists and get selection mask */
    gboolean has_selection = (doc && doc->selection_mask && !selection_mask_is_empty(doc->selection_mask));
    SelectionMask* full_region_mask = NULL;
    DirtyRect selection_dirty_rect;
    DirtyRect actual_region;
    if (has_selection) {
        /* Calculate the region that might be affected by the fill (in document coordinates) */
        dirty_rect_set(&selection_dirty_rect,
                       layer_offset_x,
                       layer_offset_y,
                       width,
                       height);

        full_region_mask = selection_build_combined_mask(
            doc->selection_mask, &selection_dirty_rect, FEATHER_QUALITY_NORMAL, &actual_region);

        if (!full_region_mask || !full_region_mask->data) {
            has_selection = FALSE;
        }
    }

    /* Flush surface to ensure all drawing operations are complete */
    cairo_surface_flush(surface);
    surface_data = cairo_image_surface_get_data(surface);

    /* Get fill color from foreground color */
    if (!tools_panel_get_foreground_color(&fill_color)) {
        fill_color.red = 0.0;
        fill_color.green = 0.0;
        fill_color.blue = 0.0;
        fill_color.alpha = 1.0;
    }

    /* Convert fill color to 8-bit values */
    fill_r = (guint8)(fill_color.red * 255.0);
    fill_g = (guint8)(fill_color.green * 255.0);
    fill_b = (guint8)(fill_color.blue * 255.0);
    fill_a = (guint8)(fill_color.alpha * 255.0);

    /* Read target color at start position (Cairo ARGB32: BGRA in memory) */
    guchar* start_pixel = surface_data + start_y * stride + start_x * 4;
    target_b = start_pixel[0];
    target_g = start_pixel[1];
    target_r = start_pixel[2];
    target_a = start_pixel[3];

    /* Un-premultiply target color if needed */
    if (target_a > 0 && target_a < 255) {
        target_r = (target_r * 255 + target_a / 2) / target_a;
        target_g = (target_g * 255 + target_a / 2) / target_a;
        target_b = (target_b * 255 + target_a / 2) / target_a;
        if (target_r > 255)
            target_r = 255;
        if (target_g > 255)
            target_g = 255;
        if (target_b > 255)
            target_b = 255;
    }

    /* If fill color matches target color, nothing to do */
    if (color_matches(fill_r, fill_g, fill_b, fill_a, target_r, target_g, target_b, target_a, 0.0,
                      compare_mode)) {
        return;
    }

    /* Allocate visited array */
    visited = (gboolean*)g_malloc0(width * height * sizeof(gboolean));
    if (!visited) {
        g_warning("Paint bucket: Failed to allocate visited array");
        return;
    }

    /* Create queue for flood fill */
    queue = g_queue_new();

    if (contiguous) {
        /* Contiguous fill: flood fill from start point */
        point.x = start_x;
        point.y = start_y;
        g_queue_push_tail(queue, g_memdup2(&point, sizeof(FloodFillPoint)));
        visited[start_y * width + start_x] = TRUE;

        /* Process queue */
        while (!g_queue_is_empty(queue)) {
            FloodFillPoint* p = (FloodFillPoint*)g_queue_pop_head(queue);
            x = p->x;
            y = p->y;
            g_free(p);

            /* Check bounds */
            if (x < 0 || x >= width || y < 0 || y >= height) {
                continue;
            }

            /* Check if pixel is inside selection (if selection exists) */
            if (has_selection && full_region_mask && full_region_mask->data) {
                /* Convert layer coordinates to document coordinates */
                gint doc_x = x + layer_offset_x;
                gint doc_y = y + layer_offset_y;

                /* Calculate mask coordinates relative to actual_region (returned by selection_build_combined_mask) */
                gint mask_x = doc_x - actual_region.x;
                gint mask_y = doc_y - actual_region.y;

                /* Check if pixel is within mask bounds and has non-zero mask value */
                if (mask_x < 0 || mask_x >= full_region_mask->width ||
                    mask_y < 0 || mask_y >= full_region_mask->height) {
                    continue; /* Outside selection mask bounds */
                }

                uint8_t mask_alpha = full_region_mask->data[mask_y * full_region_mask->stride + mask_x];
                if (mask_alpha == 0) {
                    continue; /* Outside selection - skip this pixel */
                }
            }

            /* Read pixel color */
            guchar* pixel = surface_data + y * stride + x * 4;
            guint8 b = pixel[0];
            guint8 g = pixel[1];
            guint8 r = pixel[2];
            guint8 a = pixel[3];

            /* Un-premultiply if needed */
            if (a > 0 && a < 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
                if (r > 255)
                    r = 255;
                if (g > 255)
                    g = 255;
                if (b > 255)
                    b = 255;
            }

            /* Calculate blend factor for antialiasing */
            gdouble blend_factor = 1.0;
            if (antialiased) {
                blend_factor = calculate_blend_factor(r, g, b, a, target_r, target_g, target_b, target_a,
                                                      tolerance, compare_mode);
                if (blend_factor <= 0.0) {
                    continue; /* Outside tolerance */
                }
            } else {
                /* Check if pixel matches target color within tolerance */
                if (!color_matches(r, g, b, a, target_r, target_g, target_b, target_a, tolerance,
                                   compare_mode)) {
                    continue;
                }
            }

            /* Blend fill color with existing pixel color if antialiasing is enabled */
            guint8 final_r, final_g, final_b, final_a;
            if (antialiased && blend_factor < 1.0) {
                /* Blend: result = fill * blend_factor + original * (1 - blend_factor) */
                final_r = (guint8)(fill_r * blend_factor + r * (1.0 - blend_factor));
                final_g = (guint8)(fill_g * blend_factor + g * (1.0 - blend_factor));
                final_b = (guint8)(fill_b * blend_factor + b * (1.0 - blend_factor));
                final_a = (guint8)(fill_a * blend_factor + a * (1.0 - blend_factor));
            } else {
                final_r = fill_r;
                final_g = fill_g;
                final_b = fill_b;
                final_a = fill_a;
            }

            /* Apply selection mask blending (for feathered selections) */
            if (has_selection && full_region_mask && full_region_mask->data) {
                gint doc_x = x + layer_offset_x;
                gint doc_y = y + layer_offset_y;
                gint mask_x = doc_x - actual_region.x;
                gint mask_y = doc_y - actual_region.y;

                if (mask_x >= 0 && mask_x < full_region_mask->width &&
                    mask_y >= 0 && mask_y < full_region_mask->height) {
                    uint8_t mask_alpha = full_region_mask->data[mask_y * full_region_mask->stride + mask_x];

                    if (mask_alpha == 255) {
                        /* Fully inside selection: use fill color as-is */
                        /* final_r, final_g, final_b, final_a already set correctly */
                    } else if (mask_alpha == 0) {
                        /* Outside selection: keep original pixel (shouldn't happen due to earlier check, but handle gracefully) */
                        final_r = r;
                        final_g = g;
                        final_b = b;
                        final_a = a;
                    } else {
                        /* Feather zone: blend fill color with original pixel based on mask alpha */
                        float mask_factor = (float)mask_alpha / 255.0f;
                        float orig_factor = 1.0f - mask_factor;

                        final_r = (guint8)(final_r * mask_factor + r * orig_factor);
                        final_g = (guint8)(final_g * mask_factor + g * orig_factor);
                        final_b = (guint8)(final_b * mask_factor + b * orig_factor);
                        final_a = (guint8)(final_a * mask_factor + a * orig_factor);
                    }
                }
            }

            /* Fill pixel with premultiplied alpha */
            guint8 final_r_pre = (final_r * final_a + 127) / 255;
            guint8 final_g_pre = (final_g * final_a + 127) / 255;
            guint8 final_b_pre = (final_b * final_a + 127) / 255;

            pixel[0] = final_b_pre;
            pixel[1] = final_g_pre;
            pixel[2] = final_r_pre;
            pixel[3] = final_a;

            /* Add neighbors to queue */
            for (i = -1; i <= 1; i++) {
                for (j = -1; j <= 1; j++) {
                    if (i == 0 && j == 0)
                        continue; /* Skip center */

                    gint nx = x + j;
                    gint ny = y + i;

                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        if (!visited[ny * width + nx]) {
                            visited[ny * width + nx] = TRUE;
                            point.x = nx;
                            point.y = ny;
                            g_queue_push_tail(queue, g_memdup2(&point, sizeof(FloodFillPoint)));
                        }
                    }
                }
            }
        }
    } else {
        /* Global fill: fill all matching pixels in the entire image */
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                /* Check if pixel is inside selection (if selection exists) */
                if (has_selection && full_region_mask && full_region_mask->data) {
                    /* Convert layer coordinates to document coordinates */
                    gint doc_x = x + layer_offset_x;
                    gint doc_y = y + layer_offset_y;

                    /* Calculate mask coordinates relative to actual_region */
                    gint mask_x = doc_x - actual_region.x;
                    gint mask_y = doc_y - actual_region.y;

                    /* Check if pixel is within mask bounds and has non-zero mask value */
                    if (mask_x < 0 || mask_x >= full_region_mask->width ||
                        mask_y < 0 || mask_y >= full_region_mask->height) {
                        continue; /* Outside selection mask bounds */
                    }

                    uint8_t mask_alpha = full_region_mask->data[mask_y * full_region_mask->stride + mask_x];
                    if (mask_alpha == 0) {
                        continue; /* Outside selection - skip this pixel */
                    }
                }

                guchar* pixel = surface_data + y * stride + x * 4;
                guint8 b = pixel[0];
                guint8 g = pixel[1];
                guint8 r = pixel[2];
                guint8 a = pixel[3];

                /* Un-premultiply if needed */
                if (a > 0 && a < 255) {
                    r = (r * 255 + a / 2) / a;
                    g = (g * 255 + a / 2) / a;
                    b = (b * 255 + a / 2) / a;
                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;
                }

                /* Calculate blend factor for antialiasing */
                gdouble blend_factor = 1.0;
                if (antialiased) {
                    blend_factor = calculate_blend_factor(r, g, b, a, target_r, target_g, target_b, target_a,
                                                          tolerance, compare_mode);
                    if (blend_factor <= 0.0) {
                        continue; /* Outside tolerance */
                    }
                } else {
                    /* Check if pixel matches target color within tolerance */
                    if (!color_matches(r, g, b, a, target_r, target_g, target_b, target_a, tolerance,
                                       compare_mode)) {
                        continue;
                    }
                }

                /* Blend fill color with existing pixel color if antialiasing is enabled */
                guint8 final_r, final_g, final_b, final_a;
                if (antialiased && blend_factor < 1.0) {
                    /* Blend: result = fill * blend_factor + original * (1 - blend_factor) */
                    final_r = (guint8)(fill_r * blend_factor + r * (1.0 - blend_factor));
                    final_g = (guint8)(fill_g * blend_factor + g * (1.0 - blend_factor));
                    final_b = (guint8)(fill_b * blend_factor + b * (1.0 - blend_factor));
                    final_a = (guint8)(fill_a * blend_factor + a * (1.0 - blend_factor));
                } else {
                    final_r = fill_r;
                    final_g = fill_g;
                    final_b = fill_b;
                    final_a = fill_a;
                }

                /* Apply selection mask to alpha (for feathered selections) */
                if (has_selection && full_region_mask && full_region_mask->data) {
                    gint doc_x = x + layer_offset_x;
                    gint doc_y = y + layer_offset_y;
                    gint mask_x = doc_x - selection_dirty_rect.x;
                    gint mask_y = doc_y - selection_dirty_rect.y;

                    if (mask_x >= 0 && mask_x < full_region_mask->width &&
                        mask_y >= 0 && mask_y < full_region_mask->height) {
                        uint8_t mask_alpha = full_region_mask->data[mask_y * full_region_mask->stride + mask_x];
                        /* Multiply final alpha by mask alpha to respect feathered selection */
                        /* Since we're working in straight alpha space, we can just multiply */
                        final_a = (guint8)((final_a * mask_alpha) / 255);
                    }
                }

                /* Fill pixel with premultiplied alpha */
                /* Note: final_r, final_g, final_b are in straight alpha space, so we premultiply here */
                guint8 final_r_pre = (final_r * final_a + 127) / 255;
                guint8 final_g_pre = (final_g * final_a + 127) / 255;
                guint8 final_b_pre = (final_b * final_a + 127) / 255;

                pixel[0] = final_b_pre;
                pixel[1] = final_g_pre;
                pixel[2] = final_r_pre;
                pixel[3] = final_a;
            }
        }
    }

    /* Clean up */
    g_queue_free_full(queue, g_free);
    g_free(visited);
    if (full_region_mask) {
        selection_mask_free(full_region_mask);
    }

    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);
}

/**
 * Paint bucket tool: mouse down - perform fill
 */
static void paint_bucket_tool_mouse_down(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    PaintBucketToolState* state;
    struct ImageLayer* active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(PaintBucketToolState));
    }
    state = (PaintBucketToolState*)tool->user_data;

    /* Get the selected layer (from layers panel) */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer || !active_layer->surface) {
        // printf("Fill tool: no selected layer with surface\n");
        return;
    }

    /* Begin tile-based undo transaction */
    state->transaction = tile_undo_transaction_begin(active_layer,
                                                     doc,
                                                     command_get_name_string(CMD_NAME_FILL));
    if (!state->transaction) {
        return;
    }

    /* Register all tiles in the layer before fill (fill can affect entire layer) */
    gint tile_size = doc->tile_grid ? doc->tile_grid->tile_size : 128;
    gint tiles_x = (active_layer->width + tile_size - 1) / tile_size;
    gint tiles_y = (active_layer->height + tile_size - 1) / tile_size;

    for (gint ty = 0; ty < tiles_y; ty++) {
        for (gint tx = 0; tx < tiles_x; tx++) {
            gint sample_x = tx * tile_size + tile_size / 2;
            gint sample_y = ty * tile_size + tile_size / 2;
            tile_undo_transaction_register_tile(state->transaction, doc, sample_x, sample_y);
        }
    }

    /* Perform the fill at the clicked position,
       adjusted for layer offset */
    gint layer_x = event->x - active_layer->offset_x;
    gint layer_y = event->y - active_layer->offset_y;

    /* Get tool options */
    ToolOptions* opts = tool_options_get_for_tool(tool->type);
    gfloat tolerance = opts ? opts->tolerance : 10.0f;
    gboolean contiguous = opts ? opts->fill_contiguous : TRUE;
    gboolean antialiased = opts ? opts->fill_antialiased : FALSE;
    FillCompareMode compare_mode = opts ? opts->fill_compare_mode : FILL_COMPARE_COLOR;

    paint_bucket_flood_fill(active_layer->surface, doc, layer_x, layer_y,
                            active_layer->offset_x, active_layer->offset_y,
                            tolerance, contiguous, antialiased, compare_mode);

    /* Commit tile-based undo transaction (captures "after" state and creates command) */
    Command* cmd = NULL;
    if (state->transaction) {
        cmd = tile_undo_transaction_commit(state->transaction);
        state->transaction = NULL;
    }

    /* Push command to undo stack */
    if (cmd && doc->undo_stack) {
        command_stack_push(doc->undo_stack, cmd);

        /* Clear redo stack */
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }

        /* Update UI */
        AppContext* ctx = (AppContext*)tool->app_context;
        if (ctx) {
            ui_update_menu_and_button_states(ctx);
            ui_update_window_title(ctx, NULL);
        }
    } else if (cmd) {
        /* No undo stack, free the command */
        command_free(cmd);
    }

    /* Invalidate layer cache since pixels changed */
    layer_invalidate_cache(active_layer);

    /* Mark document as modified */
    doc->modified = TRUE;

    /* For fill tool, invalidate entire composite (fill can affect large areas) */
    document_invalidate_composite(doc);

    // printf("Fill tool: filled at (%d, %d)\n", layer_x, layer_y);
}

/**
 * Paint bucket tool: mouse move - no-op (fill is instant on click)
 */
static void paint_bucket_tool_mouse_move(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    (void)tool;  /* Unused */
    (void)doc;   /* Unused */
    (void)event; /* Unused */
    /* Paint bucket tool doesn't do anything on mouse move */
}

/**
 * Fill tool: mouse up - no-op
 */
static void paint_bucket_tool_mouse_up(Tool* tool, struct ImageDocument* doc, MouseEvent* event) {
    (void)tool;  /* Unused */
    (void)doc;   /* Unused */
    (void)event; /* Unused */
    /* Fill tool doesn't do anything on mouse up */
}

/**
 * Create the Fill Tool
 */
Tool* tool_fill_create(void) {
    Tool* tool;

    /* Fill tool doesn't have size/opacity/hardness options yet */
    tool = tool_new("Paint Bucket", TOOL_PAINT_BUCKET, GDK_CROSSHAIR, TOOL_OPT_NONE);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = paint_bucket_tool_mouse_down;
    tool->mouse_move = paint_bucket_tool_mouse_move;
    tool->mouse_up = paint_bucket_tool_mouse_up;

    /* Replace cursor with custom paintbucket cursor */
    if (tool->cursor) {
        g_object_unref(tool->cursor);
    }
    tool->cursor = create_paintbucket_cursor();

    // printf("Fill tool created\n");

    return tool;
}
