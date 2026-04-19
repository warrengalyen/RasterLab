/*
 * This file is part of RasterLab
 * Copyright (c) 2025-2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

/*
 * Gradient Editor dialog  (Tools > Developer > Gradient Editor)
 * Uses layout from resources/ui/gradient_editor_dialog.glade
 *
 * Collection tab  – scrollable list of gradient swatches read from
 *                   <app_dir>/gradients/  (.ggr and .grd files)
 * Editor tab      – large preview bar + detail info for the selected gradient
 */

#include "ui/dialogs/gradient_editor_dialog.h"
#include "debug_logger.h"
#include "gradient.h"
#include "io/gradient_io.h"
#include "ui/dialogs/color_chooser_dialog.h"
#include "ui/ui_utils.h"
#include "ui/widgets/vertical_spin_button.h"
#include <cairo.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal data structures
 * ---------------------------------------------------------------------- */

/* One entry in the loaded gradient collection */
typedef struct {
    GradientSet* set; /* owns the GradientDef array */
    gchar* filepath;  /* full path to the source file (owned) */
    gchar* basename;  /* display name: filename without extension (owned) */
} GradEntry;

/* Per-row widget data attached to the GtkDrawingArea inside each list row */
typedef struct {
    GradientDef* def; /* borrowed – lives inside GradEntry.set */
    gchar* name;      /* gradient name (borrowed from def->name) */
    gchar* source;    /* source file basename (borrowed from GradEntry.basename) */
} RowData;

/* One colour stop derived from the GradientDef's segment array */
typedef struct {
    double pos;          /* [0,1] normalised gradient position  */
    double r, g, b;      /* stop colour                         */
    gboolean can_remove; /* FALSE for the first and last stops  */
} NodeStop;

/* One transparency stop derived from GradientDef.transparency_stops */
typedef struct {
    double pos;     /* [0,1] gradient position */
    double opacity; /* [0,1] opacity (1.0 = fully opaque) */
} TransStop;

#define NODE_HALF    6   /* half-width of node body (pixels either side of centre) */
#define NODE_CAP_H   7   /* height of the triangular cap at the top of each node   */
#define NODE_RECT_H  14  /* height of the coloured rectangle body of each node     */
/* NODE_BAR_PAD must be >= NODE_HALF + 2 so the node outline (0.5 px each side)
 * clears the bar's own 1 px border (inner edge at x=1, right edge at x=w-1). */
#define NODE_BAR_PAD 8   /* margin each side: keeps end-nodes inside the border    */
/* Derived bar height: border (1) + cap (NODE_CAP_H) + rect (NODE_RECT_H) + border (1) */
#define NODE_BAR_H   (1 + NODE_CAP_H + NODE_RECT_H + 1)

/* Dialog-level state stored as GObject data on the dialog widget */
typedef struct {
    GPtrArray* entries;     /* owns GradEntry* elements */
    GradientDef* selected;  /* currently selected gradient (borrowed) */
    gchar* selected_source; /* currently selected source basename (borrowed) */

    /* Glade widgets we need to update on selection change */
    GtkWidget* notebook;
    GtkWidget* editor_preview;
    GtkWidget* node_bar;   /* GtkDrawingArea for colour stop handles */
    GtkWidget* trans_bar;         /* GtkDrawingArea for transparency stop handles */
    GtkWidget* collection_list;  /* GtkListBox in the Collection tab */
    GtkWidget* name_label;
    GtkWidget* file_label;

    /* Node options section */
    GtkWidget*          node_options_separator; /* shown only when a node is selected */
    GtkWidget*          node_options_label;     /* "node options:" heading */
    GtkWidget*          node_options_box;       /* outer row of option columns */
    GtkWidget*          node_color_box;         /* colour column (hidden for trans stops) */
    GtkWidget*          node_color_btn;         /* colour swatch button */
    GtkAdjustment*      opacity_adj;            /* shared by opacity scale + spin */
    GtkAdjustment*      location_adj;           /* shared by location scale + spin */
    VerticalSpinButton* opacity_spin;           /* spin next to opacity scale */
    VerticalSpinButton* location_spin;          /* spin next to location scale */
    gboolean            updating_node_ui;       /* suppress feedback loops */

    /* Colour stop bar state */
    int selected_node; /* index into collect_nodes result; -1 = none */
    int hover_node;    /* node the pointer is currently over; -1 = none */
    int hover_x;       /* mouse x when hovering over empty bar space; -1 = none */
    gboolean dragging; /* TRUE while a node is being dragged */
    int drag_node_idx; /* which node is being dragged; -1 = none */

    /* Transparency stop bar state */
    int      selected_trans_node; /* index into collect_trans_nodes; -1 = none */
    int      hover_trans_node;    /* hovered transparency node index; -1 = none */
    int      hover_trans_x;       /* hover x for empty space indicator; -1 = none */
    gboolean trans_dragging;      /* TRUE while dragging a transparency node */
    int      drag_trans_idx;      /* dragging transparency node index; -1 = none */
} DialogData;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static void grad_entry_free(gpointer p) {
    GradEntry* e = (GradEntry*)p;
    if (!e)
        return;
    if (e->set) {
        gradient_set_free(e->set);
    }
    g_free(e->filepath);
    g_free(e->basename);
    g_free(e);
}

static void dialog_data_free(gpointer p) {
    DialogData* d = (DialogData*)p;
    if (!d)
        return;
    if (d->entries) {
        g_ptr_array_unref(d->entries);
    }
    g_free(d);
}

/* -------------------------------------------------------------------------
 * Node bar helpers
 * ---------------------------------------------------------------------- */

/*
 * Build an array of NodeStop from a GradientDef's segment endpoints.
 * Stops are: segments[0].left, then segments[i].right for every i.
 * Caller must free with g_array_free(arr, TRUE).
 */
static GArray* collect_nodes(const GradientDef* def) {
    if (!def || def->num_segments < 1)
        return NULL;
    GArray* arr = g_array_new(FALSE, FALSE, sizeof(NodeStop));
    int n = def->num_segments;
    for (int i = 0; i < n; i++) {
        const GradientSegment* s = &def->segments[i];
        if (i == 0) {
            NodeStop ns = {s->left_pos, s->left_r, s->left_g, s->left_b, FALSE};
            g_array_append_val(arr, ns);
        }
        NodeStop ns = {s->right_pos, s->right_r, s->right_g, s->right_b, (i < n - 1)};
        g_array_append_val(arr, ns);
    }
    return arr;
}

/* Map a [0,1] gradient position to a pixel x within the node bar,
 * leaving NODE_BAR_PAD pixels of margin on each side. */
static int pos_to_x(double pos, int bar_w) {
    return NODE_BAR_PAD + (int)(pos * (double)(bar_w - 2 * NODE_BAR_PAD) + 0.5);
}

/* Inverse of pos_to_x – clamps result to [0,1]. */
static double x_to_pos(int x, int bar_w) {
    double range = (double)(bar_w - 2 * NODE_BAR_PAD);
    if (range <= 0.0)
        return 0.0;
    double raw = (double)(x - NODE_BAR_PAD) / range;
    return (raw < 0.0) ? 0.0 : (raw > 1.0) ? 1.0
                                           : raw;
}

/* Return the index of the node closest to pixel x in a bar of width w,
 * or -1 if none is within (NODE_HALF + 2) pixels. */
static int find_node_at_x(GArray* nodes, int x, int w) {
    if (!nodes || w < 2)
        return -1;
    for (guint i = 0; i < nodes->len; i++) {
        NodeStop* ns = &g_array_index(nodes, NodeStop, i);
        int nx = pos_to_x(ns->pos, w);
        if (abs(x - nx) <= NODE_HALF + 2)
            return (int)i;
    }
    return -1;
}

/* Slide any stop to new_pos, clamped between its neighbours (or [0,1] for endpoints). */
/* Flat representation of one gradient colour stop used during reordering. */
typedef struct {
    double pos;
    double r, g, b, a;
} ColorStopRec;

/* Move colour stop *stop_idx_ptr to new_pos.
 * When the stop crosses a neighbour the underlying segments are reordered so
 * positions always stay sorted, and *stop_idx_ptr is updated to the stop's
 * new index.  Only the [0,1] range is enforced; no neighbour clamping. */
