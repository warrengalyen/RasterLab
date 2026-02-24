#include "ui/widgets/canvas_ruler.h"
#include "document.h"
#include <cairo.h>
#include <gdk/gdk.h>
#include <math.h>

#define RULER_SIZE_PX 24
/** Canvas pixels between major ticks*/
#define TICK_CANVAS_STEP 100

G_DEFINE_TYPE(CanvasRuler, canvas_ruler, GTK_TYPE_DRAWING_AREA)

static gboolean canvas_ruler_draw(GtkWidget* widget, cairo_t* cr);
static void on_adjustment_value_changed(GtkAdjustment* adj, gpointer user_data);

static void canvas_ruler_init(CanvasRuler* self) {
    self->orientation = CANVAS_RULER_HORIZONTAL;
    self->document = NULL;

    gtk_widget_set_size_request(GTK_WIDGET(self),
                                self->orientation == CANVAS_RULER_HORIZONTAL ? -1 : RULER_SIZE_PX,
                                self->orientation == CANVAS_RULER_HORIZONTAL ? RULER_SIZE_PX : -1);
}

static void canvas_ruler_class_init(CanvasRulerClass* klass) {
    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->draw = canvas_ruler_draw;
}

/**
 * Get canvas origin in viewport/ruler pixels.
 * canvas_origin_x = -hadjustment value, canvas_origin_y = -vadjustment value
 * so that tick "0" aligns with the canvas edge (not viewport origin).
 */
static void get_canvas_origin(ImageDocument* doc, gdouble* out_origin_x, gdouble* out_origin_y) {
    gdouble ox = 0.0, oy = 0.0;
    if (doc && doc->scrolled_window && GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(doc->scrolled_window));
        if (hadj) ox = -gtk_adjustment_get_value(hadj);
        if (vadj) oy = -gtk_adjustment_get_value(vadj);
    }
    if (out_origin_x) *out_origin_x = ox;
    if (out_origin_y) *out_origin_y = oy;
}

static gboolean canvas_ruler_draw(GtkWidget* widget, cairo_t* cr) {
    CanvasRuler* ruler = CANVAS_RULER(widget);
    GtkStyleContext* style = gtk_widget_get_style_context(widget);
    gint w = gtk_widget_get_allocated_width(widget);
    gint h = gtk_widget_get_allocated_height(widget);
    ImageDocument* doc = (ImageDocument*)ruler->document;
    gdouble canvas_origin_x, canvas_origin_y;
    gdouble zoom;

    gtk_render_background(style, cr, 0, 0, (gdouble)w, (gdouble)h);

    if (!doc) {
        return FALSE;
    }

    get_canvas_origin(doc, &canvas_origin_x, &canvas_origin_y);
    zoom = doc->zoom_factor;
    if (zoom <= 0.0) zoom = 1.0;

    /* Offset tick drawing by scroll position so tick "0" aligns with canvas edge */
    if (ruler->orientation == CANVAS_RULER_HORIZONTAL) {
        gdouble x;
        int canvas_val;
        /* First tick: largest multiple of TICK_CANVAS_STEP such that ruler x >= -1 */
        int first = (int)floor((-1.0 - canvas_origin_x) / zoom / (gdouble)TICK_CANVAS_STEP) * TICK_CANVAS_STEP;
        for (canvas_val = first; ; canvas_val += TICK_CANVAS_STEP) {
            x = canvas_origin_x + canvas_val * zoom;
            if (x > (gdouble)w + 1.0) break;
            if (x < -1.0) continue;
            cairo_move_to(cr, x, (gdouble)h);
            cairo_line_to(cr, x, (gdouble)h - 8.0);
            cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
            cairo_stroke(cr);
        }
    } else {
        gdouble y;
        int canvas_val;
        int first = (int)floor((-1.0 - canvas_origin_y) / zoom / (gdouble)TICK_CANVAS_STEP) * TICK_CANVAS_STEP;
        for (canvas_val = first; ; canvas_val += TICK_CANVAS_STEP) {
            y = canvas_origin_y + canvas_val * zoom;
            if (y > (gdouble)h + 1.0) break;
            if (y < -1.0) continue;
            cairo_move_to(cr, (gdouble)w, y);
            cairo_line_to(cr, (gdouble)w - 8.0, y);
            cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
            cairo_stroke(cr);
        }
    }

    return FALSE;
}

static void on_adjustment_value_changed(GtkAdjustment* adj, gpointer user_data) {
    GtkWidget* ruler_widget = (GtkWidget*)user_data;
    (void)adj;
    if (ruler_widget && gtk_widget_get_visible(ruler_widget)) {
        gtk_widget_queue_draw(ruler_widget);
    }
}

GtkWidget* canvas_ruler_new(CanvasRulerOrientation orientation) {
    CanvasRuler* ruler = g_object_new(CANVAS_RULER_TYPE, NULL);
    ruler->orientation = orientation;

    if (orientation == CANVAS_RULER_HORIZONTAL) {
        gtk_widget_set_size_request(GTK_WIDGET(ruler), -1, RULER_SIZE_PX);
    } else {
        gtk_widget_set_size_request(GTK_WIDGET(ruler), RULER_SIZE_PX, -1);
    }

    return GTK_WIDGET(ruler);
}

void canvas_ruler_set_document(CanvasRuler* ruler, gpointer document) {
    ImageDocument* doc = (ImageDocument*)document;
    if (!CANVAS_IS_RULER(ruler)) {
        return;
    }
    ruler->document = document;
    /* Connect to scroll adjustments so ruler redraws when viewport scrolls (zero stays aligned) */
    if (doc && doc->scrolled_window && GTK_IS_SCROLLED_WINDOW(doc->scrolled_window)) {
        GtkScrolledWindow* sw = GTK_SCROLLED_WINDOW(doc->scrolled_window);
        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(sw);
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(sw);
        GtkWidget* widget = GTK_WIDGET(ruler);
        if (ruler->orientation == CANVAS_RULER_HORIZONTAL && hadj) {
            g_signal_connect(hadj, "value-changed", G_CALLBACK(on_adjustment_value_changed), widget);
        }
        if (ruler->orientation == CANVAS_RULER_VERTICAL && vadj) {
            g_signal_connect(vadj, "value-changed", G_CALLBACK(on_adjustment_value_changed), widget);
        }
    }
    gtk_widget_queue_draw(GTK_WIDGET(ruler));
}

CanvasRulerOrientation canvas_ruler_get_orientation(CanvasRuler* ruler) {
    return ruler ? ruler->orientation : CANVAS_RULER_HORIZONTAL;
}
