#ifndef CANVAS_RULER_H
#define CANVAS_RULER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CANVAS_RULER_TYPE (canvas_ruler_get_type())
#define CANVAS_RULER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), CANVAS_RULER_TYPE, CanvasRuler))
#define CANVAS_RULER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), CANVAS_RULER_TYPE, CanvasRulerClass))
#define CANVAS_IS_RULER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), CANVAS_RULER_TYPE))

typedef struct _CanvasRuler CanvasRuler;
typedef struct _CanvasRulerClass CanvasRulerClass;

/** Ruler orientation */
typedef enum {
    CANVAS_RULER_HORIZONTAL,
    CANVAS_RULER_VERTICAL
} CanvasRulerOrientation;

struct _CanvasRuler {
    GtkDrawingArea parent;

    CanvasRulerOrientation orientation;
    /** Document (ImageDocument*); used to access canvas and scrolled window */
    gpointer document;
};

struct _CanvasRulerClass {
    GtkDrawingAreaClass parent_class;
};

GType canvas_ruler_get_type(void) G_GNUC_CONST;

/** Create a ruler. Orientation must be set before use. */
GtkWidget* canvas_ruler_new(CanvasRulerOrientation orientation);

/** Set the document (ImageDocument*) for canvas/scrolled window access and redraws. */
void canvas_ruler_set_document(CanvasRuler* ruler, gpointer document);

CanvasRulerOrientation canvas_ruler_get_orientation(CanvasRuler* ruler);

G_END_DECLS

#endif /* CANVAS_RULER_H */