static void node_bar_move_stop(GradientDef* def, int* stop_idx_ptr, double new_pos) {
    if (!def || def->num_segments < 1) return;
    int N   = def->num_segments;
    int idx = *stop_idx_ptr;

    if (new_pos < 0.0) new_pos = 0.0;
    if (new_pos > 1.0) new_pos = 1.0;

    /* Extract all N+1 boundary stops into a flat array */
    ColorStopRec* stops =
        (ColorStopRec*)malloc((size_t)(N + 1) * sizeof(ColorStopRec));
    if (!stops) return;

    stops[0].pos = def->segments[0].left_pos;
    stops[0].r   = def->segments[0].left_r;
    stops[0].g   = def->segments[0].left_g;
    stops[0].b   = def->segments[0].left_b;
    stops[0].a   = def->segments[0].left_a;
    for (int i = 1; i < N; i++) {
        stops[i].pos = def->segments[i - 1].right_pos;
        stops[i].r   = def->segments[i - 1].right_r;
        stops[i].g   = def->segments[i - 1].right_g;
        stops[i].b   = def->segments[i - 1].right_b;
        stops[i].a   = def->segments[i - 1].right_a;
    }
    stops[N].pos = def->segments[N - 1].right_pos;
    stops[N].r   = def->segments[N - 1].right_r;
    stops[N].g   = def->segments[N - 1].right_g;
    stops[N].b   = def->segments[N - 1].right_b;
    stops[N].a   = def->segments[N - 1].right_a;

    /* Save dragged stop's colour for later identification */
    double drag_r = stops[idx].r, drag_g = stops[idx].g;
    double drag_b = stops[idx].b, drag_a = stops[idx].a;

    /* Apply new position to the dragged stop */
    stops[idx].pos = new_pos;

    /* Insertion-sort all stops by position (stable, preserves relative order
     * of equal-position stops so the dragged stop ends up at a predictable
     * index when colours are identical). */
    for (int i = 1; i <= N; i++) {
        ColorStopRec key = stops[i];
        int j = i - 1;
        while (j >= 0 && stops[j].pos > key.pos) {
            stops[j + 1] = stops[j];
            j--;
        }
        stops[j + 1] = key;
    }

    /* Locate the new index of the dragged stop (first match at new_pos with
     * the same colour; handles the common case without ambiguity). */
    int new_idx = idx;
    for (int i = 0; i <= N; i++) {
        if (fabs(stops[i].pos - new_pos) < 1e-10 &&
            stops[i].r == drag_r && stops[i].g == drag_g &&
            stops[i].b == drag_b && stops[i].a == drag_a) {
            new_idx = i;
            break;
        }
    }

    /* Rebuild segments from the sorted stops */
    for (int i = 0; i < N; i++) {
        def->segments[i].left_pos  = stops[i].pos;
        def->segments[i].left_r    = stops[i].r;
        def->segments[i].left_g    = stops[i].g;
        def->segments[i].left_b    = stops[i].b;
        def->segments[i].left_a    = stops[i].a;
        def->segments[i].right_pos = stops[i + 1].pos;
        def->segments[i].right_r   = stops[i + 1].r;
        def->segments[i].right_g   = stops[i + 1].g;
        def->segments[i].right_b   = stops[i + 1].b;
        def->segments[i].right_a   = stops[i + 1].a;
        def->segments[i].midpoint  =
            stops[i].pos + 0.5 * (stops[i + 1].pos - stops[i].pos);
    }

    free(stops);
    *stop_idx_ptr = new_idx;
    gradient_invalidate(def);
}

/* Split the segment containing pos, inserting a new colour stop.
 * Colour at the new stop is linearly interpolated. */
static void node_bar_add_stop(GradientDef* def, double pos) {
    if (!def || def->num_segments == 0)
        return;

    /* Find the segment that contains pos */
    int idx = def->num_segments - 1;
    for (int i = 0; i < def->num_segments; i++) {
        if (pos <= def->segments[i].right_pos) {
            idx = i;
            break;
        }
    }

    /* Save the original segment before any realloc */
    GradientSegment orig = def->segments[idx];
    double span = orig.right_pos - orig.left_pos;
    double t = (span > 1e-9) ? (pos - orig.left_pos) / span : 0.5;

    double nr = orig.left_r + t * (orig.right_r - orig.left_r);
    double ng = orig.left_g + t * (orig.right_g - orig.left_g);
    double nb = orig.left_b + t * (orig.right_b - orig.left_b);
    double na = orig.left_a + t * (orig.right_a - orig.left_a);

    /* Grow the segments array by one slot */
    int new_n = def->num_segments + 1;
    GradientSegment* ns = (GradientSegment*)realloc(def->segments,
                                                    (size_t)new_n * sizeof(GradientSegment));
    if (!ns)
        return;
    def->segments = ns;

    /* Shift segments after idx up to make room */
    if (idx + 1 < def->num_segments) {
        memmove(&def->segments[idx + 2], &def->segments[idx + 1],
                (size_t)(def->num_segments - idx - 1) * sizeof(GradientSegment));
    }

    /* Left half [orig.left_pos .. pos] */
    GradientSegment left_seg = orig;
    left_seg.right_pos = pos;
    left_seg.right_r = nr;
    left_seg.right_g = ng;
    left_seg.right_b = nb;
    left_seg.right_a = na;
    left_seg.right_type = GRADIENT_ENDPOINT_FIXED;
    left_seg.midpoint = orig.left_pos + 0.5 * (pos - orig.left_pos);
    def->segments[idx] = left_seg;

    /* Right half [pos .. orig.right_pos] */
    GradientSegment right_seg = orig;
    right_seg.left_pos = pos;
    right_seg.left_r = nr;
    right_seg.left_g = ng;
    right_seg.left_b = nb;
    right_seg.left_a = na;
    right_seg.left_type = GRADIENT_ENDPOINT_FIXED;
    right_seg.midpoint = pos + 0.5 * (orig.right_pos - pos);
    def->segments[idx + 1] = right_seg;

    def->num_segments = new_n;
    gradient_invalidate(def);
}

/* Merge the two segments that share the boundary at stop_idx,
 * effectively removing that colour stop.
 * stop_idx 0 and (total-1) are the endpoints and cannot be removed. */
static void node_bar_remove_stop(GradientDef* def, int stop_idx, int total_stops) {
    if (!def || stop_idx <= 0 || stop_idx >= total_stops - 1)
        return;
    if (def->num_segments < 2)
        return;

    int li = stop_idx - 1; /* left segment index */
    GradientSegment* L = &def->segments[li];
    GradientSegment* R = &def->segments[li + 1];

    /* Extend L to cover R's right endpoint */
    L->right_pos = R->right_pos;
    L->right_r = R->right_r;
    L->right_g = R->right_g;
    L->right_b = R->right_b;
    L->right_a = R->right_a;
    L->right_type = R->right_type;
    L->midpoint = L->left_pos + 0.5 * (L->right_pos - L->left_pos);

    /* Remove segment at li+1 by shifting remaining segments down */
    int n = def->num_segments;
    if (li + 2 < n) {
        memmove(&def->segments[li + 1], &def->segments[li + 2],
                (size_t)(n - li - 2) * sizeof(GradientSegment));
    }
    def->num_segments--;
    gradient_invalidate(def);
}

/* -------------------------------------------------------------------------
 * Transparency stop bar helpers
 * ---------------------------------------------------------------------- */

static GArray* collect_trans_nodes(const GradientDef* def) {
    if (!def || def->num_transparency_stops < 1) return NULL;
    GArray* arr = g_array_new(FALSE, FALSE, sizeof(TransStop));
    for (int i = 0; i < def->num_transparency_stops; i++) {
        TransStop ts = { def->transparency_stops[i].position,
                         def->transparency_stops[i].opacity };
        g_array_append_val(arr, ts);
    }
    return arr;
}

static int find_trans_node_at_x(GArray* nodes, int x, int w) {
    if (!nodes || w < 2) return -1;
    for (guint i = 0; i < nodes->len; i++) {
        TransStop* ts = &g_array_index(nodes, TransStop, i);
        int nx = pos_to_x(ts->pos, w);
        if (abs(x - nx) <= NODE_HALF + 2) return (int)i;
    }
    return -1;
}

/* Move a transparency stop; all stops can be moved, clamped by neighbours. */
/* Move transparency stop *idx_ptr to new_pos, reordering adjacent stops when
 * crossed.  *idx_ptr is updated to the stop's new index. */
