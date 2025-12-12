#include "tool_fill.h"
#include "command.h"
#include "document.h"
#include "ui/tools_panel.h"
#include "render/compositor.h"
#include "render/dirty.h"
#include "render/layer.h"
#include "tool_options.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <glib.h>

/* Forward declarations */
typedef struct AppContext AppContext;
extern void ui_update_menu_and_button_states(AppContext *ctx);
extern void ui_update_window_title(AppContext *ctx);

/**
 * Fill Tool state
 */
typedef struct {
    struct ImageLayer *active_layer; /* Layer being filled */
    Command *current_command;        /* Current fill command for undo */
} PaintBucketToolState;

/**
 * Queue entry for flood fill
 */
typedef struct {
    gint x;
    gint y;
} FloodFillPoint;

/**
 * Calculate color difference (Euclidean distance in RGB space)
 * Returns a value from 0.0 (identical) to ~441.67 (max difference)
 */
static gdouble color_distance(guint8 r1, guint8 g1, guint8 b1, guint8 a1,
                              guint8 r2, guint8 g2, guint8 b2, guint8 a2)
{
    gdouble dr = (gdouble)r1 - (gdouble)r2;
    gdouble dg = (gdouble)g1 - (gdouble)g2;
    gdouble db = (gdouble)b1 - (gdouble)b2;
    gdouble da = (gdouble)a1 - (gdouble)a2;
    
    /* Weight alpha less than RGB for tolerance calculation */
    return sqrt(dr * dr + dg * dg + db * db + da * da * 0.5);
}

/**
 * Check if a pixel matches the target color within tolerance
 */
static gboolean color_matches(guint8 r1, guint8 g1, guint8 b1, guint8 a1,
                             guint8 r2, guint8 g2, guint8 b2, guint8 a2,
                             gfloat tolerance)
{
    /* Convert tolerance from 0-100 range to 0-441.67 range (max color distance) */
    gdouble max_distance = 441.67; /* sqrt(255^2 * 3.5) */
    gdouble threshold = (tolerance / 100.0) * max_distance;
    
    gdouble distance = color_distance(r1, g1, b1, a1, r2, g2, b2, a2);
    return distance <= threshold;
}

/**
 * Calculate blend factor for antialiasing based on color distance
 * Returns 0.0 (outside tolerance) to 1.0 (exact match)
 */
static gdouble calculate_blend_factor(guint8 r1, guint8 g1, guint8 b1, guint8 a1,
                                     guint8 r2, guint8 g2, guint8 b2, guint8 a2,
                                     gfloat tolerance)
{
    gdouble max_distance = 441.67; /* sqrt(255^2 * 3.5) */
    gdouble threshold = (tolerance / 100.0) * max_distance;
    gdouble distance = color_distance(r1, g1, b1, a1, r2, g2, b2, a2);
    
    if (distance > threshold) {
        return 0.0; /* Outside tolerance */
    }
    
    /* Linear blend: 1.0 at exact match, 0.0 at threshold */
    return 1.0 - (distance / threshold);
}

/**
 * Flood fill implementation with tolerance, contiguous/global, and antialiasing options
 */
