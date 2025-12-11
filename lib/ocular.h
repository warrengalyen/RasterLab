#ifndef OCULAR_H
#define OCULAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/* Status codes for filter functions */
typedef enum {
    OC_STATUS_OK = 0,                   /* Normal, no errors */
    OC_STATUS_ERR_OUTOFMEMORY = 1,      /* Out of memory */
    OC_STATUS_ERR_STACKOVERFLOW = 2,     /* Stack overflow */
    OC_STATUS_ERR_NULLREFERENCE = 3,     /* Empty reference */
    OC_STATUS_ERR_INVALIDPARAMETER = 4,  /* The parameters are not within the normal range */
    OC_STATUS_ERR_PARAMISMATCH = 5,     /* Parameter mismatch */
    OC_STATUS_ERR_INDEXOUTOFRANGE = 6,
    OC_STATUS_ERR_NOTSUPPORTED = 7,
    OC_STATUS_ERR_OVERFLOW = 8,
    OC_STATUS_ERR_FILENOTFOUND = 9,
    OC_STATUS_ERR_UNKNOWN = 10,
} OC_STATUS;

/* Image data structure */
typedef struct {
    int Width;
    int Height;
    int Stride;
    int Channels;
    int Depth;
    unsigned char* Data;
    int Reserved;
} OcImage;

/* Color structure */
typedef struct {
    unsigned char R;
    unsigned char G;
    unsigned char B;
} OcColor;

/* Edge handling mode */
typedef enum {
    OC_EDGE_WRAP = 0,    /* repeat edge pixel */
    OC_EDGE_MIRROR = 1,  /* mirror edge pixel */
    OC_EDGE_CLAMP = 2,   /* clamp edge pixel */
} OcEdgeMode;

/* Blend modes */
typedef enum {
    OC_BLEND_NORMAL,
    OC_BLEND_DISSOLVE,
    OC_BLEND_DARKEN,
    OC_BLEND_MULTIPLY,
    OC_BLEND_COLORBURN,
    OC_BLEND_LINEARBURN,
    OC_BLEND_DARK,
    OC_BLEND_LIGHTEN,
    OC_BLEND_SCREEN,
    OC_BLEND_COLORDODGE,
    OC_BLEND_LINEARDODGE,
    OC_BLEND_LIGHTERCOLOR,
    OC_BLEND_OVERLAY,
    OC_BLEND_SOFTLIGHT,
    OC_BLEND_HARDLIGHT,
    OC_BLEND_VIVIDLIGHT,
    OC_BLEND_LINEARLIGHT,
    OC_BLEND_PINLIGHT,
    OC_BLEND_HARDMIX,
    OC_BLEND_DIFFERENCE,
    OC_BLEND_EXCLUSION,
    OC_BLEND_SUBTRACT,
    OC_BLEND_DIVIDE,
    OC_BLEND_HUE,
    OC_BLEND_SATURATION,
    OC_BLEND_COLOR,
    OC_BLEND_LUMINOSITY
} OcBlendMode;

/* Interpolation modes */
typedef enum {
    OC_INTERPOLATION_NEAREST,
    OC_INTERPOLATION_BILINEAR,
    OC_INTERPOLATION_BICUBIC,
    OC_INTERPOLATION_LANCZOS
} OcInterpolationMode;

/* Tone balance mode */
typedef enum {
    OC_TONE_SHADOWS,
    OC_TONE_MIDTONES,
    OC_TONE_HIGHLIGHTS
} OcToneBalanceMode;

/* Retinex mode */
typedef enum {
    OC_RETINEX_UNIFORM,
    OC_RETINEX_LOW,
    OC_RETINEX_HIGH
} OcRetinexMode;

/* Spherize mode */
typedef enum {
    OC_SPHERIZE_NORMAL,
    OC_SPHERIZE_HORIZONTAL,
    OC_SPHERIZE_VERTICAL
} OcSpherizeMode;

/* Polar mode */
typedef enum {
    OC_POLAR_RECT_TO_POLAR,
    OC_POLAR_POLAR_TO_RECT
} OcPolarMode;