static void trans_bar_move_stop(GradientDef* def, int* idx_ptr, double new_pos) {
    if (!def || !def->transparency_stops || def->num_transparency_stops < 1) return;
    int idx = *idx_ptr;
    int N   = def->num_transparency_stops;

    if (new_pos < 0.0) new_pos = 0.0;
    if (new_pos > 1.0) new_pos = 1.0;

    def->transparency_stops[idx].position = new_pos;

    /* Bubble left while out of order */
    while (idx > 0 &&
           def->transparency_stops[idx].position <
               def->transparency_stops[idx - 1].position) {
        GradientTransparencyStop tmp    = def->transparency_stops[idx];
        def->transparency_stops[idx]   = def->transparency_stops[idx - 1];
        def->transparency_stops[idx-1] = tmp;
        idx--;
    }
    /* Bubble right while out of order */
    while (idx < N - 1 &&
           def->transparency_stops[idx].position >
               def->transparency_stops[idx + 1].position) {
        GradientTransparencyStop tmp    = def->transparency_stops[idx];
        def->transparency_stops[idx]   = def->transparency_stops[idx + 1];
        def->transparency_stops[idx+1] = tmp;
        idx++;
    }

    *idx_ptr = idx;
    gradient_invalidate(def);
}

/* Insert a new transparency stop at pos, interpolating opacity from neighbours.
 * No-op when the gradient has no transparency_stops array (e.g. GGR). */
static void trans_bar_add_stop(GradientDef* def, double pos) {
    if (!def || !def->transparency_stops) return;
    int N   = def->num_transparency_stops;

    /* Find sorted insert position */
    int ins = N;
    for (int i = 0; i < N; i++) {
        if (pos < def->transparency_stops[i].position) { ins = i; break; }
    }

    /* Interpolate opacity from neighbours */
    double opacity = 1.0;
    if (N > 0) {
        if (ins == 0) {
            opacity = def->transparency_stops[0].opacity;
        } else if (ins == N) {
            opacity = def->transparency_stops[N - 1].opacity;
        } else {
            GradientTransparencyStop* L = &def->transparency_stops[ins - 1];
            GradientTransparencyStop* R = &def->transparency_stops[ins];
            double span = R->position - L->position;
            double t    = (span > 1e-9) ? (pos - L->position) / span : 0.5;
            opacity = L->opacity + t * (R->opacity - L->opacity);
        }
    }

    /* Grow the stops array */
    GradientTransparencyStop* ns = (GradientTransparencyStop*)realloc(
        def->transparency_stops, (size_t)(N + 1) * sizeof(GradientTransparencyStop));
    if (!ns) return;
    def->transparency_stops = ns;

    /* Shift right and insert */
    if (ins < N) {
        memmove(&def->transparency_stops[ins + 1], &def->transparency_stops[ins],
                (size_t)(N - ins) * sizeof(GradientTransparencyStop));
    }
    def->transparency_stops[ins].position = pos;
    def->transparency_stops[ins].opacity  = opacity;
    def->transparency_stops[ins].midpoint = pos;
    def->num_transparency_stops = N + 1;
    gradient_invalidate(def);
}

/* Remove an interior transparency stop; keeps at least 2 stops. */
static void trans_bar_remove_stop(GradientDef* def, int idx) {
    if (!def || def->num_transparency_stops <= 2) return;
    if (idx <= 0 || idx >= def->num_transparency_stops - 1) return;
    int N = def->num_transparency_stops;
    memmove(&def->transparency_stops[idx], &def->transparency_stops[idx + 1],
            (size_t)(N - idx - 1) * sizeof(GradientTransparencyStop));
    def->num_transparency_stops--;
    gradient_invalidate(def);
}

static void get_node_color(const GradientDef* def, int idx,
                            double* r, double* g, double* b) {
    int N = def->num_segments;
    if (idx <= 0) {
        *r = def->segments[0].left_r;
        *g = def->segments[0].left_g;
        *b = def->segments[0].left_b;
    } else if (idx >= N) {
        *r = def->segments[N - 1].right_r;
        *g = def->segments[N - 1].right_g;
        *b = def->segments[N - 1].right_b;
    } else {
        *r = def->segments[idx - 1].right_r;
        *g = def->segments[idx - 1].right_g;
        *b = def->segments[idx - 1].right_b;
    }
}

static void set_node_color(GradientDef* def, int idx,
                            double r, double g, double b) {
    int N = def->num_segments;
    if (idx <= 0) {
        def->segments[0].left_r = r;
        def->segments[0].left_g = g;
        def->segments[0].left_b = b;
    } else if (idx >= N) {
        def->segments[N - 1].right_r = r;
        def->segments[N - 1].right_g = g;
        def->segments[N - 1].right_b = b;
    } else {
        def->segments[idx - 1].right_r = r;
        def->segments[idx - 1].right_g = g;
        def->segments[idx - 1].right_b = b;
        def->segments[idx].left_r = r;
        def->segments[idx].left_g = g;
        def->segments[idx].left_b = b;
    }
    gradient_invalidate(def);
}

static double get_node_opacity(const GradientDef* def, int idx) {
    int N = def->num_segments;
    if (idx <= 0)  return def->segments[0].left_a;
    if (idx >= N)  return def->segments[N - 1].right_a;
    return def->segments[idx - 1].right_a;
}

static void set_node_opacity(GradientDef* def, int idx, double opacity) {
    int N = def->num_segments;
    if (idx <= 0) {
        def->segments[0].left_a = opacity;
    } else if (idx >= N) {
        def->segments[N - 1].right_a = opacity;
    } else {
        def->segments[idx - 1].right_a = opacity;
        def->segments[idx].left_a = opacity;
    }
    gradient_invalidate(def);
}

/* Refresh all node-option widgets to match the current selection.
 * Safe to call with no selection – hides the entire options section. */
static void update_node_options_ui(DialogData* dd) {
    if (!dd) return;

    gboolean has_color = (dd->selected != NULL && dd->selected_node >= 0);
    gboolean has_trans = (dd->selected != NULL && dd->selected_trans_node >= 0);
    gboolean has_any   = (has_color || has_trans);

    if (dd->node_options_separator)
        gtk_widget_set_visible(dd->node_options_separator, has_any);
    /* Label is always visible: shows a hint when nothing is selected */
    if (dd->node_options_label) {
        gtk_widget_set_visible(dd->node_options_label, TRUE);
        gtk_label_set_text(GTK_LABEL(dd->node_options_label),
                           has_any ? "node options:" : "Please select a node");
    }
    if (dd->node_options_box)
        gtk_widget_set_visible(dd->node_options_box, has_any);
    if (dd->node_color_box)
        gtk_widget_set_visible(dd->node_color_box, has_color);

    if (!has_any) return;

    dd->updating_node_ui = TRUE;

    double opacity  = 1.0;
    double location = 0.0;

    if (has_color) {
        double r, g, b;
        get_node_color(dd->selected, dd->selected_node, &r, &g, &b);
        opacity = get_node_opacity(dd->selected, dd->selected_node);

        GArray* nodes = collect_nodes(dd->selected);
        if (nodes) {
            if (dd->selected_node < (int)nodes->len) {
                NodeStop* ns = &g_array_index(nodes, NodeStop, dd->selected_node);
                location = ns->pos;
            }
            g_array_free(nodes, TRUE);
        }

        if (dd->node_color_btn) {
            GdkRGBA rgba = {(float)r, (float)g, (float)b, 1.0f};
            update_color_button_appearance(dd->node_color_btn, &rgba);
        }
    } else {
        GArray* nodes = collect_trans_nodes(dd->selected);
        if (nodes) {
            if (dd->selected_trans_node < (int)nodes->len) {
                TransStop* ts = &g_array_index(nodes, TransStop, dd->selected_trans_node);
                opacity  = ts->opacity;
                location = ts->pos;
            }
            g_array_free(nodes, TRUE);
        }
    }

    if (dd->opacity_adj)
        gtk_adjustment_set_value(dd->opacity_adj, opacity * 100.0);
    if (dd->location_adj)
        gtk_adjustment_set_value(dd->location_adj, location * 100.0);

    dd->updating_node_ui = FALSE;
}

/* -------------------------------------------------------------------------
 * Checkerboard + gradient preview rendering
 * ---------------------------------------------------------------------- */

/* Draw a grey checkerboard to represent transparency regions */
static void draw_checkerboard(cairo_t* cr, int w, int h) {
    const int sz = 8;
    for (int y = 0; y < h; y += sz) {
        for (int x = 0; x < w; x += sz) {
            double shade = ((x / sz + y / sz) % 2) ? 0.73 : 0.90;
            cairo_set_source_rgb(cr, shade, shade, shade);
            cairo_rectangle(cr, x, y,
                            MIN(sz, w - x),
                            MIN(sz, h - y));
            cairo_fill(cr);
        }
    }
}

/*
 * Convert an RGBA8 straight-alpha pixel buffer (from GradientPreview) into
 * a Cairo ARGB32 premultiplied surface and paint it scaled to (w x h).
 */
