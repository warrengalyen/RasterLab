/*
 * This file is part of RasterLab
 * Copyright (c) 2026 Warren Galyen
 * All rights reserved.
 *
 * This software is provided as freeware for personal and commercial use.
 * Redistribution, modification, or reverse engineering is not permitted.
 * See LICENSE.txt for full terms.
 */

#ifndef FONT_CHOOSER_WIDGET_H
#define FONT_CHOOSER_WIDGET_H

#include <gtk/gtk.h>
#include <pango/pango.h>

G_BEGIN_DECLS

#define FONT_CHOOSER_WIDGET_TYPE (font_chooser_widget_get_type())
#define FONT_CHOOSER_WIDGET(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), FONT_CHOOSER_WIDGET_TYPE, FontChooserWidget))
#define IS_FONT_CHOOSER_WIDGET(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), FONT_CHOOSER_WIDGET_TYPE))

typedef struct _FontChooserWidget FontChooserWidget;
typedef struct _FontChooserWidgetClass FontChooserWidgetClass;

struct _FontChooserWidgetClass {
    GtkBoxClass parent_class;
};

GType font_chooser_widget_get_type(void) G_GNUC_CONST;

/**
 * Create a new font chooser widget.
 * The widget presents a button that opens a popover with an expandable
 * font family tree (families as roots, faces as children), a font preview
 * column rendered in the actual typeface, and a TrueType/OpenType icon.
 *
 * Signal "font-changed": emitted when the user selects a font.
 *   Handler signature:
 *     void handler(FontChooserWidget* widget,
 *                  const gchar* family,
 *                  gint weight,
 *                  gint style,
 *                  gboolean apply_face,
 *                  gpointer user_data);
 *
 *   - family: font family name
 *   - weight: PangoWeight from the selected face, or PANGO_WEIGHT_NORMAL for a family root
 *   - style: PangoStyle from the selected face, or PANGO_STYLE_NORMAL for a family root
 *   - apply_face: TRUE when a face row or family root is activated (weight/style apply);
 *                 family root uses Normal / Normal as the default variation
 *
 * @return The top-level GtkWidget* (a GtkBox containing the button)
 */
GtkWidget* font_chooser_widget_new(void);

/**
 * Set the currently displayed font family.
 * If the family exists in the tree, the button label is updated.
 * @param widget  The font chooser widget (as returned by font_chooser_widget_new)
 * @param family  Font family name to select
 */
void font_chooser_widget_set_family(GtkWidget* widget, const gchar* family);

/**
 * Get the currently selected font family name.
 * @param widget  The font chooser widget
 * @return The family name string (owned by the widget; do not free)
 */
const gchar* font_chooser_widget_get_family(GtkWidget* widget);

/**
 * GtkListStore columns for font_chooser_face_combo_new() / font_chooser_face_combo_fill().
 */
enum {
    FONT_FACE_COMBO_COL_LABEL = 0,
    FONT_FACE_COMBO_COL_WEIGHT,
    FONT_FACE_COMBO_COL_STYLE,
};

/**
 * Build a GtkComboBox listing Pango font faces (variation names with weight/style in the model).
 */
GtkWidget* font_chooser_face_combo_new(void);

/**
 * Replace all rows with faces for @a family (case-insensitive Pango match).
 */
void font_chooser_face_combo_fill(GtkComboBox* combo, const gchar* family);

/**
 * Select the row matching weight and PangoStyle, or the first row if no exact match.
 * @return TRUE if a row was selected
 */
gboolean font_chooser_face_combo_select(GtkComboBox* combo, gint weight, gint style);

G_END_DECLS

#endif /* FONT_CHOOSER_WIDGET_H */
