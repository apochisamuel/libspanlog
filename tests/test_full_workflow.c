#include "../include/spanlog.h"
#include "../src/common/types.h"
#include "../src/common/util.h"
#include "../src/common/crc.h"
#include <stdio.h>
#include <string.h>

static int p = 0, f = 0;
#define T(n) printf("  %s... ", #n)
#define P() do { printf("PASS\n"); p++; } while(0)
#define F(m) do { printf("FAIL: %s\n", m); f++; } while(0)

static void test_full_write_read(void) {
    T(full_write_read_cycle);
    spl_context_t *ctx = spl_context_create();
    uint32_t caps = SPL_CAP_CHANNEL | SPL_CAP_STREAM | SPL_CAP_CURSOR;
    spl_session_t *sess = spl_session_create(ctx, caps);
    sess->initialized = true; sess->negotiated = true;

    spl_span_t *span = spl_span_registry_create(sess, 1000, 0x1234);
    spl_segment_t *seg = spl_segment_alloc(sess, 1000, 4096);

    /* Write pattern data */
    uint8_t pattern[256];
    for (int i = 0; i < 256; i++) pattern[i] = (uint8_t)i;
    spl_segment_write(seg, pattern, 256);

    /* Read back through cursor */
    spl_lease_t *l = spl_lease_acquire(sess, 1000, 0x1234);
    spl_cursor_t *c = spl_cursor_open(sess, 1000, l->lease_id);
    spl_cursor_advance_to_segment(sess, c, 0);

    uint8_t buf[256];
    size_t bytes = 0;
    spl_cursor_materialize(sess, c, buf, 256, &bytes);

    if (bytes == 256 && memcmp(buf, pattern, 256) == 0) P();
    else F("data mismatch");

    spl_context_destroy(ctx);
}

static void test_multi_segment(void) {
    T(multi_segment_sequential);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL | SPL_CAP_CURSOR);
    sess->initialized = true; sess->negotiated = true;

    spl_span_t *span = spl_span_registry_create(sess, 2000, 0x5678);

    /* Create 3 segments with distinct data */
    for (int s = 0; s < 3; s++) {
        spl_segment_t *seg = spl_segment_alloc(sess, 2000, 1024);
        uint8_t data[128];
        memset(data, (uint8_t)(s + 1) * 0x11, 128);
        spl_segment_write(seg, data, 128);
    }

    if (span->segment_count == 3) P(); else F("segment count");

    spl_context_destroy(ctx);
}

static void test_lease_lifecycle(void) {
    T(lease_acquire_renew_release);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;

    spl_span_t *span = spl_span_registry_create(sess, 3000, 0xABCD);
    spl_lease_t *l = spl_lease_acquire(sess, 3000, 0xABCD);
    if (!l || !l->active) { F("acquire"); spl_context_destroy(ctx); return; }

    spl_segment_alloc(sess, 3000, 1024);

    spl_lease_renew(sess, l);
    if (l->renewal_count != 1) { F("renew"); spl_context_destroy(ctx); return; }

    spl_lease_release(sess, l);
    if (l->active) { F("release"); spl_context_destroy(ctx); return; }

    P();
    spl_context_destroy(ctx);
}

static void test_channel_bind_dispatch(void) {
    T(channel_dispatch);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;

    spl_span_t *span = spl_span_registry_create(sess, 4000, 0xBEEF);
    spl_segment_alloc(sess, 4000, 4096);

    spl_channel_t *ch = spl_channel_mux_alloc(sess, 99);
    spl_channel_bind_span(sess, ch, 4000, 0);

    uint8_t data[] = "hello spanlog";
    int rc = spl_channel_dispatch_to_span(sess, ch, data, sizeof(data));
    if (rc == SPL_OK) P(); else F("dispatch");

    spl_context_destroy(ctx);
}

static void test_segment_compact(void) {
    T(segment_compaction);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;

    spl_span_t *span = spl_span_registry_create(sess, 5000, 0xCCCC);

    /* Create 3 small segments */
    for (int i = 0; i < 3; i++) {
        spl_segment_t *seg = spl_segment_alloc(sess, 5000, 256);
        uint8_t data[32];
        memset(data, (uint8_t)(i + 1), 32);
        spl_segment_write(seg, data, 32);
    }

    if (span->segment_count != 3) { F("pre-compact count"); spl_context_destroy(ctx); return; }

    /* Compact */
    int rc = spl_segment_compact(sess, span);
    if (rc == SPL_OK && span->segment_count == 1) P();
    else F("post-compact");

    spl_context_destroy(ctx);
}

int main(void) {
    printf("libspanlog full workflow tests\n");
    printf("===============================\n");

    test_full_write_read();
    test_multi_segment();
    test_lease_lifecycle();
    test_channel_bind_dispatch();
    test_segment_compact();

    printf("\nResults: %d passed, %d failed\n", p, f);
    return f > 0 ? 1 : 0;
}