/* Wave type */
typedef enum {
    OC_WAVE_SINE,
    OC_WAVE_TRIANGLE,
    OC_WAVE_SQUARE,
    OC_WAVE_SAWTOOTH
} OcWaveType;

/* Quantize method */
typedef enum {
    OC_QUANTIZE_MEDIAN_CUT,
    OC_QUANTIZE_OCTREE,
    OC_QUANTIZE_KMEANS
} OcQuantizeMethod;

/* Dither method */
typedef enum {
    OC_DITHER_NONE,
    OC_DITHER_BURKES,
    OC_DITHER_FLOYD_STEINBERG,
    OC_DITHER_STUCKI,
    OC_DITHER_ATKINSON,
    OC_DITHER_SIERRA,
    OC_DITHER_SIERRA_TWO_ROW,
    OC_DITHER_SIERRA_LITE,
    OC_DITHER_JJN,
    OC_DITHER_SINGLE_NEIGHBOR,
    OC_DITHER_BAYER_4X4,
    OC_DITHER_BAYER_8X8,
} OcDitherMethod;

/* Canny noise filter */
typedef enum {
    CannyGaus3x3,
    CannyGaus5x5
} CannyNoiseFilter;

/* Parameters for Levels filter */
typedef struct {
    int levelMinimum;    /* color level minimum */
    int levelMiddle;     /* color scale median */
    int levelMaximum;    /* maximum value of color scale */
    int minOutput;       /* minimum output value */
    int maxOutput;       /* maximum output value */
    bool Enable;         /* whether to apply */
} ocularLevelParams;

/* Palette structures */
typedef struct {
    int r;
    int g;
    int b;
    char name[256];
} OcPaletteColor;

typedef struct {
    char name[256];
    int num_colors;
    int capacity;
    OcPaletteColor* colors;
} OcPalette;

/* FFT Filter Parameters */
typedef struct {
    float cutoff;
    float order;
    bool highPass;
    bool bandPass;
    float centerFreq;
    float bandwidth;
} OcFFTFilterParams;

/* Cloud Parameters */
typedef struct {
    int seed;
    float scale;
    int octaves;
    float persistence;
    float lacunarity;
    float offsetX;
    float offsetY;
} CloudParams;

/* ============================================================================
 * Color Adjustment Functions
 * ============================================================================ */

OC_STATUS ocularBrightnessAndContrastFilter(unsigned char *Input, unsigned char *Output, int Width,
                                            int Height, int Stride, float brightness, float contrast);

OC_STATUS ocularGrayscaleFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride);

OC_STATUS ocularRGBFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float redAdjustment,
                         float greenAdjustment, float blueAdjustment);

OC_STATUS ocularHSLFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float hueAdjustment,
                         float satAdjustment, float lightAdjustment);

OC_STATUS ocularAverageLuminanceThresholdFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride,
                                               float thresholdMultiplier);

OC_STATUS ocularAverageColor(unsigned char* Input, int Width, int Height, int Stride, unsigned char* AverageR,
                            unsigned char* AverageG, unsigned char* AverageB, unsigned char* AverageA);

OC_STATUS ocularLuminosity(unsigned char* Input, int Width, int Height, int Stride, unsigned char* Luminance);

OC_STATUS ocularColorMatrixFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride,
                                 float* colorMatrix, float intensity);

OC_STATUS ocularSepiaFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int intensity);

OC_STATUS ocularChromaKeyFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride,
                               unsigned char colorToReplaceR, unsigned char colorToReplaceG, unsigned char colorToReplaceB,
                               float thresholdSensitivity, float smoothing);

OC_STATUS ocularLookupFilter(unsigned char* Input, unsigned char* Output, unsigned char* lookupTable, int Width, int Height,
                            int Stride, int intensity);

OC_STATUS ocularSaturationFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float saturation);

OC_STATUS ocularGammaFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float gamma[]);

OC_STATUS ocularExposureFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float exposure);

OC_STATUS ocularFalseColorFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride,
                                unsigned char firstColorR, unsigned char firstColorG, unsigned char firstColorB,
                                unsigned char secondColorR, unsigned char secondColorG, unsigned char secondColorB, int intensity);

