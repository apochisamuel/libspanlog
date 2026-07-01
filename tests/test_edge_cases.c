/*
 * Edge case tests for libspanlog protocol handling.
 * Tests boundary conditions, error paths, and unusual sequences.
 */
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

static void test_empty_input(void) {
    T(empty_input);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    int rc = spl_ingest_frames(sess, NULL, 0);
    if (rc <= 0) P(); else F("accepted null");
    spl_context_destroy(ctx);
}

static void test_truncated_header(void) {
    T(truncated_header);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    uint8_t buf[10] = {0};
    int rc = spl_ingest_frames(sess, buf, 10);
    if (rc <= 0) P(); else F("accepted truncated");
    spl_context_destroy(ctx);
}

static void test_bad_magic(void) {
    T(bad_magic);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    uint8_t buf[SPL_WIRE_HEADER_SIZE + SPL_WIRE_FRAME_HEADER_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    int rc = spl_ingest_frames(sess, buf, sizeof(buf));
    if (rc <= 0) P(); else F("accepted bad magic");
    spl_context_destroy(ctx);
}

static void test_bad_crc(void) {
    T(bad_header_crc);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    uint8_t buf[SPL_WIRE_HEADER_SIZE + SPL_WIRE_FRAME_HEADER_SIZE];
    spl_write_u32(buf, SPL_MAGIC);
    spl_write_u16(buf + 4, 0x0100);
    spl_write_u32(buf + 6, 1);
    spl_write_u32(buf + 10, 0);
    spl_write_u16(buf + 14, 1);
    spl_write_u32(buf + 16, 0xDEADBEEF); /* wrong CRC */
    int rc = spl_ingest_frames(sess, buf, sizeof(buf));
    if (rc <= 0) P(); else F("accepted bad CRC");
    spl_context_destroy(ctx);
}

static void test_span_double_create(void) {
    T(span_double_create);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    spl_span_t *s1 = spl_span_registry_create(sess, 42, 0x1111);
    spl_span_t *s2 = spl_span_registry_lookup(sess, 42);
    if (s1 == s2 && s1 != NULL) P(); else F("mismatch");
    spl_context_destroy(ctx);
}

static void test_cursor_no_data(void) {
    T(cursor_materialize_empty);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL | SPL_CAP_CURSOR);
    sess->initialized = true; sess->negotiated = true;
    spl_span_t *span = spl_span_registry_create(sess, 50, 0x2222);
    spl_segment_alloc(sess, 50, 1024);
    /* Don't write any data */
    spl_lease_t *l = spl_lease_acquire(sess, 50, 0x2222);
    spl_cursor_t *c = spl_cursor_open(sess, 50, l->lease_id);
    spl_cursor_advance_to_segment(sess, c, 0);
    uint8_t buf[256];
    size_t bytes = 0;
    int rc = spl_cursor_materialize(sess, c, buf, 256, &bytes);
    if (rc == SPL_OK && bytes == 0) P(); else F("non-zero bytes on empty");
    spl_context_destroy(ctx);
}

static void test_segment_overflow(void) {
    T(segment_write_overflow);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    spl_span_registry_create(sess, 60, 0x3333);
    spl_segment_t *seg = spl_segment_alloc(sess, 60, 32);
    uint8_t data[64];
    memset(data, 0xAA, 64);
    int rc = spl_segment_write(seg, data, 64);
    if (rc != SPL_OK) P(); else F("accepted overflow");
    spl_context_destroy(ctx);
}

static void test_lease_double_release(void) {
    T(lease_double_release);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    spl_span_registry_create(sess, 70, 0x4444);
    spl_segment_alloc(sess, 70, 1024);
    spl_lease_t *l = spl_lease_acquire(sess, 70, 0x4444);
    spl_lease_release(sess, l);
    /* Second release should be safe (no crash) */
    int rc = spl_lease_release(sess, l);
    P(); /* no crash = pass */
    (void)rc;
    spl_context_destroy(ctx);
}

static void test_channel_bind_nonexistent(void) {
    T(channel_bind_nonexistent_span);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    spl_channel_t *ch = spl_channel_mux_alloc(sess, 99);
    int rc = spl_channel_bind_span(sess, ch, 99999, 0);
    if (rc != SPL_OK) P(); else F("bound to nonexistent");
    spl_context_destroy(ctx);
}

static void test_migration_without_cap(void) {
    T(migrate_without_capability);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    spl_span_registry_create(sess, 80, 0x5555);
    int rc = spl_migrate_initiate(sess, 80, 42);
    if (rc != SPL_OK) P(); else F("migrated without cap");
    spl_context_destroy(ctx);
}

static void test_context_null_safety(void) {
    T(null_safety);
    /* All functions should handle NULL gracefully */
    spl_span_registry_lookup(NULL, 1);
    spl_lease_lookup(NULL, 1);
    spl_cursor_lookup(NULL, 1);
    spl_channel_lookup(NULL, 1);
    spl_stream_lookup(NULL, 1);
    spl_migrate_lookup(NULL, 1);
    spl_repl_lookup(NULL, 1);
    spl_checkpoint_lookup(NULL, 1);
    spl_node_lookup(NULL, 1);
    spl_route_lookup(NULL, 1);
    P();
}

static void test_max_capacity(void) {
    T(max_spans_capacity);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    sess->initialized = true; sess->negotiated = true;
    int created = 0;
    for (uint32_t i = 1; i <= SPL_MAX_SPANS + 5; i++) {
        spl_span_t *s = spl_span_registry_create(sess, i, 0x6666);
        if (s) created++;
    }
    if (created == SPL_MAX_SPANS) P();
    else F("capacity not enforced");
    spl_context_destroy(ctx);
}

int main(void) {
    printf("libspanlog edge case tests\n");
    printf("===========================\n");
    test_empty_input();
    test_truncated_header();
    test_bad_magic();
    test_bad_crc();
    test_span_double_create();
    test_cursor_no_data();
    test_segment_overflow();
    test_lease_double_release();
    test_channel_bind_nonexistent();
    test_migration_without_cap();
    test_context_null_safety();
    test_max_capacity();
    printf("\nResults: %d passed, %d failed\n", p, f);
    return f > 0 ? 1 : 0;
}
