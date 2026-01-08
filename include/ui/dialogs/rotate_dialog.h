#ifndef ROTATE_DIALOG_H
#define ROTATE_DIALOG_H

#include "ocular.h"
#include "ui/widgets/filter_preview.h"
#include <cairo.h>
#include <gtk/gtk.h>

typedef struct _RotateDialog RotateDialog;

typedef gboolean (*RotateDialogPreviewCallback)(RotateDialog* dialog,
                                                gdouble angle_degrees,
                                                gboolean preserve_size,
                                                OcInterpolationMode interpolation,
                                                gboolean use_transparency,
                                                const GdkRGBA* fill_color,
                                                gpointer user_data);

RotateDialog* rotate_dialog_new(const gchar* title);
void rotate_dialog_free(RotateDialog* dialog);
GtkWindow* rotate_dialog_get_window(RotateDialog* dialog);

FilterPreview* rotate_dialog_get_preview(RotateDialog* dialog);

void rotate_dialog_set_before_surface(RotateDialog* dialog, cairo_surface_t* before_surface);
void rotate_dialog_set_after_surface(RotateDialog* dialog, cairo_surface_t* after_surface);

void rotate_dialog_set_preview_callback(RotateDialog* dialog,
                                        RotateDialogPreviewCallback callback,
                                        gpointer user_data);

void rotate_dialog_reset(RotateDialog* dialog);

gint rotate_dialog_run(RotateDialog* dialog,
                       GtkWindow* parent,
                       gdouble* out_angle_degrees,
                       gboolean* out_preserve_size,
                       OcInterpolationMode* out_interpolation,
                       gboolean* out_use_transparency,
                       GdkRGBA* out_fill_color);

/* Accessor used by preview callback */
cairo_surface_t* rotate_dialog_get_before_surface(RotateDialog* dialog);

#endif /* ROTATE_DIALOG_H */
