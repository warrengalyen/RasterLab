/**
 * Unit tests for rli_tile_codec.c -- tile-based pixel encoding/decoding.
 *
 * Uses the Unity test framework (ThrowTheSwitch/Unity).
 */

#include "unity.h"
#include "plugins/rli_codec.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================== */
/* Unity setUp / tearDown                                                   */
/* ======================================================================== */

void setUp(void)  {}
void tearDown(void) {}

/* ======================================================================== */
/* Helpers                                                                  */
/* ======================================================================== */

/**
 * Allocate a tile buffer with RLI_LITERALS_PRE_PADDING and fill with
 * the "previous pixel" sentinel (0,0,0,0xFF).
 */
static uint8_t*
alloc_tile_src(uint32_t tw, uint32_t th) {
    size_t sz = RLI_LITERALS_PRE_PADDING + (4 * (size_t)tw * th);
    uint8_t* buf = (uint8_t*)g_malloc0(sz);
    buf[0] = 0x00;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0xFF;
    return buf;
}

static uint8_t*
pixel_ptr(uint8_t* tile_buf, uint32_t idx) {
    return tile_buf + RLI_LITERALS_PRE_PADDING + (4 * idx);
}

static void
set_pixel(uint8_t* tile_buf, uint32_t idx, uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    uint8_t* p = pixel_ptr(tile_buf, idx);
    p[0] = b;
    p[1] = g;
    p[2] = r;
    p[3] = a;
}

/**
 * Perform a round-trip encode then decode of a tile and verify exact match.
 */
static void
assert_tile_roundtrip(uint8_t* src, uint32_t tw, uint32_t th) {
    size_t n_pixels = (size_t)tw * th;
    size_t ops_buf_size = (5 * n_pixels) + 64;
    uint8_t* ops = (uint8_t*)g_malloc(ops_buf_size);

    size_t ops_len = rli_encode_tile_ops(ops, src, tw, th);
    TEST_ASSERT_GREATER_THAN(0, ops_len);

    size_t dst_size = RLI_LITERALS_PRE_PADDING + (4 * n_pixels);
    uint8_t* dst = (uint8_t*)g_malloc0(dst_size);
    dst[0] = 0x00;
    dst[1] = 0x00;
    dst[2] = 0x00;
    dst[3] = 0xFF;

    size_t dec = rli_decode_tile_ops(dst, dst_size, ops, ops_len + 8);
    TEST_ASSERT_EQUAL_size_t(dst_size, dec);

    TEST_ASSERT_EQUAL_MEMORY(
        src + RLI_LITERALS_PRE_PADDING,
        dst + RLI_LITERALS_PRE_PADDING,
        4 * n_pixels);

    g_free(ops);
    g_free(dst);
}

/* ======================================================================== */
/* Tile Op Encode/Decode Round-Trip Tests                                   */
/* ======================================================================== */

void test_tile_solid_color(void) {
    uint32_t tw = RLI_TILE_SIZE, th = RLI_TILE_SIZE;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t i = 0; i < tw * th; i++)
        set_pixel(src, i, 0x40, 0x80, 0xC0, 0xFF);
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_solid_black(void) {
    uint32_t tw = RLI_TILE_SIZE, th = RLI_TILE_SIZE;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t i = 0; i < tw * th; i++)
        set_pixel(src, i, 0x00, 0x00, 0x00, 0xFF);
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_gradient(void) {
    uint32_t tw = RLI_TILE_SIZE, th = RLI_TILE_SIZE;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t y = 0; y < th; y++)
        for (uint32_t x = 0; x < tw; x++) {
            uint32_t idx = y * tw + x;
            uint8_t val = (uint8_t)((x * 255) / (tw - 1));
            set_pixel(src, idx, val, val, val, 0xFF);
        }
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_color_gradient(void) {
    uint32_t tw = RLI_TILE_SIZE, th = RLI_TILE_SIZE;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t y = 0; y < th; y++)
        for (uint32_t x = 0; x < tw; x++) {
            uint32_t idx = y * tw + x;
            set_pixel(src, idx,
                      (uint8_t)((x * 255) / (tw - 1)),
                      (uint8_t)((y * 255) / (th - 1)),
                      (uint8_t)(((x + y) * 127) / (tw + th - 2)),
                      0xFF);
        }
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_checkerboard(void) {
    uint32_t tw = RLI_TILE_SIZE, th = RLI_TILE_SIZE;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t y = 0; y < th; y++)
        for (uint32_t x = 0; x < tw; x++) {
            uint32_t idx = y * tw + x;
            if ((x ^ y) & 1)
                set_pixel(src, idx, 0xFF, 0xFF, 0xFF, 0xFF);
            else
                set_pixel(src, idx, 0x00, 0x00, 0x00, 0xFF);
        }
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_random(void) {
    uint32_t tw = RLI_TILE_SIZE, th = RLI_TILE_SIZE;
    uint8_t* src = alloc_tile_src(tw, th);
    GRand* rng = g_rand_new_with_seed(42);
    for (uint32_t i = 0; i < tw * th; i++)
        set_pixel(src, i,
                  (uint8_t)(g_rand_int(rng) & 0xFF),
                  (uint8_t)(g_rand_int(rng) & 0xFF),
                  (uint8_t)(g_rand_int(rng) & 0xFF),
                  (uint8_t)(g_rand_int(rng) & 0xFF));
    assert_tile_roundtrip(src, tw, th);
    g_rand_free(rng);
    g_free(src);
}