static void paint_gradient_preview(cairo_t* cr, const GradientPreview* pv, int w, int h) {
    if (!pv || !pv->pixels || pv->width < 1 || pv->height < 1) {
        return;
    }

    int pw = pv->width;
    int ph = pv->height;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pw);

    guchar* buf = (guchar*)g_malloc((gsize)stride * (gsize)ph);
    if (!buf)
        return;

    /* Convert RGBA8 straight → ARGB32 premultiplied (native byte order) */
    const uint8_t* src = pv->pixels;
    for (int row = 0; row < ph; row++) {
        guint32* dst_row = (guint32*)(buf + (gsize)row * (gsize)stride);
        for (int col = 0; col < pw; col++) {
            uint8_t r = src[(row * pw + col) * 4 + 0];
            uint8_t g = src[(row * pw + col) * 4 + 1];
            uint8_t b = src[(row * pw + col) * 4 + 2];
            uint8_t a = src[(row * pw + col) * 4 + 3];
            if (a == 255) {
                dst_row[col] = (0xFFu << 24) | ((guint32)r << 16) | ((guint32)g << 8) | b;
            } else if (a == 0) {
                dst_row[col] = 0;
            } else {
                uint8_t pr = (uint8_t)(((unsigned)r * a + 127u) / 255u);
                uint8_t pg = (uint8_t)(((unsigned)g * a + 127u) / 255u);
                uint8_t pb = (uint8_t)(((unsigned)b * a + 127u) / 255u);
                dst_row[col] = ((guint32)a << 24) | ((guint32)pr << 16) | ((guint32)pg << 8) | pb;
            }
        }
    }

    cairo_surface_t* surf = cairo_image_surface_create_for_data(
        buf, CAIRO_FORMAT_ARGB32, pw, ph, stride);

    cairo_save(cr);
    cairo_scale(cr, (double)w / (double)pw, (double)h / (double)ph);
    cairo_set_source_surface(cr, surf, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_surface_destroy(surf);
    g_free(buf);
}

/* -------------------------------------------------------------------------
 * GtkDrawingArea draw callbacks
 * ---------------------------------------------------------------------- */

/* Draw callback for the small swatch inside each list row */
static gboolean on_row_swatch_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    RowData* rd = (RowData*)user_data;
    if (!rd || !rd->def)
        return FALSE;

    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    if (w < 1 || h < 1)
        return FALSE;

    draw_checkerboard(cr, w, h);

    const GradientPreview* pv = gradient_preview_get(rd->def);
    if (pv) {
        paint_gradient_preview(cr, pv, w, h);
    }
    return TRUE;
}

/* Draw callback for the large preview bar in the Editor tab */
static gboolean on_editor_preview_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    DialogData* dd = (DialogData*)user_data;
    if (!dd || !dd->selected)
        return FALSE;

    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    if (w < 1 || h < 1)
        return FALSE;

    draw_checkerboard(cr, w, h);

    const GradientPreview* pv = gradient_preview_get(dd->selected);
    if (pv) {
        paint_gradient_preview(cr, pv, w, h);
    }
    return TRUE;
}

/* -------------------------------------------------------------------------
 * Selection handler
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Node bar draw + input callbacks
 * ---------------------------------------------------------------------- */

/*
 * Draw a single house-shaped node handle:
 *   - Triangular cap  (top)  : white when normal, blue when highlighted
 *   - Rectangular body (bottom): filled with the stop colour
 *   - Thin black outline around the whole pentagon
 *
 * The node is centred on pixel x and spans the full interior of the bar
 * (1 px padding top and bottom).
 */
static void draw_node_handle(cairo_t* cr, int x, int bar_h,
                             double r, double g, double b,
                             gboolean highlighted) {
    double pad = ((double)bar_h - (double)(NODE_CAP_H + NODE_RECT_H)) * 0.5;
    if (pad < 1.0)
        pad = 1.0;
    double tip_y = pad;
    double base_y = tip_y + (double)NODE_CAP_H;
    double bot_y = base_y + (double)NODE_RECT_H;
    double xl = (double)(x - NODE_HALF);
    double xr = (double)(x + NODE_HALF);

    /* --- Rectangle body (stop colour) ---------------------------------- */
    cairo_rectangle(cr, xl, base_y, xr - xl, bot_y - base_y);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_fill(cr);

    /* --- Triangle cap (white / selection-blue) ------------------------- */
    cairo_move_to(cr, (double)x, tip_y);
    cairo_line_to(cr, xl, base_y);
    cairo_line_to(cr, xr, base_y);
    cairo_close_path(cr);
    if (highlighted) {
        cairo_set_source_rgb(cr, 0.2, 0.5, 1.0);
    } else {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    }
    cairo_fill(cr);

    /* --- Pentagon outline ---------------------------------------------- */
    cairo_move_to(cr, (double)x, tip_y);
    cairo_line_to(cr, xl, base_y);
    cairo_line_to(cr, xl, bot_y);
    cairo_line_to(cr, xr, bot_y);
    cairo_line_to(cr, xr, base_y);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.85);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* --- Triangle cap bottom border (cap/body divider) ----------------- */
    cairo_move_to(cr, xl, base_y);
    cairo_line_to(cr, xr, base_y);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.85);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
}

static gboolean on_node_bar_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    DialogData* dd = (DialogData*)user_data;

    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    /* White background */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    if (dd && dd->selected && w >= 2) {
        GArray* nodes = collect_nodes(dd->selected);
        if (nodes) {
            for (guint i = 0; i < nodes->len; i++) {
                NodeStop* ns = &g_array_index(nodes, NodeStop, i);
                int x = pos_to_x(ns->pos, w);
                gboolean active = (dd->selected_node == (int)i || dd->hover_node == (int)i);
                draw_node_handle(cr, x, h, ns->r, ns->g, ns->b, active);
            }
            g_array_free(nodes, TRUE);
        }
    }

    /* Blue position indicator: shown when pointer is in empty bar space */
    if (dd && dd->selected && dd->hover_node < 0 && dd->hover_x >= 0 && !dd->dragging) {
        double r  = (double)h * 0.25;  /* ~half bar height diameter */
        double cy = (double)h * 0.5;
        cairo_save(cr);
        cairo_rectangle(cr, 1.0, 1.0, (double)(w - 2), (double)(h - 2));
        cairo_clip(cr);
        cairo_arc(cr, (double)dd->hover_x, cy, r, 0.0, 2.0 * M_PI);
        cairo_set_source_rgba(cr, 0.2, 0.5, 1.0, 0.85);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    /* 1-px border */
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0.5, 0.5, (double)w - 1.0, (double)h - 1.0);
    cairo_stroke(cr);

    return TRUE;
}

