#include "tone_mapping.h"
#include <math.h>
#include <string.h>

/* Parameter ranges */
#define GAMMA_MIN 1.00f
#define GAMMA_MAX 5.00f
#define GAMMA_DEFAULT 2.20f

#define EXPOSURE_MIN 0.01f
#define EXPOSURE_MAX 8.00f
#define EXPOSURE_DEFAULT_LINEAR 1.00f
#define EXPOSURE_DEFAULT_FILMIC 2.00f
#define EXPOSURE_DEFAULT_DRAGO 0.00f

#define WHITE_POINT_MIN 1.00f
#define WHITE_POINT_MAX 40.00f
#define WHITE_POINT_DEFAULT 11.20f

#define INTENSITY_MIN -4.00f
#define INTENSITY_MAX 4.00f
#define INTENSITY_DEFAULT 0.00f

#define ADAPTATION_MIN 0.00f
#define ADAPTATION_MAX 1.00f
#define ADAPTATION_DEFAULT 1.00f

#define COLOR_CORRECTION_MIN 0.00f
#define COLOR_CORRECTION_MAX 1.00f
#define COLOR_CORRECTION_DEFAULT 0.00f

/* Visible spectrum range (typical monitor range: ~0.1 to ~100 nits) */
#define VISIBLE_SPECTRUM_MIN 0.1f
#define VISIBLE_SPECTRUM_MAX 100.0f

/* Full spectrum range (typical HDR range: 0.0 to ~10000 nits) */
#define FULL_SPECTRUM_MIN 0.0f
#define FULL_SPECTRUM_MAX 10000.0f

/**
 * Clamp a value to a range
 */
