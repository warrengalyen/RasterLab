#ifndef RULER_UNITS_H
#define RULER_UNITS_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    RULER_UNIT_PERCENT,
    RULER_UNIT_PIXEL,
    RULER_UNIT_INCH,
    RULER_UNIT_CM,
    RULER_UNIT_MM,
    RULER_UNIT_PT,
    RULER_UNIT_PC
} RulerUnit;

#define RULER_DPI_DEFAULT 96.0

/**
 * Convert a length in canvas pixels to the given unit.
 * @param pixel Length in canvas pixels
 * @param unit Target unit
 * @param dpi DPI for physical units (use RULER_DPI_DEFAULT if 0)
 * @param canvas_extent Canvas width (horizontal) or height (vertical) for percentage
 */
gdouble ruler_units_pixel_to_value(gdouble pixel, RulerUnit unit, gdouble dpi, guint canvas_extent);

/**
 * Format a ruler value for display in the given unit.
 * Caller must g_free the result.
 */
gchar* ruler_units_format_value(gdouble value, RulerUnit unit);

/**
 * Map statusbar/combobox string to RulerUnit. Returns RULER_UNIT_PIXEL for unknown.
 */
RulerUnit ruler_unit_from_string(const gchar* str);

/**
 * Map RulerUnit to short string for UI (e.g. "px", "in").
 */
const gchar* ruler_unit_to_string(RulerUnit unit);

G_END_DECLS

#endif /* RULER_UNITS_H */