static gboolean on_node_bar_button_press(GtkWidget* widget,
                                         GdkEventButton* event,
                                         gpointer user_data) {
    DialogData* dd = (DialogData*)user_data;
    if (!dd || !dd->selected)
        return FALSE;

    int w = gtk_widget_get_allocated_width(widget);
    if (w < 2)
        return FALSE;

    int click_x = (int)event->x;
    double pos = x_to_pos(click_x, w);

    GArray* nodes = collect_nodes(dd->selected);
    if (!nodes)
        return FALSE;
    int node_idx = find_node_at_x(nodes, click_x, w);
    int total = (int)nodes->len;
    g_array_free(nodes, TRUE);

    /* Clicking the colour bar deselects any transparency-stop selection */
    dd->selected_trans_node = -1;

    if (event->button == GDK_BUTTON_PRIMARY) {
        if (node_idx >= 0) {
            /* Select the node and start dragging it */
            dd->selected_node = node_idx;
            dd->dragging = TRUE;
            dd->drag_node_idx = node_idx;
        } else {
            /* Add new stop at this position */
            node_bar_add_stop(dd->selected, pos);
            GArray* updated = collect_nodes(dd->selected);
            if (updated) {
                dd->selected_node = find_node_at_x(updated, click_x, w);
                g_array_free(updated, TRUE);
            }
            gtk_widget_queue_draw(dd->editor_preview);
        }
    } else if (event->button == GDK_BUTTON_SECONDARY) {
        if (node_idx > 0 && node_idx < total - 1) {
            /* Remove interior stop */
            node_bar_remove_stop(dd->selected, node_idx, total);
            if (dd->selected_node >= (int)(total - 1))
                dd->selected_node = (int)(total - 2);
            gtk_widget_queue_draw(dd->editor_preview);
        }
    }

    update_node_options_ui(dd);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean on_node_bar_motion(GtkWidget* widget,
                                   GdkEventMotion* event,
                                   gpointer user_data) {
    DialogData* dd = (DialogData*)user_data;
    if (!dd || !dd->selected)
        return FALSE;

    int w = gtk_widget_get_allocated_width(widget);

    if (dd->dragging && dd->drag_node_idx >= 0) {
        double new_pos = x_to_pos((int)event->x, w);
        node_bar_move_stop(dd->selected, &dd->drag_node_idx, new_pos);
        dd->selected_node = dd->drag_node_idx; /* follow reordering */
        /* Sync location spin without triggering the adj callback */
        if (dd->location_adj) {
            dd->updating_node_ui = TRUE;
            gtk_adjustment_set_value(dd->location_adj, new_pos * 100.0);
            dd->updating_node_ui = FALSE;
        }
        gtk_widget_queue_draw(dd->editor_preview);
        gtk_widget_queue_draw(widget);
    } else {
        GArray* nodes = collect_nodes(dd->selected);
        int idx = nodes ? find_node_at_x(nodes, (int)event->x, w) : -1;
        if (nodes)
            g_array_free(nodes, TRUE);

        /* hover_x is set only when the pointer is not over any node */
        int new_hover_x = (idx < 0) ? (int)event->x : -1;
        if (idx != dd->hover_node || new_hover_x != dd->hover_x) {
            dd->hover_node = idx;
            dd->hover_x    = new_hover_x;
            gtk_widget_queue_draw(widget);
        }
    }
    return FALSE;
}

static gboolean on_node_bar_leave(GtkWidget* widget,
                                  GdkEventCrossing* event,
                                  gpointer user_data) {
    (void)event;
    DialogData* dd = (DialogData*)user_data;
    if (dd && (dd->hover_node != -1 || dd->hover_x != -1)) {
        dd->hover_node = -1;
        dd->hover_x    = -1;
        gtk_widget_queue_draw(widget);
    }
    return FALSE;
}

static void on_node_bar_realize(GtkWidget* widget, gpointer user_data) {
    (void)user_data;
    GdkCursor* cursor = gdk_cursor_new_for_display(
        gtk_widget_get_display(widget), GDK_HAND2);
    gdk_window_set_cursor(gtk_widget_get_window(widget), cursor);
    g_object_unref(cursor);
}

static gboolean on_node_bar_button_release(GtkWidget* widget,
                                           GdkEventButton* event,
                                           gpointer user_data) {
    (void)widget;
    (void)event;
    DialogData* dd = (DialogData*)user_data;
    if (dd && dd->dragging) {
        dd->dragging = FALSE;
        dd->drag_node_idx = -1;
    }
    return FALSE;
}

/* -------------------------------------------------------------------------
 * Transparency stop bar  – draw + input callbacks
 * ---------------------------------------------------------------------- */

/*
 * Draw a single inverted house-shaped node for a transparency stop:
 *   - Rectangular body (top): filled with grayscale = opacity (white=1, black=0)
 *   - Triangular cap (bottom): tip points DOWN; white when normal, blue when highlighted
 *   - Thin outline and cap/body divider line
 */
static void draw_trans_node_handle(cairo_t* cr, int x, int bar_h,
                                   double opacity, gboolean highlighted) {
    double pad    = ((double)bar_h - (double)(NODE_CAP_H + NODE_RECT_H)) * 0.5;
    if (pad < 1.0) pad = 1.0;
    double top_y  = pad;
    double base_y = top_y + (double)NODE_RECT_H;
    double tip_y  = base_y + (double)NODE_CAP_H;
    double xl     = (double)(x - NODE_HALF);
    double xr     = (double)(x + NODE_HALF);

    /* --- Rectangle body (grayscale opacity) ----------------------------- */
    cairo_rectangle(cr, xl, top_y, xr - xl, base_y - top_y);
    cairo_set_source_rgb(cr, opacity, opacity, opacity);
    cairo_fill(cr);

    /* --- Triangle cap (tip points DOWN) --------------------------------- */
    cairo_move_to(cr, xl,        base_y);
    cairo_line_to(cr, xr,        base_y);
    cairo_line_to(cr, (double)x, tip_y);
    cairo_close_path(cr);
    if (highlighted)
        cairo_set_source_rgb(cr, 0.2, 0.5, 1.0);
    else
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill(cr);

    /* --- Pentagon outline ---------------------------------------------- */
    cairo_move_to(cr, xl,        top_y);
    cairo_line_to(cr, xr,        top_y);
    cairo_line_to(cr, xr,        base_y);
    cairo_line_to(cr, (double)x, tip_y);
    cairo_line_to(cr, xl,        base_y);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.85);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* --- Cap/body divider ---------------------------------------------- */
    cairo_move_to(cr, xl, base_y);
    cairo_line_to(cr, xr, base_y);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.85);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
}

static gboolean on_trans_bar_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    DialogData* dd = (DialogData*)user_data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    /* White background */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    if (dd && dd->selected && w >= 2) {
        GArray* nodes = collect_trans_nodes(dd->selected);
        if (nodes) {
            for (guint i = 0; i < nodes->len; i++) {
                TransStop* ts   = &g_array_index(nodes, TransStop, i);
                int x           = pos_to_x(ts->pos, w);
                gboolean active = (dd->selected_trans_node == (int)i ||
                                   dd->hover_trans_node    == (int)i);
                draw_trans_node_handle(cr, x, h, ts->opacity, active);
            }
            g_array_free(nodes, TRUE);
        }
    }

    /* Blue position indicator when hovering over empty bar space */
    if (dd && dd->selected && dd->selected->transparency_stops &&
        dd->hover_trans_node < 0 && dd->hover_trans_x >= 0 && !dd->trans_dragging) {
        double r  = (double)h * 0.25;
        double cy = (double)h * 0.5;
        cairo_save(cr);
        cairo_rectangle(cr, 1.0, 1.0, (double)(w - 2), (double)(h - 2));
        cairo_clip(cr);
        cairo_arc(cr, (double)dd->hover_trans_x, cy, r, 0.0, 2.0 * M_PI);
        cairo_set_source_rgba(cr, 0.2, 0.5, 1.0, 0.85);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    /* 1-px border */
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0.5, 0.5, (double)w - 1.0, (double)h - 1.0);
    cairo_stroke(cr);
    return TRUE;
}