static void paint_bucket_flood_fill(cairo_surface_t *surface, gint start_x, gint start_y,
                                    gfloat tolerance, gboolean contiguous, gboolean antialiased)
{
    gint width, height, stride;
    guchar *surface_data;
    gboolean *visited;
    GQueue *queue;
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
    guchar *start_pixel = surface_data + start_y * stride + start_x * 4;
    target_b = start_pixel[0];
    target_g = start_pixel[1];
    target_r = start_pixel[2];
    target_a = start_pixel[3];
    
    /* Un-premultiply target color if needed */
    if (target_a > 0 && target_a < 255) {
        target_r = (target_r * 255 + target_a / 2) / target_a;
        target_g = (target_g * 255 + target_a / 2) / target_a;
        target_b = (target_b * 255 + target_a / 2) / target_a;
        if (target_r > 255) target_r = 255;
        if (target_g > 255) target_g = 255;
        if (target_b > 255) target_b = 255;
    }
    
    /* If fill color matches target color, nothing to do */
    if (color_matches(fill_r, fill_g, fill_b, fill_a, target_r, target_g, target_b, target_a, 0.0)) {
        return;
    }
    
    /* Allocate visited array */
    visited = (gboolean *)g_malloc0(width * height * sizeof(gboolean));
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
        g_queue_push_tail(queue, g_memdup(&point, sizeof(FloodFillPoint)));
        visited[start_y * width + start_x] = TRUE;
        
        /* Process queue */
        while (!g_queue_is_empty(queue)) {
            FloodFillPoint *p = (FloodFillPoint *)g_queue_pop_head(queue);
            x = p->x;
            y = p->y;
            g_free(p);
            
            /* Check bounds */
            if (x < 0 || x >= width || y < 0 || y >= height) {
                continue;
            }
            
            /* Read pixel color */
            guchar *pixel = surface_data + y * stride + x * 4;
            guint8 b = pixel[0];
            guint8 g = pixel[1];
            guint8 r = pixel[2];
            guint8 a = pixel[3];
            
            /* Un-premultiply if needed */
            if (a > 0 && a < 255) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
            }
            
            /* Calculate blend factor for antialiasing */
            gdouble blend_factor = 1.0;
            if (antialiased) {
                blend_factor = calculate_blend_factor(r, g, b, a, target_r, target_g, target_b, target_a, tolerance);
                if (blend_factor <= 0.0) {
                    continue; /* Outside tolerance */
                }
            } else {
                /* Check if pixel matches target color within tolerance */
                if (!color_matches(r, g, b, a, target_r, target_g, target_b, target_a, tolerance)) {
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
                    if (i == 0 && j == 0) continue; /* Skip center */
                    
                    gint nx = x + j;
                    gint ny = y + i;
                    
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        if (!visited[ny * width + nx]) {
                            visited[ny * width + nx] = TRUE;
                            point.x = nx;
                            point.y = ny;
                            g_queue_push_tail(queue, g_memdup(&point, sizeof(FloodFillPoint)));
                        }
                    }
                }
            }
        }
    } else {
        /* Global fill: fill all matching pixels in the entire image */
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                guchar *pixel = surface_data + y * stride + x * 4;
                guint8 b = pixel[0];
                guint8 g = pixel[1];
                guint8 r = pixel[2];
                guint8 a = pixel[3];
                
                /* Un-premultiply if needed */
                if (a > 0 && a < 255) {
                    r = (r * 255 + a / 2) / a;
                    g = (g * 255 + a / 2) / a;
                    b = (b * 255 + a / 2) / a;
                    if (r > 255) r = 255;
                    if (g > 255) g = 255;
                    if (b > 255) b = 255;
                }
                
                /* Calculate blend factor for antialiasing */
                gdouble blend_factor = 1.0;
                if (antialiased) {
                    blend_factor = calculate_blend_factor(r, g, b, a, target_r, target_g, target_b, target_a, tolerance);
                    if (blend_factor <= 0.0) {
                        continue; /* Outside tolerance */
                    }
                } else {
                    /* Check if pixel matches target color within tolerance */
                    if (!color_matches(r, g, b, a, target_r, target_g, target_b, target_a, tolerance)) {
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
                
                /* Fill pixel with premultiplied alpha */
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
    
    /* Mark surface as modified */
    cairo_surface_mark_dirty(surface);
}

/**
 * Paint bucket tool: mouse down - perform fill
 */
static void paint_bucket_tool_mouse_down(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    PaintBucketToolState *state;
    struct ImageLayer *active_layer;

    if (!tool || !doc || !doc->layers) {
        return;
    }

    /* Get or create tool state */
    if (!tool->user_data) {
        tool->user_data = g_malloc0(sizeof(PaintBucketToolState));
    }
    state = (PaintBucketToolState *)tool->user_data;

    /* Get the selected layer (from layers panel) */
    active_layer = document_get_selected_layer(doc);
    if (!active_layer || !active_layer->surface) {
        //printf("Fill tool: no selected layer with surface\n");
        return;
    }

    /* Create a draw command for undo/redo */
    state->current_command = command_create_draw(active_layer);

    /* Perform the fill at the clicked position,
       adjusted for layer offset */
    gint layer_x = event->x - active_layer->offset_x;
    gint layer_y = event->y - active_layer->offset_y;
    
    /* Get tool options */
    ToolOptions *opts = tool_options_get_global();
    gfloat tolerance = opts ? opts->tolerance : 10.0f;
    gboolean contiguous = opts ? opts->fill_contiguous : TRUE;
    gboolean antialiased = opts ? opts->fill_antialiased : FALSE;

    paint_bucket_flood_fill(active_layer->surface, layer_x, layer_y, tolerance, contiguous, antialiased);

    /* Finalize draw command by taking snapshot of state after fill */
    if (state->current_command) {
        command_finalize_draw(state->current_command);
    }

    /* Push fill command to undo stack */
    if (state->current_command && doc->undo_stack) {
        command_stack_push(doc->undo_stack, state->current_command);
        //printf("Fill tool: fill command pushed to undo stack\n");

        /* Clear redo stack */
        if (doc->redo_stack) {
            command_stack_clear(doc->redo_stack);
        }

        /* Update UI */
        AppContext *ctx = (AppContext *)tool->app_context;
        if (ctx) {
            ui_update_menu_and_button_states(ctx);
            ui_update_window_title(ctx);
        }
    }

    /* Invalidate layer cache since pixels changed */
    layer_invalidate_cache(active_layer);

    /* Mark document as modified */
    doc->modified = TRUE;

    /* For fill tool, invalidate entire composite (fill can affect large areas) */
    document_invalidate_composite(doc);

    //printf("Fill tool: filled at (%d, %d)\n", layer_x, layer_y);
}

/**
 * Paint bucket tool: mouse move - no-op (fill is instant on click)
 */
static void paint_bucket_tool_mouse_move(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;     /* Unused */
    (void)doc;      /* Unused */
    (void)event;    /* Unused */
    /* Paint bucket tool doesn't do anything on mouse move */
}

/**
 * Fill tool: mouse up - no-op
 */
static void paint_bucket_tool_mouse_up(Tool *tool, struct ImageDocument *doc, MouseEvent *event)
{
    (void)tool;     /* Unused */
    (void)doc;      /* Unused */
    (void)event;    /* Unused */
    /* Fill tool doesn't do anything on mouse up */
}

/**
 * Create the Fill Tool
 */
Tool* tool_fill_create(void)
{
    Tool *tool;

    /* Fill tool doesn't have size/opacity/hardness options yet */
    tool = tool_new("Paint Bucket", TOOL_PAINT_BUCKET, GDK_CROSSHAIR, TOOL_OPT_NONE);
    if (!tool) {
        return NULL;
    }

    tool->mouse_down = paint_bucket_tool_mouse_down;
    tool->mouse_move = paint_bucket_tool_mouse_move;
    tool->mouse_up = paint_bucket_tool_mouse_up;

    //printf("Fill tool created\n");

    return tool;
}

