#ifndef SWATCHES_WIDGET_H
#define SWATCHES_WIDGET_H

#include "ui/swatches.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SWATCHES_TYPE_WIDGET (swatches_widget_get_type())
#define SWATCHES_WIDGET(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), SWATCHES_TYPE_WIDGET, SwatchesWidget))
#define SWATCHES_WIDGET_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), SWATCHES_TYPE_WIDGET, SwatchesWidgetClass))
#define SWATCHES_IS_WIDGET(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), SWATCHES_TYPE_WIDGET))

typedef struct _SwatchesWidget SwatchesWidget;
typedef struct _SwatchesWidgetClass SwatchesWidgetClass;

struct _SwatchesWidget {
    GtkDrawingArea parent;

    /* Swatch data */
    SwatchData* swatches; /* Array of swatches */
    gint swatch_count;    /* Number of swatches */
    gint columns;         /* Number of columns in grid */
    gint rows;            /* Number of rows in grid */

    /* Layout */
    gdouble swatch_size;     /* Size of each swatch in pixels */
    gdouble max_swatch_size; /* Maximum size of each swatch in pixels */
    gdouble spacing;         /* Spacing between swatches */
    gdouble padding;         /* Padding around the grid */

    /* Mouse interaction */
    gint hovered_swatch;   /* Index of currently hovered swatch (-1 if none) */
    gboolean show_outline; /* Whether to show outline on hover */
    gint selected_swatch;  /* Index of currently selected swatch (-1 if none) */

    /* Tooltip */
    GtkWidget* tooltip_window; /* Tooltip window */
    gboolean tooltip_visible;  /* Whether tooltip is currently visible */
};

struct _SwatchesWidgetClass {
    GtkDrawingAreaClass parent_class;

    /* Signals */
    void (*swatch_selected)(SwatchesWidget* self, gint index);
};

GType swatches_widget_get_type(void) G_GNUC_CONST;

/* Constructor */
GtkWidget* swatches_widget_new(void);

/* Swatch management */
void swatches_widget_add_swatch(SwatchesWidget* self, const GdkRGBA* color, const gchar* name);
void swatches_widget_clear(SwatchesWidget* self);
void swatches_widget_set_swatch(SwatchesWidget* self, gint index, const GdkRGBA* color, const gchar* name);
gint swatches_widget_get_swatch_count(SwatchesWidget* self);
gboolean swatches_widget_get_swatch(SwatchesWidget* self, gint index, GdkRGBA* color, gchar** name);
gint swatches_widget_get_selected(SwatchesWidget* self);
void swatches_widget_set_selected(SwatchesWidget* self, gint index);

/* Layout configuration */
void swatches_widget_set_columns(SwatchesWidget* self, gint columns);
void swatches_widget_set_spacing(SwatchesWidget* self, gdouble spacing);
void swatches_widget_set_padding(SwatchesWidget* self, gdouble padding);
void swatches_widget_set_max_swatch_size(SwatchesWidget* self, gdouble max_size);

G_END_DECLS

#endif /* SWATCHES_WIDGET_H */