static gboolean on_trans_bar_button_press(GtkWidget*      widget,
                                          GdkEventButton* event,
                                          gpointer        user_data) {
    DialogData* dd = (DialogData*)user_data;
    if (!dd || !dd->selected || !dd->selected->transparency_stops) return FALSE;

    int w = gtk_widget_get_allocated_width(widget);
    if (w < 2) return FALSE;

    int    click_x = (int)event->x;
    double pos     = x_to_pos(click_x, w);

    GArray* nodes = collect_trans_nodes(dd->selected);
    if (!nodes) return FALSE;
    int node_idx  = find_trans_node_at_x(nodes, click_x, w);
    int total     = (int)nodes->len;
    g_array_free(nodes, TRUE);

    /* Clicking the transparency bar deselects any colour-stop selection */
    dd->selected_node = -1;

    if (event->button == GDK_BUTTON_PRIMARY) {
        if (node_idx >= 0) {
            dd->selected_trans_node = node_idx;
            dd->trans_dragging      = TRUE;
            dd->drag_trans_idx      = node_idx;
        } else {
            trans_bar_add_stop(dd->selected, pos);
            GArray* updated = collect_trans_nodes(dd->selected);
            if (updated) {
                dd->selected_trans_node = find_trans_node_at_x(updated, click_x, w);
                g_array_free(updated, TRUE);
            }
            gtk_widget_queue_draw(dd->editor_preview);
        }
    } else if (event->button == GDK_BUTTON_SECONDARY) {
        if (node_idx > 0 && node_idx < total - 1) {
            trans_bar_remove_stop(dd->selected, node_idx);
            if (dd->selected_trans_node >= total - 1)
                dd->selected_trans_node = total - 2;
            gtk_widget_queue_draw(dd->editor_preview);
        }
    }

    update_node_options_ui(dd);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean on_trans_bar_motion(GtkWidget*      widget,
                                    GdkEventMotion* event,
                                    gpointer        user_data) {
    DialogData* dd = (DialogData*)user_data;
    if (!dd || !dd->selected) return FALSE;

    int w = gtk_widget_get_allocated_width(widget);

    if (dd->trans_dragging && dd->drag_trans_idx >= 0) {
        double new_pos = x_to_pos((int)event->x, w);
        trans_bar_move_stop(dd->selected, &dd->drag_trans_idx, new_pos);
        dd->selected_trans_node = dd->drag_trans_idx; /* follow reordering */
        /* Sync location spin without triggering the adj callback */
        if (dd->location_adj) {
            dd->updating_node_ui = TRUE;
            gtk_adjustment_set_value(dd->location_adj, new_pos * 100.0);
            dd->updating_node_ui = FALSE;
        }
        gtk_widget_queue_draw(dd->editor_preview);
        gtk_widget_queue_draw(widget);
    } else {
        GArray* nodes = collect_trans_nodes(dd->selected);
        int idx = nodes ? find_trans_node_at_x(nodes, (int)event->x, w) : -1;
        if (nodes) g_array_free(nodes, TRUE);

        int new_hover_x = (idx < 0) ? (int)event->x : -1;
        if (idx != dd->hover_trans_node || new_hover_x != dd->hover_trans_x) {
            dd->hover_trans_node = idx;
            dd->hover_trans_x    = new_hover_x;
            gtk_widget_queue_draw(widget);
        }
    }
    return FALSE;
}

static gboolean on_trans_bar_leave(GtkWidget* widget,
                                   GdkEventCrossing* event,
                                   gpointer user_data) {
    (void)event;
    DialogData* dd = (DialogData*)user_data;
    if (dd && (dd->hover_trans_node != -1 || dd->hover_trans_x != -1)) {
        dd->hover_trans_node = -1;
        dd->hover_trans_x    = -1;
        gtk_widget_queue_draw(widget);
    }
    return FALSE;
}

static gboolean on_trans_bar_button_release(GtkWidget*      widget,
                                             GdkEventButton* event,
                                             gpointer        user_data) {
    (void)widget;
    (void)event;
    DialogData* dd = (DialogData*)user_data;
    if (dd && dd->trans_dragging) {
        dd->trans_dragging = FALSE;
        dd->drag_trans_idx = -1;
    }
    return FALSE;
}

/* -------------------------------------------------------------------------
 * Node option signal handlers
 * ---------------------------------------------------------------------- */

/* Colour-swatch button clicked: open colour chooser and apply to stop */
static void on_node_color_btn_clicked(GtkButton* button, gpointer user_data) {
    DialogData* dd = (DialogData*)user_data;
    if (!dd || !dd->selected || dd->selected_node < 0) return;

    GtkWidget* top = gtk_widget_get_toplevel(GTK_WIDGET(button));
    if (!top || !GTK_IS_WINDOW(top)) return;

    double r, g, b;
    get_node_color(dd->selected, dd->selected_node, &r, &g, &b);
    GdkRGBA initial = {(float)r, (float)g, (float)b, 1.0f};

    GtkWidget* dlg = color_chooser_dialog_new(
        GTK_WINDOW(top), "Stop Color", &initial, NULL, NULL, FALSE);
    gtk_dialog_run(GTK_DIALOG(dlg));

    double out_r, out_g, out_b;
    color_chooser_dialog_get_color(dlg, &out_r, &out_g, &out_b);
    gtk_widget_destroy(dlg);

    set_node_color(dd->selected, dd->selected_node, out_r, out_g, out_b);

    GdkRGBA new_rgba = {(float)out_r, (float)out_g, (float)out_b, 1.0f};
    update_color_button_appearance(GTK_WIDGET(button), &new_rgba);

    gtk_widget_queue_draw(dd->editor_preview);
    if (dd->node_bar) gtk_widget_queue_draw(dd->node_bar);
}

/* Opacity adjustment changed: apply to selected colour or transparency stop */
static void on_opacity_adj_changed(GtkAdjustment* adj, gpointer user_data) {
    DialogData* dd = (DialogData*)user_data;
    if (!dd || !dd->selected || dd->updating_node_ui) return;

    double opacity = gtk_adjustment_get_value(adj) / 100.0;

    if (dd->selected_node >= 0) {
        set_node_opacity(dd->selected, dd->selected_node, opacity);
        gtk_widget_queue_draw(dd->editor_preview);
        if (dd->node_bar) gtk_widget_queue_draw(dd->node_bar);
    } else if (dd->selected_trans_node >= 0 &&
               dd->selected->transparency_stops &&
               dd->selected_trans_node < dd->selected->num_transparency_stops) {
        dd->selected->transparency_stops[dd->selected_trans_node].opacity = opacity;
        gradient_invalidate(dd->selected);
        gtk_widget_queue_draw(dd->editor_preview);
        if (dd->trans_bar) gtk_widget_queue_draw(dd->trans_bar);
    }
}

/* Location adjustment changed: move selected stop along the gradient */
static void on_location_adj_changed(GtkAdjustment* adj, gpointer user_data) {
    DialogData* dd = (DialogData*)user_data;
    if (!dd || !dd->selected || dd->updating_node_ui) return;

    double pos = gtk_adjustment_get_value(adj) / 100.0;

    if (dd->selected_node >= 0) {
        node_bar_move_stop(dd->selected, &dd->selected_node, pos);
        gtk_widget_queue_draw(dd->editor_preview);
        if (dd->node_bar) gtk_widget_queue_draw(dd->node_bar);
    } else if (dd->selected_trans_node >= 0) {
        trans_bar_move_stop(dd->selected, &dd->selected_trans_node, pos);
        gtk_widget_queue_draw(dd->editor_preview);
        if (dd->trans_bar) gtk_widget_queue_draw(dd->trans_bar);
    }
}

/* -------------------------------------------------------------------------
 * Selection handler
 * ---------------------------------------------------------------------- */

static void update_editor_tab(DialogData* dd, GradientDef* def, const gchar* source) {
    dd->selected = def;
    dd->selected_source = (gchar*)source; /* borrowed */

    dd->selected_node = -1;
    dd->hover_node    = -1;
    dd->hover_x       = -1;
    dd->dragging      = FALSE;
    dd->drag_node_idx = -1;

    dd->selected_trans_node = -1;
    dd->hover_trans_node    = -1;
    dd->hover_trans_x       = -1;
    dd->trans_dragging      = FALSE;
    dd->drag_trans_idx      = -1;

    gtk_widget_queue_draw(dd->editor_preview);
    if (dd->node_bar)
        gtk_widget_queue_draw(dd->node_bar);
    if (dd->trans_bar)
        gtk_widget_queue_draw(dd->trans_bar);

    if (!def) {
        gtk_label_set_text(GTK_LABEL(dd->name_label), "No gradient selected");
        gtk_label_set_text(GTK_LABEL(dd->file_label), "");
    } else {
        gtk_label_set_text(GTK_LABEL(dd->name_label), def->name ? def->name : "(unnamed)");
        gtk_label_set_text(GTK_LABEL(dd->file_label), source ? source : "");
    }

    update_node_options_ui(dd);
}

static void on_collection_row_selected(GtkListBox* list_box,
                                       GtkListBoxRow* row,
                                       gpointer user_data) {
    /* Selection is tracked by GTK; editor is only loaded when the user
     * explicitly clicks "Edit Gradient". Nothing to do here. */
    (void)list_box;
    (void)row;
    (void)user_data;
}

/* -------------------------------------------------------------------------
 * Collection loading
 * ---------------------------------------------------------------------- */

static void load_gradient_collection(DialogData* dd, const gchar* app_dir) {
    gchar* dir_path = g_build_filename(app_dir, "gradients", NULL);
    GDir* dir = g_dir_open(dir_path, 0, NULL);

    if (!dir) {
        debug_log("DBG", "gradient_editor: no gradients directory at %s", dir_path);
        g_free(dir_path);
        return;
    }

    const gchar* fname;
    while ((fname = g_dir_read_name(dir)) != NULL) {
        const gchar* dot = strrchr(fname, '.');
        if (!dot)
            continue;
        if (g_ascii_strcasecmp(dot, ".ggr") != 0 &&
            g_ascii_strcasecmp(dot, ".grd") != 0) {
            continue;
        }

        gchar* full = g_build_filename(dir_path, fname, NULL);
        GradientIOError io_err = GRADIENT_IO_ERROR_NONE;
        GradientSet* gs = gradient_io_load(full, &io_err);
        if (!gs) {
            debug_log("DBG", "gradient_editor: failed to load %s: %s",
                      fname, gradient_io_get_error_message(io_err, full));
            g_free(full);
            continue;
        }

        GradEntry* e = g_new0(GradEntry, 1);
        e->set = gs;
        e->filepath = full;

        /* Strip extension for display */
        gchar* base = g_strdup(fname);
        gchar* ext = strrchr(base, '.');
        if (ext)
            *ext = '\0';
        e->basename = base;

        g_ptr_array_add(dd->entries, e);

        debug_log("DBG", "gradient_editor: loaded %s (%d gradient(s))",
                  fname, gs->num_gradients);
    }

    g_dir_close(dir);
    g_free(dir_path);
}

/* -------------------------------------------------------------------------
 * List population
 * ---------------------------------------------------------------------- */

/* RowData is freed when the GtkListBoxRow is destroyed via GObject notify */
static void row_data_free(gpointer p) {
    g_free(p);
}

static void populate_collection_list(DialogData* dd, GtkWidget* list_box) {
    guint total = 0;

    for (guint i = 0; i < dd->entries->len; i++) {
        GradEntry* e = (GradEntry*)g_ptr_array_index(dd->entries, i);
        if (!e->set)
            continue;

        for (int j = 0; j < e->set->num_gradients; j++) {
            GradientDef* def = &e->set->gradients[j];

            /* Warm up the preview cache now so the list draws instantly */
            gradient_preview_get(def);

            /* ---- Row container ---------------------------------------- */
            GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_widget_set_margin_start(row_box, 8);
            gtk_widget_set_margin_end(row_box, 8);
            gtk_widget_set_margin_top(row_box, 5);
            gtk_widget_set_margin_bottom(row_box, 5);

            /* ---- Swatch drawing area ----------------------------------- */
            GtkWidget* swatch = gtk_drawing_area_new();
            gtk_widget_set_size_request(swatch,
                                        GRADIENT_PREVIEW_WIDTH,
                                        GRADIENT_PREVIEW_HEIGHT);
            gtk_widget_set_valign(swatch, GTK_ALIGN_CENTER);

            RowData* rd = g_new0(RowData, 1);
            rd->def = def;
            rd->name = def->name;     /* borrowed */
            rd->source = e->basename; /* borrowed */

            g_signal_connect(swatch, "draw", G_CALLBACK(on_row_swatch_draw), rd);

            /* ---- Name + source labels ---------------------------------- */
            GtkWidget* label_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            gtk_widget_set_valign(label_box, GTK_ALIGN_CENTER);

            GtkWidget* name_lbl = gtk_label_new(def->name ? def->name : "(unnamed)");
            gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
            PangoAttrList* attrs = pango_attr_list_new();
            pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
            gtk_label_set_attributes(GTK_LABEL(name_lbl), attrs);
            pango_attr_list_unref(attrs);

            /* Source file label (dim) */
            gchar* src_text = g_strdup_printf("%s%s",
                                              e->basename,
                                              strrchr(e->filepath, '.') ? strrchr(e->filepath, '.') : "");
            GtkWidget* src_lbl = gtk_label_new(src_text);
            g_free(src_text);
            gtk_widget_set_halign(src_lbl, GTK_ALIGN_START);
            GtkStyleContext* sc = gtk_widget_get_style_context(src_lbl);
            gtk_style_context_add_class(sc, "dim-label");

            gtk_box_pack_start(GTK_BOX(label_box), name_lbl, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(label_box), src_lbl, FALSE, FALSE, 0);

            /* ---- Assemble row ----------------------------------------- */
            gtk_box_pack_start(GTK_BOX(row_box), swatch, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(row_box), label_box, TRUE, TRUE, 0);

            GtkWidget* list_row = gtk_list_box_row_new();
            gtk_container_add(GTK_CONTAINER(list_row), row_box);

            /* Attach RowData to the list row; freed when the row is destroyed */
            g_object_set_data_full(G_OBJECT(list_row), "row-data", rd, row_data_free);

            gtk_list_box_insert(GTK_LIST_BOX(list_box), list_row, -1);
            gtk_widget_show_all(list_row);

            total++;
        }
    }
    (void)total;
}

/* -------------------------------------------------------------------------
 * Collection action buttons
 * ---------------------------------------------------------------------- */

/* "Edit Gradient" – load the selected collection row into the editor and switch tab */
static void on_edit_gradient_btn_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    DialogData* dd = (DialogData*)user_data;
    if (!dd || !dd->collection_list) return;

    GtkListBoxRow* row =
        gtk_list_box_get_selected_row(GTK_LIST_BOX(dd->collection_list));
    if (!row) return;

    RowData* rd = (RowData*)g_object_get_data(G_OBJECT(row), "row-data");
    if (!rd) return;

    update_editor_tab(dd, rd->def, rd->source);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(dd->notebook), 1);
}