static inline float clamp(float value, float min_val, float max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/**
 * Initialize tone mapping parameters with defaults
 */
void tone_map_params_init(ToneMapParams* params) {
    if (!params) {
        return;
    }

    memset(params, 0, sizeof(ToneMapParams));
    params->operator = TONE_MAP_LINEAR;
    params->gamma = GAMMA_DEFAULT;
    params->exposure = EXPOSURE_DEFAULT_LINEAR;
    params->white_point = WHITE_POINT_DEFAULT;
    params->intensity = INTENSITY_DEFAULT;
    params->adaptation = ADAPTATION_DEFAULT;
    params->color_correction = COLOR_CORRECTION_DEFAULT;
    params->normalize = TONE_MAP_NORMALIZE_NONE;
}

/**
 * Normalize HDR values based on normalization mode
 */
void tone_map_normalize(float r_in, float g_in, float b_in,
                        ToneMapNormalize normalize,
                        float* r_out, float* g_out, float* b_out) {
    if (!r_out || !g_out || !b_out) {
        return;
    }

    switch (normalize) {
        case TONE_MAP_NORMALIZE_NONE:
            *r_out = r_in;
            *g_out = g_in;
            *b_out = b_in;
            break;

        case TONE_MAP_NORMALIZE_VISIBLE_SPECTRUM: {
            /* Normalize to visible spectrum range (0.0 to 1.0) */
            /* Clamp values to visible spectrum range, then map to [0, 1] */
            *r_out = clamp(r_in, VISIBLE_SPECTRUM_MIN, VISIBLE_SPECTRUM_MAX);
            *g_out = clamp(g_in, VISIBLE_SPECTRUM_MIN, VISIBLE_SPECTRUM_MAX);
            *b_out = clamp(b_in, VISIBLE_SPECTRUM_MIN, VISIBLE_SPECTRUM_MAX);
            
            /* Map to [0, 1] range */
            float range = VISIBLE_SPECTRUM_MAX - VISIBLE_SPECTRUM_MIN;
            if (range > 0.0f) {
                *r_out = (*r_out - VISIBLE_SPECTRUM_MIN) / range;
                *g_out = (*g_out - VISIBLE_SPECTRUM_MIN) / range;
                *b_out = (*b_out - VISIBLE_SPECTRUM_MIN) / range;
            } else {
                *r_out = *g_out = *b_out = 0.0f;
            }
            break;
        }

        case TONE_MAP_NORMALIZE_FULL_SPECTRUM: {
            /* Normalize to full spectrum range (0.0 to 1.0) */
            /* Clamp values to full spectrum range, then map to [0, 1] */
            *r_out = clamp(r_in, FULL_SPECTRUM_MIN, FULL_SPECTRUM_MAX);
            *g_out = clamp(g_in, FULL_SPECTRUM_MIN, FULL_SPECTRUM_MAX);
            *b_out = clamp(b_in, FULL_SPECTRUM_MIN, FULL_SPECTRUM_MAX);
            
            /* Map to [0, 1] range */
            float range = FULL_SPECTRUM_MAX - FULL_SPECTRUM_MIN;
            if (range > 0.0f) {
                *r_out = (*r_out - FULL_SPECTRUM_MIN) / range;
                *g_out = (*g_out - FULL_SPECTRUM_MIN) / range;
                *b_out = (*b_out - FULL_SPECTRUM_MIN) / range;
            } else {
                *r_out = *g_out = *b_out = 0.0f;
            }
            break;
        }
    }
}

/**
 * Linear tone mapping operator
 */
static void tone_map_linear(float r, float g, float b, float gamma,
                            uint8_t* r_out, uint8_t* g_out, uint8_t* b_out) {
    /* Apply gamma correction */
    float inv_gamma = 1.0f / gamma;
    r = powf(fmaxf(r, 0.0f), inv_gamma);
    g = powf(fmaxf(g, 0.0f), inv_gamma);
    b = powf(fmaxf(b, 0.0f), inv_gamma);

    /* Clamp and convert to 8-bit */
    r = clamp(r, 0.0f, 1.0f);
    g = clamp(g, 0.0f, 1.0f);
    b = clamp(b, 0.0f, 1.0f);

    *r_out = (uint8_t)(r * 255.0f + 0.5f);
    *g_out = (uint8_t)(g * 255.0f + 0.5f);
    *b_out = (uint8_t)(b * 255.0f + 0.5f);
}

/**
 * Filmic tone mapping operator (ACES-inspired)
 */
static void tone_map_filmic(float r, float g, float b, float gamma,
                            float exposure, float white_point,
                            uint8_t* r_out, uint8_t* g_out, uint8_t* b_out) {
    /* Apply exposure */
    r *= exposure;
    g *= exposure;
    b *= exposure;

    /* Filmic curve (ACES-inspired) */
    /* x = max(0, color - 0.004) */
    /* color = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06) */
    float filmic_curve(float x) {
        x = fmaxf(0.0f, x - 0.004f);
        float numerator = x * (6.2f * x + 0.5f);
        float denominator = x * (6.2f * x + 1.7f) + 0.06f;
        if (denominator > 0.0f) {
            return numerator / denominator;
        }
        return 0.0f;
    }

    r = filmic_curve(r / white_point);
    g = filmic_curve(g / white_point);
    b = filmic_curve(b / white_point);

    /* Apply gamma correction */
    float inv_gamma = 1.0f / gamma;
    r = powf(fmaxf(r, 0.0f), inv_gamma);
    g = powf(fmaxf(g, 0.0f), inv_gamma);
    b = powf(fmaxf(b, 0.0f), inv_gamma);

    /* Clamp and convert to 8-bit */
    r = clamp(r, 0.0f, 1.0f);
    g = clamp(g, 0.0f, 1.0f);
    b = clamp(b, 0.0f, 1.0f);

    *r_out = (uint8_t)(r * 255.0f + 0.5f);
    *g_out = (uint8_t)(g * 255.0f + 0.5f);
    *b_out = (uint8_t)(b * 255.0f + 0.5f);
}

/**
 * Drago tone mapping operator
 */
static void tone_map_drago(float r, float g, float b, float gamma,
                           float exposure,
                           uint8_t* r_out, uint8_t* g_out, uint8_t* b_out) {
    /* Apply exposure */
    r *= powf(2.0f, exposure);
    g *= powf(2.0f, exposure);
    b *= powf(2.0f, exposure);

    /* Find maximum luminance */
    float max_lum = fmaxf(fmaxf(r, g), b);
    if (max_lum <= 0.0f) {
        *r_out = *g_out = *b_out = 0;
        return;
    }

    /* Drago logarithmic mapping */
    /* Ld = Ldmax * log(1 + Lw) / log(1 + Lwmax) */
    /* where Ldmax = 100, Lwmax = max_lum */
    float Ldmax = 100.0f;
    float Lwmax = max_lum;
    float log_base = logf(1.0f + Lwmax);
    
    if (log_base <= 0.0f) {
        *r_out = *g_out = *b_out = 0;
        return;
    }

    float scale = Ldmax / log_base;
    r = logf(1.0f + r) * scale / Ldmax;
    g = logf(1.0f + g) * scale / Ldmax;
    b = logf(1.0f + b) * scale / Ldmax;

    /* Apply gamma correction */
    float inv_gamma = 1.0f / gamma;
    r = powf(fmaxf(r, 0.0f), inv_gamma);
    g = powf(fmaxf(g, 0.0f), inv_gamma);
    b = powf(fmaxf(b, 0.0f), inv_gamma);

    /* Clamp and convert to 8-bit */
    r = clamp(r, 0.0f, 1.0f);
    g = clamp(g, 0.0f, 1.0f);
    b = clamp(b, 0.0f, 1.0f);

    *r_out = (uint8_t)(r * 255.0f + 0.5f);
    *g_out = (uint8_t)(g * 255.0f + 0.5f);
    *b_out = (uint8_t)(b * 255.0f + 0.5f);
}

/**
 * Reinhard tone mapping operator (extended version)
 */
static void tone_map_reinhard(float r, float g, float b,
                              float intensity, float adaptation, float color_correction,
                              uint8_t* r_out, uint8_t* g_out, uint8_t* b_out) {
    /* Calculate luminance */
    float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    
    if (lum <= 0.0f) {
        *r_out = *g_out = *b_out = 0;
        return;
    }

    /* Extended Reinhard formula */
    /* Ld = Lw / (1 + Lw) * (1 + Lw / (Lwhite^2)) */
    /* where Lwhite = adaptation * max_luminance */
    float Lwhite = adaptation * 100.0f; /* Assume max luminance of 100 for adaptation */
    if (Lwhite < 1.0f) Lwhite = 1.0f;

    float Ld = lum / (1.0f + lum) * (1.0f + lum / (Lwhite * Lwhite));
    
    /* Apply intensity adjustment */
    Ld *= powf(2.0f, intensity);

    /* Scale colors by tone-mapped luminance ratio */
    float scale = Ld / lum;
    r *= scale;
    g *= scale;
    b *= scale;

    /* Color correction (optional color saturation adjustment) */
    if (color_correction > 0.0f) {
        float sat = 1.0f + color_correction;
        float gray = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        r = gray + (r - gray) * sat;
        g = gray + (g - gray) * sat;
        b = gray + (b - gray) * sat;
    }

    /* Clamp and convert to 8-bit */
    r = clamp(r, 0.0f, 1.0f);
    g = clamp(g, 0.0f, 1.0f);
    b = clamp(b, 0.0f, 1.0f);

    *r_out = (uint8_t)(r * 255.0f + 0.5f);
    *g_out = (uint8_t)(g * 255.0f + 0.5f);
    *b_out = (uint8_t)(b * 255.0f + 0.5f);
}

/**
 * Tone map a single HDR RGB value to 8-bit RGB
 */
void tone_map_rgb(float r_in, float g_in, float b_in,
                  const ToneMapParams* params,
                  uint8_t* r_out, uint8_t* g_out, uint8_t* b_out) {
    if (!params || !r_out || !g_out || !b_out) {
        return;
    }

    float r = r_in, g = g_in, b = b_in;

    /* Apply normalization only when linear operator is set */
    if (params->operator == TONE_MAP_LINEAR && params->normalize != TONE_MAP_NORMALIZE_NONE) {
        tone_map_normalize(r, g, b, params->normalize, &r, &g, &b);
    }

    /* Clamp parameters to valid ranges */
    float gamma = clamp(params->gamma, GAMMA_MIN, GAMMA_MAX);
    float exposure = clamp(params->exposure, EXPOSURE_MIN, EXPOSURE_MAX);
    float white_point = clamp(params->white_point, WHITE_POINT_MIN, WHITE_POINT_MAX);
    float intensity = clamp(params->intensity, INTENSITY_MIN, INTENSITY_MAX);
    float adaptation = clamp(params->adaptation, ADAPTATION_MIN, ADAPTATION_MAX);
    float color_correction = clamp(params->color_correction, COLOR_CORRECTION_MIN, COLOR_CORRECTION_MAX);

    /* Apply tone mapping operator */
    switch (params->operator) {
        case TONE_MAP_LINEAR:
            tone_map_linear(r, g, b, gamma, r_out, g_out, b_out);
            break;

        case TONE_MAP_FILMIC:
            tone_map_filmic(r, g, b, gamma, exposure, white_point, r_out, g_out, b_out);
            break;

        case TONE_MAP_DRAGO:
            tone_map_drago(r, g, b, gamma, exposure, r_out, g_out, b_out);
            break;

        case TONE_MAP_REINHARD:
            tone_map_reinhard(r, g, b, intensity, adaptation, color_correction, r_out, g_out, b_out);
            break;
    }
}

/**
 * Tone map an array of HDR RGB values to 8-bit RGB
 */
void tone_map_image(const float* hdr_input, uint8_t* output,
                    uint32_t num_pixels, const ToneMapParams* params) {
    if (!hdr_input || !output || !params || num_pixels == 0) {
        return;
    }

    for (uint32_t i = 0; i < num_pixels; i++) {
        float r = hdr_input[i * 3 + 0];
        float g = hdr_input[i * 3 + 1];
        float b = hdr_input[i * 3 + 2];

        tone_map_rgb(r, g, b, params,
                     &output[i * 3 + 0],
                     &output[i * 3 + 1],
                     &output[i * 3 + 2]);
    }
}
