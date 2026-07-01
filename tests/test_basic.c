/*
 * Basic integration tests for libspanlog.
 */
#include "../include/spanlog.h"
#include "../src/common/types.h"
#include "../src/common/crc.h"
#include "../src/common/util.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  %s... ", #name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static void test_version(void) {
    TEST(version_string);
    const char *v = spl_version_string();
    if (v && strcmp(v, "1.4.0") == 0) PASS(); else FAIL("version mismatch");

    TEST(version_numeric);
    uint32_t n = spl_version_numeric();
    if (n == ((1 << 16) | (4 << 8) | 0)) PASS(); else FAIL("numeric mismatch");
}

static void test_crc(void) {
    TEST(crc32c_basic);
    uint8_t data[] = "hello world";
    uint32_t crc = spl_crc32c(data, 11);
    if (crc != 0) PASS(); else FAIL("zero CRC");

    TEST(crc32c_empty);
    uint32_t empty_crc = spl_crc32c(NULL, 0);
    (void)empty_crc;
    PASS();
}

static void test_context(void) {
    TEST(context_create);
    spl_context_t *ctx = spl_context_create();
    if (ctx) PASS(); else { FAIL("NULL ctx"); return; }

    TEST(session_create);
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL | SPL_CAP_STREAM);
    if (sess) PASS(); else { FAIL("NULL session"); spl_context_destroy(ctx); return; }

    TEST(session_id);
    uint32_t id = spl_session_get_id(sess);
    if (id > 0) PASS(); else FAIL("zero session_id");

    TEST(session_caps);
    uint32_t caps = spl_session_get_capabilities(sess);
    if (caps & SPL_CAP_CHANNEL) PASS(); else FAIL("missing channel cap");

    spl_context_destroy(ctx);
}

static void test_util(void) {
    TEST(read_write_u16);
    uint8_t buf[8];
    spl_write_u16(buf, 0x1234);
    if (spl_read_u16(buf) == 0x1234) PASS(); else FAIL("u16 roundtrip");

    TEST(read_write_u32);
    spl_write_u32(buf, 0xDEADBEEF);
    if (spl_read_u32(buf) == 0xDEADBEEF) PASS(); else FAIL("u32 roundtrip");

    TEST(hash_mix);
    uint32_t h = spl_hash_mix(1, 2);
    if (h != 0) PASS(); else FAIL("zero hash");

    TEST(fnv1a);
    uint32_t f = spl_fnv1a((const uint8_t *)"test", 4);
    if (f != 0) PASS(); else FAIL("zero fnv1a");
}

static void test_bitops(void) {
    TEST(next_power_of_2);
    if (spl_next_power_of_2(5) == 8) PASS(); else FAIL("npo2(5)!=8");

    TEST(is_power_of_2);
    if (spl_is_power_of_2(8) && !spl_is_power_of_2(7)) PASS();
    else FAIL("ipo2");

    TEST(popcount);
    if (spl_popcount32(0xFF) == 8) PASS(); else FAIL("popcount");
}

int main(void) {
    printf("libspanlog basic tests\n");
    printf("======================\n");

    test_version();
    test_crc();
    test_context();
    test_util();
    test_bitops();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
