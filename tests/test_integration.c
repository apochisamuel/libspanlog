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

/* Helper: build and send one frame */
static int send_frame(spl_session_t *sess, uint32_t sid, uint32_t epoch,
                      uint8_t type, uint16_t seq, uint16_t ch, uint16_t rt,
                      uint32_t own) {
    uint8_t buf[256];
    spl_write_u32(buf, SPL_MAGIC);
    spl_write_u16(buf+4, 0x0100);
    spl_write_u32(buf+6, sid);
    spl_write_u32(buf+10, epoch);
    spl_write_u16(buf+14, 1);
    spl_write_u32(buf+16, spl_crc32c(buf, 14));
    size_t off = SPL_WIRE_HEADER_SIZE;

    spl_write_u16(buf+off, SPL_WIRE_FRAME_HEADER_SIZE);
    buf[off+2] = type;
    spl_write_u16(buf+off+3, seq);
    spl_write_u32(buf+off+5, sid);
    spl_write_u16(buf+off+9, ch);
    spl_write_u16(buf+off+11, rt);
    spl_write_u32(buf+off+13, own);
    spl_write_u16(buf+off+17, 0);
    buf[off+19] = 0;
    return spl_ingest_frames(sess, buf, off + SPL_WIRE_FRAME_HEADER_SIZE);
}

static void test_full_lifecycle(void) {
    T(full_lifecycle);
    spl_context_t *ctx = spl_context_create();
    uint32_t caps = SPL_CAP_CHANNEL | SPL_CAP_STREAM | SPL_CAP_CURSOR |
                    SPL_CAP_MIGRATION;
    spl_session_t *sess = spl_session_create(ctx, caps);
    uint32_t sid = spl_session_get_id(sess);

    /* Init */
    int rc = send_frame(sess, sid, 0, SPL_FRAME_SESSION_INIT, 0, 0, 0, 0);
    if (rc != 1) { F("init"); spl_context_destroy(ctx); return; }

    /* Manually set negotiated for simplicity */
    sess->negotiated = true;

    /* Create span directly */
    spl_span_t *span = spl_span_registry_create(sess, 500, 0xAABB);
    spl_segment_t *seg = spl_segment_alloc(sess, 500, 8192);
    uint8_t data[256];
    memset(data, 0xCC, 256);
    spl_segment_write(seg, data, 256);

    /* Lease */
    spl_lease_t *lease = spl_lease_acquire(sess, 500, 0xAABB);

    /* Channel */
    spl_channel_t *ch = spl_channel_mux_alloc(sess, 77);
    spl_channel_bind_span(sess, ch, 500, 0);

    /* Cursor */
    spl_cursor_t *cur = spl_cursor_open(sess, 500, lease->lease_id);
    spl_cursor_advance_to_segment(sess, cur, 0);

    /* Read through cursor */
    uint8_t buf[256];
    size_t bytes = 0;
    rc = spl_cursor_materialize(sess, cur, buf, 256, &bytes);
    if (rc == SPL_OK && bytes == 256 && memcmp(buf, data, 256) == 0) P();
    else F("materialize");

    spl_context_destroy(ctx);
}

static void test_span_refcount(void) {
    T(refcount);
    spl_context_t *ctx = spl_context_create();
    spl_session_t *sess = spl_session_create(ctx, SPL_CAP_CHANNEL);
    sess->initialized = true;
    sess->negotiated = true;

    spl_span_t *span = spl_span_registry_create(sess, 10, 0x1111);
    if (span->refcount != 1) { F("initial"); spl_context_destroy(ctx); return; }

    spl_span_lifecycle_incref(span);
    if (span->refcount != 2) { F("incref"); spl_context_destroy(ctx); return; }

    spl_lease_t *l = spl_lease_acquire(sess, 10, 0x1111);
    if (span->refcount != 3) { F("lease_incref"); spl_context_destroy(ctx); return; }

    spl_lease_release(sess, l);
    /* After release, refcount should be 2 (creation + incref) */
    spl_span_t *check = spl_span_registry_lookup(sess, 10);
    if (check && check->refcount == 2) P(); else F("decref");

    spl_context_destroy(ctx);
}

static void test_migration_prep(void) {
    T(migration_segment_realloc);
    spl_context_t *ctx = spl_context_create();
    uint32_t caps = SPL_CAP_CHANNEL | SPL_CAP_MIGRATION;
    spl_session_t *sess = spl_session_create(ctx, caps);
    sess->initialized = true;
    sess->negotiated = true;

    spl_span_t *span = spl_span_registry_create(sess, 20, 0x2222);
    spl_segment_t *seg = spl_segment_alloc(sess, 20, 4096);
    uint8_t data[64];
    memset(data, 0xDD, 64);
    spl_segment_write(seg, data, 64);

    uint8_t *old_ptr = seg->data;

    /* Initiate migration */
    spl_migrate_initiate(sess, 20, 99);
    spl_migration_t *mig = spl_migrate_find_by_span(sess, 20);

    /* Prepare source (reallocates segments) */
    spl_migrate_prepare_source(sess, mig);

    /* Data pointer should have changed */
    if (seg->data != old_ptr) P();
    else F("realloc didn't change pointer");

    spl_context_destroy(ctx);
}

int main(void) {
    printf("libspanlog integration tests\n");
    printf("============================\n");

    test_full_lifecycle();
    test_span_refcount();
    test_migration_prep();

    printf("\nResults: %d passed, %d failed\n", p, f);
    return f > 0 ? 1 : 0;
}
