#include "ui/widgets/canvas_ruler.h"
#include "document.h"
#include <cairo.h>
#include <gdk/gdk.h>

#define RULER_SIZE_PX 24

G_DEFINE_TYPE(CanvasRuler, canvas_ruler, GTK_TYPE_DRAWING_AREA)

static gboolean canvas_ruler_draw(GtkWidget* widget, cairo_t* cr);

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

static gboolean canvas_ruler_draw(GtkWidget* widget, cairo_t* cr) {
    GtkStyleContext* style = gtk_widget_get_style_context(widget);
    gint w = gtk_widget_get_allocated_width(widget);
    gint h = gtk_widget_get_allocated_height(widget);

    gtk_render_background(style, cr, 0, 0, (gdouble)w, (gdouble)h);
    /* Stage 1: no tick marks or mouse indicator yet */

    return FALSE;
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
    if (!CANVAS_IS_RULER(ruler)) {
        return;
    }
    ruler->document = document;
    gtk_widget_queue_draw(GTK_WIDGET(ruler));
}

CanvasRulerOrientation canvas_ruler_get_orientation(CanvasRuler* ruler) {
    return ruler ? ruler->orientation : CANVAS_RULER_HORIZONTAL;
}