OC_STATUS ocularHazeFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float distance,
                          float slope, int intensity);

OC_STATUS ocularOpacityFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float opacity);

OC_STATUS ocularLevelsFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride,
                            ocularLevelParams* redLevelParams, ocularLevelParams* greenLevelParams, ocularLevelParams* blueLevelParams);

OC_STATUS ocularHueFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float hueAdjust);

OC_STATUS ocularHighlightShadowTintFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float shadowTintR,
                                         float shadowTintG, float shadowTintB, float highlightTintR, float highlightTintG,
                                         float highlightTintB, float shadowTintIntensity, float highlightTintIntensity);

OC_STATUS ocularHighlightShadowFilter(unsigned char *Input, unsigned char *Output, int Width, int Height, int Stride, float shadows,
                                      float highlights, float midtoneContrast);

OC_STATUS ocularMonochromeFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride,
                                unsigned char filterColorR, unsigned char filterColorG, unsigned char filterColorB, int intensity);

OC_STATUS ocularColorInvertFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride);

OC_STATUS ocularSolidColorGenerator(unsigned char* Output, int Width, int Height, int Stride, unsigned char colorR,
                                unsigned char colorG, unsigned char colorB, unsigned char colorAlpha);

OC_STATUS ocularLuminanceThresholdFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, unsigned char threshold);

bool ocularAutoWhiteBalance(unsigned char* input, unsigned char* output, int Width, int height, int channels, int stride,
                           int colorCoeff, float cutLimit, float contrast);

OC_STATUS ocularWhiteBalanceFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float temperature, float tint);

OC_STATUS ocularVibranceFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float vibrance);

OC_STATUS ocularSkinToneFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float skinToneAdjust,
                               float skinHue, float skinHueThreshold, float maxHueShift, float maxSaturationShift, int upperSkinToneColor);

OC_STATUS ocularAutoLevel(const unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float fraction);

OC_STATUS ocularAutoContrast(unsigned char* Input, unsigned char* Output, int Width, int Height, int Channels);

OC_STATUS ocularAutoGammaCorrection(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride);

OC_STATUS ocularEqualizeFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride);

OC_STATUS ocularHistogramStretch(uint8_t* input, uint8_t* output, int width, int height, int channels);

OC_STATUS ocularAutoThreshold(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride);

OC_STATUS ocularBacklightRepair(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride);

OC_STATUS ocularLayerBlend(unsigned char* baseInput, int bWidth, int bHeight, int bStride, unsigned char* mixInput, int mWidth,
                           int mHeight, int mStride, OcBlendMode blendMode, int alpha);

OC_STATUS ocularColorBalance(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, 
                            int redBalance, int greenBalance, int blueBalance, OcToneBalanceMode Mode, bool preserveLuminosity);

OC_STATUS ocularColorTemperature(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride,
                                 float Temperature, float Strength);

OC_STATUS ocularMultiscaleRetinex(unsigned char* input, unsigned char* output, int width, int height, int channels,
                                 OcRetinexMode mode, int scale, int numScales, float dynamic);

OC_STATUS ocularDarkChannelPriorHazeRemoval(unsigned char* Input, unsigned char* Output, 
                                            int Width, int Height, int Stride,
                                            int radius, int guideRadius, float maxAtm, float omega, float epsilon, float t0);

/* ============================================================================
 * Image Processing Functions
 * ============================================================================ */

OC_STATUS ocularConvolution2DFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Channels,
                                   float* kernel, unsigned char filterW, unsigned char cfactor, unsigned char bias);

OC_STATUS ocularMotionBlurFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int Distance, int Angle);

OC_STATUS ocularRadialBlur(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int centerX,
                           int centerY, int intensity);

OC_STATUS ocularZoomBlur(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int sampleRadius, float blurAmount,
                         int centerX, int centerY);

OC_STATUS ocularAverageBlur(const unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int Radius);

OC_STATUS ocularMedianBlur(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int Radius);

OC_STATUS ocularExponentialBlur(unsigned char* Input, unsigned char* Output, int Width, int Height, int Channels, float Radius);

