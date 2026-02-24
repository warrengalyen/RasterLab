#include "ui/ruler_units.h"

#define INCH_TO_CM  2.54
#define INCH_TO_MM  25.4
#define INCH_TO_PT  72.0
#define INCH_TO_PC  6.0

gdouble ruler_units_pixel_to_value(gdouble pixel, RulerUnit unit, gdouble dpi, guint canvas_extent) {
    gdouble inch;

    if (dpi <= 0.0) {
        dpi = RULER_DPI_DEFAULT;
    }

    switch (unit) {
        case RULER_UNIT_PIXEL:
            return pixel;
        case RULER_UNIT_PERCENT:
            if (canvas_extent == 0) return 0.0;
            return (pixel / (gdouble)canvas_extent) * 100.0;
        case RULER_UNIT_INCH:
            return pixel / dpi;
        case RULER_UNIT_CM:
            inch = pixel / dpi;
            return inch * INCH_TO_CM;
        case RULER_UNIT_MM:
            inch = pixel / dpi;
            return inch * INCH_TO_MM;
        case RULER_UNIT_PT:
            inch = pixel / dpi;
            return inch * INCH_TO_PT;
        case RULER_UNIT_PC:
            inch = pixel / dpi;
            return inch * INCH_TO_PC;
        default:
            return pixel;
    }
}

gchar* ruler_units_format_value(gdouble value, RulerUnit unit) {
    switch (unit) {
        case RULER_UNIT_PIXEL:
        case RULER_UNIT_PT:
            return g_strdup_printf("%d", (int)(value + 0.5));
        case RULER_UNIT_PERCENT:
            if (value >= 100.0 || (value > -0.01 && value < 0.01))
                return g_strdup_printf("%.0f", value);
            return g_strdup_printf("%.1f", value);
        case RULER_UNIT_INCH:
        case RULER_UNIT_CM:
        case RULER_UNIT_PC:
            if (value >= 100.0) return g_strdup_printf("%.1f", value);
            if (value >= 10.0) return g_strdup_printf("%.2f", value);
            return g_strdup_printf("%.3f", value);
        case RULER_UNIT_MM:
            if (value >= 100.0) return g_strdup_printf("%.0f", value);
            if (value >= 10.0) return g_strdup_printf("%.1f", value);
            return g_strdup_printf("%.2f", value);
        default:
            return g_strdup_printf("%.2f", value);
    }
}

RulerUnit ruler_unit_from_string(const gchar* str) {
    if (!str) return RULER_UNIT_PIXEL;
    if (g_str_equal(str, "%"))  return RULER_UNIT_PERCENT;
    if (g_str_equal(str, "px")) return RULER_UNIT_PIXEL;
    if (g_str_equal(str, "in")) return RULER_UNIT_INCH;
    if (g_str_equal(str, "cm")) return RULER_UNIT_CM;
    if (g_str_equal(str, "mm")) return RULER_UNIT_MM;
    if (g_str_equal(str, "pt")) return RULER_UNIT_PT;
    if (g_str_equal(str, "pc")) return RULER_UNIT_PC;
    return RULER_UNIT_PIXEL;
}

const gchar* ruler_unit_to_string(RulerUnit unit) {
    switch (unit) {
        case RULER_UNIT_PERCENT: return "%";
        case RULER_UNIT_PIXEL:   return "px";
        case RULER_UNIT_INCH:    return "in";
        case RULER_UNIT_CM:      return "cm";
        case RULER_UNIT_MM:      return "mm";
        case RULER_UNIT_PT:      return "pt";
        case RULER_UNIT_PC:      return "pc";
        default:                 return "px";
    }
}
