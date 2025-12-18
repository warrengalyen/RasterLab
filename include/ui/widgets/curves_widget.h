#ifndef CURVES_WIDGET_H
#define CURVES_WIDGET_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define MAX_NODES 16
#define CURVES_TYPE_WIDGET (curves_widget_get_type())
#define CURVES_WIDGET(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), CURVES_TYPE_WIDGET, CurvesWidget))
#define CURVES_WIDGET_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), CURVES_TYPE_WIDGET, CurvesWidgetClass))
#define CURVES_IS_WIDGET(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), CURVES_TYPE_WIDGET))

typedef struct _CurvesWidget CurvesWidget;
typedef struct _CurvesWidgetClass CurvesWidgetClass;

typedef struct {
    double x;
    double y;
} CurveNode;

typedef enum {
    CHANNEL_RGB,
    CHANNEL_RED,
    CHANNEL_GREEN,
    CHANNEL_BLUE
} Channel;

struct _CurvesWidget {
    GtkDrawingArea parent;

    // Curve data - separate curves for each channel
    CurveNode nodes_rgb[MAX_NODES];
    CurveNode nodes_r[MAX_NODES];
    CurveNode nodes_g[MAX_NODES];
    CurveNode nodes_b[MAX_NODES];

    int node_count_rgb;
    int node_count_r;
    int node_count_g;
    int node_count_b;

    int selected_node;
    int hovered_node;

    // Histogram data (256 bins per channel)
    double histogram_r[256];
    double histogram_g[256];
    double histogram_b[256];
    double histogram_rgb[256];

    // Display options
    gboolean show_histogram;
    gboolean show_grid;
    gboolean show_diagonal;
    Channel active_channel;

    // Mouse interaction
    double mouse_x;
    double mouse_y;
    gboolean dragging;
    gboolean show_tooltip;
};

struct _CurvesWidgetClass {
    GtkDrawingAreaClass parent_class;
};

GType curves_widget_get_type(void) G_GNUC_CONST;

// Constructor
GtkWidget* curves_widget_new(void);

// Display option setters
void curves_widget_set_histogram_visible(CurvesWidget* self, gboolean visible);
void curves_widget_set_grid_visible(CurvesWidget* self, gboolean visible);
void curves_widget_set_diagonal_visible(CurvesWidget* self, gboolean visible);
void curves_widget_set_channel(CurvesWidget* self, Channel channel);

// Data setters
void curves_widget_set_histogram_data(CurvesWidget* self, Channel channel,
                                      const double* data, int size);

// Curve data accessors
int curves_widget_get_node_count(CurvesWidget* self);
void curves_widget_get_node(CurvesWidget* self, int index, double* x, double* y);
void curves_widget_reset_curve(CurvesWidget* self);

// Convert widget curve to lookup table (256 values) for filter compatibility
void curves_widget_get_lut(CurvesWidget* self, uint8_t* lut);

G_END_DECLS

#endif /* CURVES_WIDGET_H */