OC_STATUS ocularErodeFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int Radius);

OC_STATUS ocularDilateFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int Radius);

OC_STATUS ocularMinFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int Radius);

OC_STATUS ocularMaxFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int Radius);

OC_STATUS ocularHighPassFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int Radius);

OC_STATUS ocularBilateralFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride,
                               float sigmaSpatial, float sigmaRange);

OC_STATUS ocularGaussianBlurFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float GaussianSigma);

OC_STATUS ocularUnsharpMaskFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride,
                                 float GaussianSigma, float intensity, float threshold);

OC_STATUS ocularBoxBlurFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int Radius);

OC_STATUS ocularSurfaceBlurFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int Radius, int Threshold);

OC_STATUS ocularBEEPSFilter(const unsigned char* Input, unsigned char* Output, int width, int height, int Stride,
                          float PhotometricStandardDeviation, float SpatialDecay, int RangeFilter);

OC_STATUS ocularGuidedFilter(unsigned char* Input, unsigned char* Guide, unsigned char* Output, int Width, int Height, 
                            int Stride, int Radius, float Epsilon);

OC_STATUS ocularSkinSmoothingFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride,
                                   int smoothingLevel, bool applySkinFilter);

OC_STATUS ocularSharpenFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float Strength);

OC_STATUS ocularResamplingFilter(unsigned char* Input, int Width, int Height, int Stride, unsigned char* Output,
                                int newWidth, int newHeight, int dstStride, OcInterpolationMode InterpolationMode);

OC_STATUS ocularRotateImage(unsigned char* Input, int Width, int Height, int Stride, unsigned char* Output, 
                            unsigned char newWidth, unsigned char newHeight, float angle, bool useTransparency,
                            OcInterpolationMode InterpolationMode, unsigned char fillColorR, unsigned char fillColorG, 
                            unsigned char fillColorB);

OC_STATUS ocularCropImage(const unsigned char* Input, int Width, int Height, int srcStride, unsigned char* Output, int cropX,
                         int cropY, int dstWidth, int dstHeight, int dstStride);

OC_STATUS ocularFlipImage(unsigned char* Input, unsigned char* Output, int Width, int Height, int Channels, int type);

OC_STATUS ocularTransposeImage(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride);

OC_STATUS ocularDespeckle(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int maxWindowSize, int Threshold);

bool ocularDocumentDeskew(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride);

OC_STATUS ocularCannyEdgeDetect(const unsigned char* Input, unsigned char* Output, int Width, int Height, int Channels,
                               CannyNoiseFilter kernel_size, int weak_threshold, int strong_threshold);

OC_STATUS ocularSobelEdgeFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Channels);

OC_STATUS ocularGradientEdgeDetect(unsigned char* Input, unsigned char* Output, int Width, int Height, int Channels);

OC_STATUS ocularMosaicFilter(const unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int blockSize);

OC_STATUS ocularOilPaintFilter(const unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int radius, int intensity);

OC_STATUS ocularFrostedGlassEffect(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, int Radius, int Range);

OC_STATUS ocularFilmGrainEffect(unsigned char* Input, unsigned char* Output, int Width, int Height, int Channels, float Strength, float Softness);

OC_STATUS ocularPosterizeFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Channels, int Levels);

OC_STATUS ocularReliefFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, float Angle, int Offset);

OC_STATUS ocularFragmentFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride);

OC_STATUS ocularCrystallizeFilter(unsigned char* input, unsigned char* output, int width, int height, int stride, int cellSize);

OC_STATUS ocularPointillizeFilter(unsigned char* input, unsigned char* output, int width, int height, int stride, int cellSize, 
                                  unsigned char bgR, unsigned char bgG, unsigned char bgB);

OC_STATUS ocularColorHalftoneFilter(unsigned char* input, unsigned char* output, int width, int height, int stride,
                                     int radius, float dotDensity,
                                     float cyanAngle, float magentaAngle, float yellowAngle);

OC_STATUS ocularPalettetizeFromFile(unsigned char* input, unsigned char* output, int width, int height, int channels,
                                    const char* filename, OcDitherMethod method, int amount);