/* "New Gradient" – create a fresh white→black gradient and open it in the editor */
static void on_new_gradient_btn_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    DialogData* dd = (DialogData*)user_data;
    if (!dd) return;

    /* Allocate a GradientSet with one slot */
    GradientSet* set = gradient_set_new(1);
    if (!set) return;

    GradientDef* def = &set->gradients[0];
    def->name = g_strdup("New Gradient");

    /* One segment: white at 0% → black at 100% */
    def->num_segments = 1;
    def->segments = (GradientSegment*)calloc(1, sizeof(GradientSegment));
    if (!def->segments) {
        gradient_set_free(set);
        return;
    }
    GradientSegment* seg = &def->segments[0];
    seg->left_pos   = 0.0;
    seg->midpoint   = 0.5;
    seg->right_pos  = 1.0;
    seg->left_r     = 1.0;  /* white */
    seg->left_g     = 1.0;
    seg->left_b     = 1.0;
    seg->left_a     = 1.0;
    seg->right_r    = 0.0;  /* black */
    seg->right_g    = 0.0;
    seg->right_b    = 0.0;
    seg->right_a    = 1.0;
    seg->blend_mode  = GRADIENT_BLEND_LINEAR;
    seg->color_space = GRADIENT_COLOR_RGB;
    seg->left_type   = GRADIENT_ENDPOINT_FIXED;
    seg->right_type  = GRADIENT_ENDPOINT_FIXED;

    /* Create a GradEntry to own the set */
    GradEntry* entry  = g_new0(GradEntry, 1);
    entry->set        = set;
    entry->filepath   = g_strdup("(unsaved)");
    entry->basename   = g_strdup("New Gradient");
    g_ptr_array_add(dd->entries, entry);

    /* Open in editor */
    update_editor_tab(dd, def, entry->basename);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(dd->notebook), 1);
}

/* -------------------------------------------------------------------------
 * Destroy handler
 * ---------------------------------------------------------------------- */

static void on_dialog_destroy(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    (void)user_data;
    /* DialogData is freed via g_object_set_data_full with dialog_data_free */
}

/* -------------------------------------------------------------------------
 * Public entry point
 * ---------------------------------------------------------------------- */

