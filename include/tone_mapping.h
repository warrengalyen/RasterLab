#ifndef TONE_MAPPING_H
#define TONE_MAPPING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Tone mapping operators
 */
typedef enum {
    TONE_MAP_LINEAR = 0,
    TONE_MAP_FILMIC = 1,
    TONE_MAP_DRAGO = 2,
    TONE_MAP_REINHARD = 3
} ToneMapOperator;

/**
 * Normalization modes
 */
typedef enum {
    TONE_MAP_NORMALIZE_NONE = 0,
    TONE_MAP_NORMALIZE_VISIBLE_SPECTRUM = 1,
    TONE_MAP_NORMALIZE_FULL_SPECTRUM = 2
} ToneMapNormalize;

/**
 * Tone mapping parameters structure
 */
typedef struct {
    /* Tone mapping operator */
    ToneMapOperator operator;

    /* Operator-specific parameters */
    /* Linear: gamma */
    /* Filmic: gamma, exposure, white_point */
    /* Drago: gamma, exposure */
    /* Reinhard: intensity, adaptation, color_correction */
    float gamma;              /* 1.00-5.00, default 2.20 */
    float exposure;           /* 0.01-8.00, default varies by operator */
    float white_point;        /* 1.00-40.00, default 11.20 (Filmic only) */
    float intensity;          /* -4.00-4.00, default 0.00 (Reinhard only) */
    float adaptation;         /* 0.00-1.00, default 1.00 (Reinhard only) */
    float color_correction;   /* 0.00-1.00, default 0.00 (Reinhard only) */

    /* Normalization mode (only used with linear operator) */
    ToneMapNormalize normalize;

    /* Drago: global max luminance (0 = auto in tone_map_image, else use for Lwmax) */
    float max_luminance;
} ToneMapParams;

/**
 * Initialize tone mapping parameters with defaults
 * @param params Output structure to initialize
 */
void tone_map_params_init(ToneMapParams* params);

/**
 * Tone map a single HDR RGB value to 8-bit RGB
 * @param r_in Input red value (linear HDR, can be > 1.0)
 * @param g_in Input green value (linear HDR, can be > 1.0)
 * @param b_in Input blue value (linear HDR, can be > 1.0)
 * @param params Tone mapping parameters
 * @param r_out Output red value (0-255)
 * @param g_out Output green value (0-255)
 * @param b_out Output blue value (0-255)
 */
void tone_map_rgb(float r_in, float g_in, float b_in,
                  const ToneMapParams* params,
                  uint8_t* r_out, uint8_t* g_out, uint8_t* b_out);

/**
 * Tone map an array of HDR RGB values to 8-bit RGB
 * @param hdr_input Input HDR RGB float array (3 floats per pixel: R, G, B)
 * @param output Output 8-bit RGB array (3 bytes per pixel: R, G, B)
 * @param num_pixels Number of pixels to process
 * @param params Tone mapping parameters
 */
void tone_map_image(const float* hdr_input, uint8_t* output,
                    uint32_t num_pixels, const ToneMapParams* params);

/**
 * Normalize HDR values based on normalization mode
 * @param r_in Input red value
 * @param g_in Input green value
 * @param b_in Input blue value
 * @param normalize Normalization mode
 * @param r_out Output red value (normalized)
 * @param g_out Output green value (normalized)
 * @param b_out Output blue value (normalized)
 */
void tone_map_normalize(float r_in, float g_in, float b_in,
                        ToneMapNormalize normalize,
                        float* r_out, float* g_out, float* b_out);

#ifdef __cplusplus
}
#endif

#endif /* TONE_MAPPING_H */