OC_STATUS ocularPalettetizeFromImage(unsigned char* input, unsigned char* output, int width, int height, int channels,
                                    OcQuantizeMethod method, int maxColors, int amount);

OC_STATUS ocularFFTFilter(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, OcFFTFilterParams* params);

OC_STATUS ocularFFTVisualize(unsigned char* Input, unsigned char* Output, int Width, int Height, int Stride, bool logScale);

int ocularHoughLineDetection(unsigned char* Input, int Width, int Height, int lineIntensity, int Threshold, float resTheta,
                            int numLine, float* Radius, float* Theta);

OC_STATUS ocularDrawLine(unsigned char* canvas, int width, int height, int stride, int x1, int y1, int x2, int y2,
                         unsigned char R, unsigned char G, unsigned char B);

/* ============================================================================
 * Distortion Functions
 * ============================================================================ */

OC_STATUS ocularPinchDistortionFilter(unsigned char* input, unsigned char* output, 
                                      int width, int height, int stride, 
                                      float amount);

OC_STATUS ocularTwirlDistortionFilter(unsigned char* input, unsigned char* output,
                                      int width, int height, int stride,
                                      float angle);

OC_STATUS ocularRippleDistortionFilter(unsigned char* input, unsigned char* output,
                                       int width, int height, int stride,
                                       float wavelength, float amplitude,
                                       float centerX, float centerY,
                                       float radiusPercentage, float phase);

OC_STATUS ocularSpherizeDistortionFilter(unsigned char* input, unsigned char* output,
                                         int width, int height, int stride,
                                         int amount, OcSpherizeMode mode);

OC_STATUS ocularPolarCoordinatesFilter(unsigned char* input, unsigned char* output,
                                       int width, int height, int stride,
                                       OcPolarMode mode);

OC_STATUS ocularWaveDistortionFilter(unsigned char* input, unsigned char* output,
                                     int width, int height, int stride,
                                     int numGenerators,
                                     int minWavelength, int maxWavelength,
                                     int minAmplitude, int maxAmplitude,
                                     int scaleX, int scaleY,
                                     OcWaveType waveType, unsigned int seed);

OC_STATUS ocularKaleidoscopeFilter(unsigned char* input, unsigned char* output,
                                   int width, int height, int stride,
                                   int mirrors, float angle, float angle2,
                                   float centerX, float centerY, float radius);

/* ============================================================================
 * Render Functions
 * ============================================================================ */

OC_STATUS ocularRenderClouds(unsigned char* Input, unsigned char* Output, int Width, int Height, int Channels, const CloudParams* params);

/* ============================================================================
 * Pre-Image Processing Functions
 * ============================================================================ */

bool ocularGetImageSize(const char* file_path, int* width, int* height, int* file_size);

/* ============================================================================
 * File Processing Functions
 * ============================================================================ */

OC_STATUS ocularLoadPalette(const char* filename, OcPalette* palette);

void ocularFreePalette(OcPalette* palette);

void read_gimp_palette(const char* filename, OcPalette* palette_data);

void save_gimp_palette(const char* filename, const OcPalette* palette);

void read_riff_palette(const char* filename, OcPalette* palette);

void save_riff_palette(const char* filename, const OcPalette* palette);

void read_aco_palette(const char* filename, OcPalette* palette);

void save_aco_palette(const char* filename, const OcPalette* palette);

void read_paintnet_palette(const char* filename, OcPalette* palette_data);

void save_paintnet_palette(const char* filename, const OcPalette* palette);

void read_act_palette(const char* filename, OcPalette* palette);

void save_act_palette(const char* filename, const OcPalette* palette);

void read_ase_palette(const char* filename, OcPalette* palette);

/* ============================================================================
 * General Functions
 * ============================================================================ */

OcImage* ocularCreateImage(int width, int height, int channels);

void ocularFreeImage(OcImage** image);

void ocularCloneImage(OcImage* Input, OcImage** Output);

#ifdef __cplusplus
}
#endif

#endif  /* OCULAR_H */