void gradient_editor_dialog_show(AppContext* ctx) {
    if (!ctx || !ctx->window)
        return;

    /* Load the glade UI */
    GtkBuilder* builder = gtk_builder_new();
    ui_utils_builder_set_translation_domain(builder);
    GError* err = NULL;
    if (!gtk_builder_add_from_resource(builder, "/ui/gradient_editor_dialog.glade", &err)) {
        debug_log("WRN", "gradient_editor: failed to load glade: %s",
                  err ? err->message : "unknown");
        if (err)
            g_error_free(err);
        g_object_unref(builder);
        return;
    }

    GtkWidget* dialog = GTK_WIDGET(gtk_builder_get_object(builder, "gradient_editor_dialog"));
    if (!dialog) {
        debug_log("WRN", "gradient_editor: missing gradient_editor_dialog in glade");
        g_object_unref(builder);
        return;
    }

    /* Collect widget references */
    GtkWidget* notebook = GTK_WIDGET(gtk_builder_get_object(builder, "gradient_editor_notebook"));
    GtkWidget* list_box = GTK_WIDGET(gtk_builder_get_object(builder, "gradient_collection_list"));
    GtkWidget* editor_preview = GTK_WIDGET(gtk_builder_get_object(builder, "gradient_editor_preview"));
    GtkWidget* node_bar  = GTK_WIDGET(gtk_builder_get_object(builder, "gradient_node_bar"));
    GtkWidget* trans_bar = GTK_WIDGET(gtk_builder_get_object(builder, "gradient_trans_bar"));
    GtkWidget* name_label = GTK_WIDGET(gtk_builder_get_object(builder, "gradient_editor_name_label"));
    GtkWidget* file_label = GTK_WIDGET(gtk_builder_get_object(builder, "gradient_editor_file_label"));
    GtkWidget* close_btn      = GTK_WIDGET(gtk_builder_get_object(builder, "gradient_editor_close_btn"));
    GtkWidget* ok_btn         = GTK_WIDGET(gtk_builder_get_object(builder, "gradient_editor_cancel_btn"));
    GtkWidget* edit_grad_btn  = GTK_WIDGET(gtk_builder_get_object(builder, "edit_gradient_btn"));
    GtkWidget* new_grad_btn   = GTK_WIDGET(gtk_builder_get_object(builder, "new_gradient_btn"));

    /* Node options widgets */
    GtkWidget* node_options_sep   = GTK_WIDGET(gtk_builder_get_object(builder, "node_options_separator"));
    GtkWidget* node_options_lbl   = GTK_WIDGET(gtk_builder_get_object(builder, "node_options_label"));
    GtkWidget* node_options_box   = GTK_WIDGET(gtk_builder_get_object(builder, "node_options_box"));
    GtkWidget* node_color_box     = GTK_WIDGET(gtk_builder_get_object(builder, "node_color_box"));
    GtkWidget* node_color_btn     = GTK_WIDGET(gtk_builder_get_object(builder, "gradient_editor_node_color_btn"));
    GtkAdjustment* opacity_adj    = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "opacity_adjustment"));
    GtkAdjustment* location_adj   = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "location_adjustment"));

    /* Build dialog data */
    DialogData* dd = g_new0(DialogData, 1);
    dd->entries = g_ptr_array_new_with_free_func(grad_entry_free);
    dd->selected = NULL;
    dd->selected_node       = -1;
    dd->hover_node          = -1;
    dd->hover_x             = -1;
    dd->drag_node_idx       = -1;
    dd->selected_trans_node = -1;
    dd->hover_trans_node    = -1;
    dd->hover_trans_x       = -1;
    dd->drag_trans_idx      = -1;
    dd->notebook            = notebook;
    dd->editor_preview      = editor_preview;
    dd->node_bar            = node_bar;
    dd->trans_bar           = trans_bar;
    dd->collection_list     = list_box;
    dd->name_label          = name_label;
    dd->file_label          = file_label;
    dd->node_options_separator = node_options_sep;
    dd->node_options_label     = node_options_lbl;
    dd->node_options_box       = node_options_box;
    dd->node_color_box         = node_color_box;
    dd->node_color_btn         = node_color_btn;
    dd->opacity_adj            = opacity_adj;
    dd->location_adj           = location_adj;

    /* Wrap each GtkScale with a VerticalSpinButton to its right */
    {
        GtkWidget* opacity_scale = GTK_WIDGET(gtk_builder_get_object(builder, "opacity_scale"));
        if (opacity_scale && opacity_adj) {
            GtkWidget* parent = gtk_widget_get_parent(opacity_scale);
            if (parent) {
                g_object_ref(opacity_scale);
                gtk_container_remove(GTK_CONTAINER(parent), opacity_scale);
                GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                gtk_box_pack_start(GTK_BOX(hbox), opacity_scale, TRUE, TRUE, 0);
                g_object_unref(opacity_scale);
                GtkWidget* spin = vertical_spin_button_new(opacity_adj, 1.0, 0);
                gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 0);
                dd->opacity_spin = VERTICAL_SPIN_BUTTON(spin);
                gtk_box_pack_start(GTK_BOX(parent), hbox, FALSE, TRUE, 0);
                gtk_widget_show_all(hbox);
            }
        }

        GtkWidget* location_scale = GTK_WIDGET(gtk_builder_get_object(builder, "location_scale"));
        if (location_scale && location_adj) {
            GtkWidget* parent = gtk_widget_get_parent(location_scale);
            if (parent) {
                g_object_ref(location_scale);
                gtk_container_remove(GTK_CONTAINER(parent), location_scale);
                GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                gtk_box_pack_start(GTK_BOX(hbox), location_scale, TRUE, TRUE, 0);
                g_object_unref(location_scale);
                GtkWidget* spin = vertical_spin_button_new(location_adj, 1.0, 0);
                gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 0);
                dd->location_spin = VERTICAL_SPIN_BUTTON(spin);
                gtk_box_pack_start(GTK_BOX(parent), hbox, FALSE, TRUE, 0);
                gtk_widget_show_all(hbox);
            }
        }
    }

    /* Set hand cursor and wire colour-button click */
    if (node_color_btn) {
        ui_utils_widget_set_hand_cursor(node_color_btn);
        g_signal_connect(node_color_btn, "clicked",
                         G_CALLBACK(on_node_color_btn_clicked), dd);
    }

    /* Attach data to dialog; freed automatically when dialog is destroyed */
    g_object_set_data_full(G_OBJECT(dialog), "dialog-data", dd, dialog_data_free);

    /* Load gradients from app_dir/gradients/ */
    if (ctx->app_dir) {
        load_gradient_collection(dd, ctx->app_dir);
    } else {
        debug_log("WRN", "gradient_editor: ctx->app_dir is NULL, no gradients loaded");
    }

    /* Populate the list */
    populate_collection_list(dd, list_box);

    /* Wire signals */
    g_signal_connect(list_box,        "row-selected", G_CALLBACK(on_collection_row_selected),   dd);
    g_signal_connect(editor_preview,  "draw",         G_CALLBACK(on_editor_preview_draw),       dd);
    g_signal_connect(close_btn,       "clicked",      G_CALLBACK(gtk_widget_destroy),           dialog);
    if (ok_btn)
        g_signal_connect(ok_btn,      "clicked",      G_CALLBACK(gtk_widget_destroy),           dialog);
    if (edit_grad_btn)
        g_signal_connect(edit_grad_btn, "clicked",    G_CALLBACK(on_edit_gradient_btn_clicked), dd);
    if (new_grad_btn)
        g_signal_connect(new_grad_btn,  "clicked",    G_CALLBACK(on_new_gradient_btn_clicked),  dd);
    g_signal_connect(dialog,          "destroy",      G_CALLBACK(on_dialog_destroy),            NULL);

    /* Colour stop node bar: draw + mouse input */
    if (node_bar) {
        gtk_widget_add_events(node_bar,
                              GDK_BUTTON_PRESS_MASK |
                                  GDK_BUTTON_RELEASE_MASK |
                                  GDK_POINTER_MOTION_MASK |
                                  GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect(node_bar, "realize",              G_CALLBACK(on_node_bar_realize),         NULL);
        g_signal_connect(node_bar, "draw",                 G_CALLBACK(on_node_bar_draw),            dd);
        g_signal_connect(node_bar, "button-press-event",   G_CALLBACK(on_node_bar_button_press),    dd);
        g_signal_connect(node_bar, "button-release-event", G_CALLBACK(on_node_bar_button_release),  dd);
        g_signal_connect(node_bar, "motion-notify-event",  G_CALLBACK(on_node_bar_motion),          dd);
        g_signal_connect(node_bar, "leave-notify-event",   G_CALLBACK(on_node_bar_leave),           dd);
    }

    /* Transparency stop bar: draw + mouse input */
    if (trans_bar) {
        gtk_widget_add_events(trans_bar,
                              GDK_BUTTON_PRESS_MASK |
                                  GDK_BUTTON_RELEASE_MASK |
                                  GDK_POINTER_MOTION_MASK |
                                  GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect(trans_bar, "realize",              G_CALLBACK(on_node_bar_realize),          NULL);
        g_signal_connect(trans_bar, "draw",                 G_CALLBACK(on_trans_bar_draw),            dd);
        g_signal_connect(trans_bar, "button-press-event",   G_CALLBACK(on_trans_bar_button_press),    dd);
        g_signal_connect(trans_bar, "button-release-event", G_CALLBACK(on_trans_bar_button_release),  dd);
        g_signal_connect(trans_bar, "motion-notify-event",  G_CALLBACK(on_trans_bar_motion),          dd);
        g_signal_connect(trans_bar, "leave-notify-event",   G_CALLBACK(on_trans_bar_leave),           dd);
    }

    /* Adjustment signals for opacity and location controls */
    if (opacity_adj)
        g_signal_connect(opacity_adj, "value-changed",
                         G_CALLBACK(on_opacity_adj_changed), dd);
    if (location_adj)
        g_signal_connect(location_adj, "value-changed",
                         G_CALLBACK(on_location_adj_changed), dd);

    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(ctx->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    /* Initialise visibility of the node options section */
    update_node_options_ui(dd);

    gtk_widget_show_all(dialog);
    /* Re-apply visibility after show_all, which overrides gtk_widget_hide */
    update_node_options_ui(dd);

    /* Run – gtk_dialog_run is synchronous but we want a non-blocking window;
     * use gtk_widget_show_all + let the main loop handle it.
     * For a developer test dialog, gtk_dialog_run is fine too. */
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    g_object_unref(builder);
}
