/**
 * Unit tests for plugin_rli.c -- RLI format plugin callbacks.
 *
 * Uses the Unity test framework (ThrowTheSwitch/Unity).
 * Links against rli_test_stubs.c for app function stubs.
 */

#include "unity.h"
#include "plugins/plugin_rli.h"

#include <glib.h>
#include <string.h>

static ImageFormatPlugin plugin;
static bool plugin_ready = false;

/* ======================================================================== */
/* Unity setUp / tearDown                                                   */
/* ======================================================================== */

void setUp(void) {
    if (!plugin_ready) {
        memset(&plugin, 0, sizeof(plugin));
        plugin_ready = plugin_init_rli(NULL, &plugin);
    }
}

void tearDown(void) {}

/* ======================================================================== */
/* plugin_init_rli Tests                                                    */
/* ======================================================================== */

void test_plugin_init_succeeds(void) {
    ImageFormatPlugin p;
    memset(&p, 0, sizeof(p));
    TEST_ASSERT_TRUE(plugin_init_rli(NULL, &p));
}

void test_plugin_init_null_out(void) {
    TEST_ASSERT_FALSE(plugin_init_rli(NULL, NULL));
}

void test_plugin_format_name(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_NOT_NULL(plugin.format_info.name);
    TEST_ASSERT_NOT_NULL(strstr(plugin.format_info.name, "Rasterlab"));
}

void test_plugin_format_extension(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_NOT_NULL(plugin.format_info.extensions);
    TEST_ASSERT_EQUAL_STRING("rli", plugin.format_info.extensions);
}

void test_plugin_supports_alpha(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_TRUE(plugin.format_info.supports_alpha);
}

void test_plugin_supports_layers(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_TRUE(plugin.format_info.supports_layers);
}

void test_plugin_callbacks_set(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_NOT_NULL(plugin.callbacks.can_load);
    TEST_ASSERT_NOT_NULL(plugin.callbacks.load);
    TEST_ASSERT_NOT_NULL(plugin.callbacks.can_save);
    TEST_ASSERT_NOT_NULL(plugin.callbacks.save);
}

/* ======================================================================== */
/* can_load Tests                                                           */
/* ======================================================================== */

void test_can_load_valid_header(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    /* "RLIB" magic = 0x52, 0x4C, 0x49, 0x42 */
    uint8_t header[] = { 0x52, 0x4C, 0x49, 0x42, 0x00, 0x00, 0x00, 0x00 };
    TEST_ASSERT_TRUE(plugin.callbacks.can_load("test.rli", header, sizeof(header)));
}

void test_can_load_valid_header_minimal(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    uint8_t header[] = { 0x52, 0x4C, 0x49, 0x42 };
    TEST_ASSERT_TRUE(plugin.callbacks.can_load("test.rli", header, 4));
}

void test_can_load_invalid_header(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    uint8_t header[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    TEST_ASSERT_FALSE(plugin.callbacks.can_load("test.png", header, sizeof(header)));
}

void test_can_load_too_short(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    uint8_t header[] = { 0x52, 0x4C, 0x49 };
    TEST_ASSERT_FALSE(plugin.callbacks.can_load("test.rli", header, 3));
}

void test_can_load_null_header(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_FALSE(plugin.callbacks.can_load("test.rli", NULL, 0));
}

/* ======================================================================== */
/* can_save Tests                                                           */
/* ======================================================================== */

void test_can_save_rli_extension(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_TRUE(plugin.callbacks.can_save("image.rli"));
}

void test_can_save_rli_uppercase(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_TRUE(plugin.callbacks.can_save("IMAGE.RLI"));
}

void test_can_save_rli_mixed_case(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_TRUE(plugin.callbacks.can_save("Image.Rli"));
}

void test_can_save_png_extension(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_FALSE(plugin.callbacks.can_save("image.png"));
}

void test_can_save_no_extension(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_FALSE(plugin.callbacks.can_save("image"));
}

void test_can_save_null_filename(void) {
    TEST_ASSERT_TRUE(plugin_ready);
    TEST_ASSERT_FALSE(plugin.callbacks.can_save(NULL));
}

/* ======================================================================== */
/* Main                                                                     */
/* ======================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Plugin init */
    RUN_TEST(test_plugin_init_succeeds);
    RUN_TEST(test_plugin_init_null_out);
    RUN_TEST(test_plugin_format_name);
    RUN_TEST(test_plugin_format_extension);
    RUN_TEST(test_plugin_supports_alpha);
    RUN_TEST(test_plugin_supports_layers);
    RUN_TEST(test_plugin_callbacks_set);

    /* can_load */
    RUN_TEST(test_can_load_valid_header);
    RUN_TEST(test_can_load_valid_header_minimal);
    RUN_TEST(test_can_load_invalid_header);
    RUN_TEST(test_can_load_too_short);
    RUN_TEST(test_can_load_null_header);

    /* can_save */
    RUN_TEST(test_can_save_rli_extension);
    RUN_TEST(test_can_save_rli_uppercase);
    RUN_TEST(test_can_save_rli_mixed_case);
    RUN_TEST(test_can_save_png_extension);
    RUN_TEST(test_can_save_no_extension);
    RUN_TEST(test_can_save_null_filename);

    return UNITY_END();
}