void test_tile_all_transparent(void) {
    uint32_t tw = RLI_TILE_SIZE, th = RLI_TILE_SIZE;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t i = 0; i < tw * th; i++)
        set_pixel(src, i, 0x00, 0x00, 0x00, 0x00);
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_alpha_varying(void) {
    uint32_t tw = RLI_TILE_SIZE, th = RLI_TILE_SIZE;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t y = 0; y < th; y++)
        for (uint32_t x = 0; x < tw; x++) {
            uint32_t idx = y * tw + x;
            uint8_t a = (uint8_t)((idx * 255) / (tw * th - 1));
            set_pixel(src, idx, 0x80, 0x80, 0x80, a);
        }
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_alpha_only_delta(void) {
    uint32_t tw = 8, th = 8;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t i = 0; i < tw * th; i++)
        set_pixel(src, i, 0x40, 0x60, 0x80, (uint8_t)(i * 4));
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_partial(void) {
    uint32_t tw = 30, th = 20;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t y = 0; y < th; y++)
        for (uint32_t x = 0; x < tw; x++) {
            uint32_t idx = y * tw + x;
            set_pixel(src, idx,
                      (uint8_t)(x * 8),
                      (uint8_t)(y * 12),
                      (uint8_t)((x + y) * 5),
                      0xFF);
        }
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_single_pixel(void) {
    uint32_t tw = 1, th = 1;
    uint8_t* src = alloc_tile_src(tw, th);
    set_pixel(src, 0, 0xAA, 0xBB, 0xCC, 0xDD);
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_long_run(void) {
    uint32_t tw = RLI_TILE_SIZE, th = RLI_TILE_SIZE;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t i = 0; i < tw * th; i++)
        set_pixel(src, i, 0xDE, 0xAD, 0xBE, 0xEF);
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_small_bgr_delta(void) {
    uint32_t tw = 16, th = 16;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t i = 0; i < tw * th; i++) {
        uint8_t base = (uint8_t)(128 + (i & 1));
        set_pixel(src, i, base, (uint8_t)(base + 1), (uint8_t)(base - 1), 0xFF);
    }
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_two_color_alternating(void) {
    uint32_t tw = RLI_TILE_SIZE, th = RLI_TILE_SIZE;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t i = 0; i < tw * th; i++) {
        if (i & 1)
            set_pixel(src, i, 0x10, 0x20, 0x30, 0xFF);
        else
            set_pixel(src, i, 0xA0, 0xB0, 0xC0, 0xFF);
    }
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_premultiplied_alpha_values(void) {
    uint32_t tw = 16, th = 16;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t i = 0; i < tw * th; i++) {
        uint8_t a = (uint8_t)((i * 255) / (tw * th - 1));
        uint8_t r = (uint8_t)((uint32_t)200 * a / 255);
        uint8_t g = (uint8_t)((uint32_t)100 * a / 255);
        uint8_t b = (uint8_t)((uint32_t)50 * a / 255);
        set_pixel(src, i, b, g, r, a);
    }
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_wide_1xN(void) {
    uint32_t tw = RLI_TILE_SIZE, th = 1;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t x = 0; x < tw; x++)
        set_pixel(src, x, (uint8_t)(x * 4), (uint8_t)(x * 3), (uint8_t)(x * 2), 0xFF);
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

void test_tile_tall_Nx1(void) {
    uint32_t tw = 1, th = RLI_TILE_SIZE;
    uint8_t* src = alloc_tile_src(tw, th);
    for (uint32_t y = 0; y < th; y++)
        set_pixel(src, y, (uint8_t)(y * 4), (uint8_t)(y * 3), (uint8_t)(y * 2), 0xFF);
    assert_tile_roundtrip(src, tw, th);
    g_free(src);
}

/* ======================================================================== */
/* Full-Layer Encode/Decode Round-Trip Tests                                */
/* ======================================================================== */

static void
assert_layer_roundtrip(uint32_t width, uint32_t height, uint32_t stride) {
    size_t total_bytes = (size_t)stride * height;
    uint8_t* src = (uint8_t*)g_malloc0(total_bytes);

    GRand* rng = g_rand_new_with_seed(width * 1000 + height);
    for (uint32_t y = 0; y < height; y++)
        for (uint32_t x = 0; x < width; x++) {
            size_t off = (size_t)y * stride + (size_t)x * 4;
            src[off + 0] = (uint8_t)(g_rand_int(rng) & 0xFF);
            src[off + 1] = (uint8_t)(g_rand_int(rng) & 0xFF);
            src[off + 2] = (uint8_t)(g_rand_int(rng) & 0xFF);
            src[off + 3] = (uint8_t)(g_rand_int(rng) & 0xFF);
        }
    g_rand_free(rng);

    size_t enc_len = 0;
    uint8_t* enc = rli_encode_pixel_data(src, width, height, stride, &enc_len);
    TEST_ASSERT_NOT_NULL(enc);
    TEST_ASSERT_GREATER_THAN(0, enc_len);

    uint8_t* dst = (uint8_t*)g_malloc0(total_bytes);
    gboolean ok = rli_decode_pixel_data(enc, enc_len, dst, width, height, stride);
    TEST_ASSERT_TRUE(ok);

    for (uint32_t y = 0; y < height; y++) {
        TEST_ASSERT_EQUAL_MEMORY(
            src + (size_t)y * stride,
            dst + (size_t)y * stride,
            (size_t)width * 4);
    }

    g_free(enc);
    g_free(dst);
    g_free(src);
}

void test_layer_single_tile(void) {
    assert_layer_roundtrip(32, 32, 32 * 4);
}

void test_layer_exact_tile(void) {
    assert_layer_roundtrip(64, 64, 64 * 4);
}

void test_layer_multi_tile(void) {
    assert_layer_roundtrip(200, 150, 200 * 4);
}

void test_layer_non_aligned(void) {
    assert_layer_roundtrip(100, 100, 100 * 4);
}

void test_layer_with_stride_padding(void) {
    uint32_t width = 100, height = 80;
    uint32_t stride = width * 4 + 32;
    assert_layer_roundtrip(width, height, stride);
}

void test_layer_1x1(void) {
    assert_layer_roundtrip(1, 1, 4);
}

void test_layer_wide(void) {
    assert_layer_roundtrip(300, 1, 300 * 4);
}

void test_layer_tall(void) {
    assert_layer_roundtrip(1, 300, 4);
}

void test_layer_solid_transparent(void) {
    uint32_t width = 128, height = 128;
    uint32_t stride = width * 4;
    size_t total = (size_t)stride * height;
    uint8_t* src = (uint8_t*)g_malloc0(total);

    size_t enc_len = 0;
    uint8_t* enc = rli_encode_pixel_data(src, width, height, stride, &enc_len);
    TEST_ASSERT_NOT_NULL(enc);

    uint8_t* dst = (uint8_t*)g_malloc0(total);
    gboolean ok = rli_decode_pixel_data(enc, enc_len, dst, width, height, stride);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_MEMORY(src, dst, total);

    g_free(enc);
    g_free(dst);
    g_free(src);
}

/* ======================================================================== */
/* Error Handling Tests                                                     */
/* ======================================================================== */

void test_encode_null_pixels(void) {
    size_t out_len = 0;
    uint8_t* result = rli_encode_pixel_data(NULL, 64, 64, 256, &out_len);
    TEST_ASSERT_NULL(result);
}

void test_encode_zero_width(void) {
    uint8_t dummy[256];
    size_t out_len = 0;
    uint8_t* result = rli_encode_pixel_data(dummy, 0, 64, 256, &out_len);
    TEST_ASSERT_NULL(result);
}

void test_encode_zero_height(void) {
    uint8_t dummy[256];
    size_t out_len = 0;
    uint8_t* result = rli_encode_pixel_data(dummy, 64, 0, 256, &out_len);
    TEST_ASSERT_NULL(result);
}

void test_encode_null_out_len(void) {
    uint8_t dummy[256];
    uint8_t* result = rli_encode_pixel_data(dummy, 64, 64, 256, NULL);
    TEST_ASSERT_NULL(result);
}

void test_decode_null_data(void) {
    uint8_t dummy[256];
    gboolean ok = rli_decode_pixel_data(NULL, 100, dummy, 8, 8, 32);
    TEST_ASSERT_FALSE(ok);
}

void test_decode_null_pixels(void) {
    uint8_t dummy[256];
    gboolean ok = rli_decode_pixel_data(dummy, 100, NULL, 8, 8, 32);
    TEST_ASSERT_FALSE(ok);
}

void test_decode_zero_dimensions(void) {
    uint8_t dummy_data[256];
    uint8_t dummy_pix[256];
    TEST_ASSERT_FALSE(rli_decode_pixel_data(dummy_data, 100, dummy_pix, 0, 8, 32));
    TEST_ASSERT_FALSE(rli_decode_pixel_data(dummy_data, 100, dummy_pix, 8, 0, 32));
}

/* ======================================================================== */
/* Tile Grid Helper Tests                                                   */
/* ======================================================================== */

void test_tiles_1d(void) {
    TEST_ASSERT_EQUAL_UINT32(1, rli_tiles_1d(1));
    TEST_ASSERT_EQUAL_UINT32(1, rli_tiles_1d(64));
    TEST_ASSERT_EQUAL_UINT32(2, rli_tiles_1d(65));
    TEST_ASSERT_EQUAL_UINT32(2, rli_tiles_1d(128));
    TEST_ASSERT_EQUAL_UINT32(4, rli_tiles_1d(200));
}

void test_tiles_2d(void) {
    TEST_ASSERT_EQUAL_UINT64(1, rli_tiles_2d(64, 64));
    TEST_ASSERT_EQUAL_UINT64(1, rli_tiles_2d(1, 1));
    TEST_ASSERT_EQUAL_UINT64(4, rli_tiles_2d(128, 128));
    TEST_ASSERT_EQUAL_UINT64(12, rli_tiles_2d(200, 150));
}

/* ======================================================================== */
/* Main                                                                     */
/* ======================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Tile op round-trips */
    RUN_TEST(test_tile_solid_color);
    RUN_TEST(test_tile_solid_black);
    RUN_TEST(test_tile_gradient);
    RUN_TEST(test_tile_color_gradient);
    RUN_TEST(test_tile_checkerboard);
    RUN_TEST(test_tile_random);
    RUN_TEST(test_tile_all_transparent);
    RUN_TEST(test_tile_alpha_varying);
    RUN_TEST(test_tile_alpha_only_delta);
    RUN_TEST(test_tile_partial);
    RUN_TEST(test_tile_single_pixel);
    RUN_TEST(test_tile_long_run);
    RUN_TEST(test_tile_small_bgr_delta);
    RUN_TEST(test_tile_two_color_alternating);
    RUN_TEST(test_tile_premultiplied_alpha_values);
    RUN_TEST(test_tile_wide_1xN);
    RUN_TEST(test_tile_tall_Nx1);

    /* Full-layer round-trips */
    RUN_TEST(test_layer_single_tile);
    RUN_TEST(test_layer_exact_tile);
    RUN_TEST(test_layer_multi_tile);
    RUN_TEST(test_layer_non_aligned);
    RUN_TEST(test_layer_with_stride_padding);
    RUN_TEST(test_layer_1x1);
    RUN_TEST(test_layer_wide);
    RUN_TEST(test_layer_tall);
    RUN_TEST(test_layer_solid_transparent);

    /* Error handling */
    RUN_TEST(test_encode_null_pixels);
    RUN_TEST(test_encode_zero_width);
    RUN_TEST(test_encode_zero_height);
    RUN_TEST(test_encode_null_out_len);
    RUN_TEST(test_decode_null_data);
    RUN_TEST(test_decode_null_pixels);
    RUN_TEST(test_decode_zero_dimensions);

    /* Grid helpers */
    RUN_TEST(test_tiles_1d);
    RUN_TEST(test_tiles_2d);

    return UNITY_END();
}
