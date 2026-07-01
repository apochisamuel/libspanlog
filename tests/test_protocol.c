#include "../include/spanlog.h"
#include "../src/common/types.h"
#include "../src/common/crc.h"
#include "../src/common/util.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;
#define TEST(name) printf("  %s... ", #name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* Build a valid single-frame message */
static size_t make_msg(uint8_t *buf, uint32_t sid, uint32_t epoch,
                       uint8_t type, uint16_t seq, uint16_t ch_id,
                       uint16_t route_id, uint32_t owner, uint16_t flags,
                       uint8_t sub_kind, uint32_t sub_epoch,
                       uint32_t sub_gen, uint32_t sub_lease,
                       const uint8_t *payload, uint16_t plen) {
    /* Header */
    spl_write_u32(buf, SPL_MAGIC);
    spl_write_u16(buf + 4, 0x0100);
    spl_write_u32(buf + 6, sid);
    spl_write_u32(buf + 10, epoch);
    spl_write_u16(buf + 14, 1);
    spl_write_u32(buf + 16, spl_crc32c(buf, 14));
    size_t off = SPL_WIRE_HEADER_SIZE;

    /* Frame */
    uint8_t *f = buf + off;
    size_t sub_total = 0;
    if (sub_kind != 0) {
        /* Build subrecord */
        uint8_t sub[512];
        sub[0] = sub_kind;
        spl_write_u16(sub + 1, plen);
        spl_write_u32(sub + 3, sub_epoch);
        spl_write_u32(sub + 7, sub_gen);
        spl_write_u32(sub + 11, sub_lease);
        if (payload && plen > 0)
            memcpy(sub + SPL_WIRE_SUBRECORD_HEADER_SIZE, payload, plen);
        sub_total = SPL_WIRE_SUBRECORD_HEADER_SIZE + plen;

        uint16_t frame_len = SPL_WIRE_FRAME_HEADER_SIZE + (uint16_t)sub_total + 4;
        spl_write_u16(f, frame_len);
        f[2] = type;
        spl_write_u16(f + 3, seq);
        spl_write_u32(f + 5, sid);
        spl_write_u16(f + 9, ch_id);
        spl_write_u16(f + 11, route_id);
        spl_write_u32(f + 13, owner);
        spl_write_u16(f + 17, flags);
        f[19] = 1;
        memcpy(f + SPL_WIRE_FRAME_HEADER_SIZE, sub, sub_total);
        uint32_t pcrc = spl_crc32c(f + SPL_WIRE_FRAME_HEADER_SIZE, sub_total);
        spl_write_u32(f + SPL_WIRE_FRAME_HEADER_SIZE + sub_total, pcrc);
        return off + frame_len;
    } else {
        uint16_t frame_len = SPL_WIRE_FRAME_HEADER_SIZE;
        spl_write_u16(f, frame_len);
        f[2] = type;
        spl_write_u16(f + 3, seq);
        spl_write_u32(f + 5, sid);
        spl_write_u16(f + 9, ch_id);
        spl_write_u16(f + 11, route_id);
        spl_write_u32(f + 13, owner);
        spl_write_u16(f + 17, flags);
        f[19] = 0;
        return off + frame_len;
    }
}

static void test_header_parse(void) {
    TEST(header_parse_valid);
    uint8_t buf[64];
    spl_write_u32(buf, SPL_MAGIC);
    spl_write_u16(buf + 4, 0x0100);
    spl_write_u32(buf + 6, 42);
    spl_write_u32(buf + 10, 0);
    spl_write_u16(buf + 14, 1);
    spl_write_u32(buf + 16, spl_crc32c(buf, 14));

    spl_parsed_header_t hdr;
    int rc = spl_protocol_parse_header(buf, SPL_WIRE_HEADER_SIZE, &hdr);
    if (rc == SPL_OK && hdr.session_id == 42) PASS(); else FAIL("parse");

    TEST(header_bad_magic);
    buf[0] = 0xFF;
    rc = spl_protocol_parse_header(buf, SPL_WIRE_HEADER_SIZE, &hdr);
    if (rc == SPL_ERR_INVALID_MAGIC) PASS(); else FAIL("magic");
}

static void test_session_lifecycle(void) {
    TEST(session_init_and_negotiate);
    spl_context_t *ctx = spl_context_create();
    uint32_t caps = SPL_CAP_CHANNEL | SPL_CAP_STREAM | SPL_CAP_CURSOR |
                    SPL_CAP_MIGRATION;
    spl_session_t *sess = spl_session_create(ctx, caps);
    uint32_t sid = spl_session_get_id(sess);

    uint8_t buf[2048];
    size_t len;

    /* Init */
    len = make_msg(buf, sid, 0, SPL_FRAME_SESSION_INIT, 0, 0, 0, 0, 0,
                   0, 0, 0, 0, NULL, 0);
    int rc = spl_ingest_frames(sess, buf, len);
    if (rc != 1) { FAIL("init"); spl_context_destroy(ctx); return; }

    /* Cap neg */
    uint8_t cp[4]; spl_write_u32(cp, caps);
    len = make_msg(buf, sid, 0, SPL_FRAME_SESSION_CAP_NEG, 1, 0, 0, 0, 0,
                   SPL_SUB_META, 0, 0, 0, cp, 4);
    rc = spl_ingest_frames(sess, buf, len);
    if (rc == 1) PASS(); else FAIL("cap_neg");

    TEST(span_create_and_lookup);
    uint8_t mp[8]; spl_write_u32(mp, 1); spl_write_u32(mp+4, 0);
    len = make_msg(buf, sid, 0, SPL_FRAME_SPAN_CREATE, 2, 0, 100, 0xBEEF, 0,
                   SPL_SUB_META, 0, 1, 0, mp, 8);
    rc = spl_ingest_frames(sess, buf, len);
    spl_span_t *span = spl_span_registry_lookup(sess, 100);
    if (rc == 1 && span && span->span_id == 100) PASS();
    else FAIL("span");

    TEST(segment_attach);
    uint8_t sp[4]; spl_write_u32(sp, 2048);
    len = make_msg(buf, sid, 0, SPL_FRAME_SEGMENT_ATTACH, 3, 0, 100, 0xBEEF, 0,
                   SPL_SUB_SEGMENT_REF, 0, 1, 0, sp, 4);
    rc = spl_ingest_frames(sess, buf, len);
    if (rc == 1 && span->segment_count >= 1) PASS();
    else FAIL("segment");

    TEST(lease_acquire);
    len = make_msg(buf, sid, 0, SPL_FRAME_LEASE_ACQUIRE, 4, 0, 100, 0xBEEF, 0,
                   0, 0, 0, 0, NULL, 0);
    rc = spl_ingest_frames(sess, buf, len);
    spl_lease_t *l = spl_lease_find_by_span(sess, 100);
    if (rc == 1 && l && l->active) PASS(); else FAIL("lease");

    TEST(channel_open_and_bind);
    len = make_msg(buf, sid, 0, SPL_FRAME_CHANNEL_OPEN, 5, 10, 0, 0, 0,
                   0, 0, 0, 0, NULL, 0);
    rc = spl_ingest_frames(sess, buf, len);
    if (rc != 1) { FAIL("ch_open"); spl_context_destroy(ctx); return; }
    uint8_t bp[4]; spl_write_u32(bp, 1);
    len = make_msg(buf, sid, 0, SPL_FRAME_CHANNEL_BIND, 6, 10, 100, 0xBEEF, 0,
                   SPL_SUB_PARTITION_DESC, 0, 0, 0, bp, 4);
    rc = spl_ingest_frames(sess, buf, len);
    spl_channel_t *ch = spl_channel_lookup(sess, 10);
    if (rc == 1 && ch && ch->span_id == 100) PASS();
    else FAIL("bind");

    TEST(cursor_open_and_advance);
    len = make_msg(buf, sid, 0, SPL_FRAME_CURSOR_OPEN, 7, 0, 100, 0xBEEF, 0,
                   SPL_SUB_CURSOR_POS, 0, 1, l->lease_id, NULL, 0);
    rc = spl_ingest_frames(sess, buf, len);
    if (rc != 1) { FAIL("cursor_open"); spl_context_destroy(ctx); return; }

    /* Write data to segment first */
    if (span->segments[0]) {
        uint8_t td[64]; memset(td, 0xBB, 64);
        spl_segment_write(span->segments[0], td, 64);
    }

    spl_cursor_t *cur = NULL;
    for (uint16_t i = 0; i < sess->cursor_count; i++) {
        if (sess->cursors[i] && sess->cursors[i]->active) {
            cur = sess->cursors[i]; break;
        }
    }
    uint8_t ap[4]; spl_write_u32(ap, 0);
    len = make_msg(buf, sid, 0, SPL_FRAME_CURSOR_ADVANCE, 8, cur ? cur->cursor_id : 1,
                   100, 0xBEEF, 0, SPL_SUB_CURSOR_POS, 0, 1, 0, ap, 4);
    rc = spl_ingest_frames(sess, buf, len);
    if (rc == 1 && cur && cur->cached_segment_data) PASS();
    else FAIL("advance");

    spl_context_destroy(ctx);
}

static void test_crc_roundtrip(void) {
    TEST(crc_deterministic);
    uint8_t d1[] = "hello world";
    uint8_t d2[] = "hello world";
    if (spl_crc32c(d1, 11) == spl_crc32c(d2, 11)) PASS();
    else FAIL("non-deterministic");

    TEST(crc_different_data);
    uint8_t d3[] = "hello worle";
    if (spl_crc32c(d1, 11) != spl_crc32c(d3, 11)) PASS();
    else FAIL("collision");
}

static void test_epoch_advance(void) {
    TEST(epoch_advance_basic);
    spl_context_t *ctx = spl_context_create();
    uint32_t caps = SPL_CAP_CHANNEL | SPL_CAP_STREAM | SPL_CAP_CURSOR |
                    SPL_CAP_MIGRATION;
    spl_session_t *sess = spl_session_create(ctx, caps);
    uint32_t sid = spl_session_get_id(sess);
    uint8_t buf[2048];
    size_t len;

    /* Init + cap neg */
    len = make_msg(buf, sid, 0, SPL_FRAME_SESSION_INIT, 0, 0, 0, 0, 0,
                   0, 0, 0, 0, NULL, 0);
    spl_ingest_frames(sess, buf, len);
    uint8_t cp[4]; spl_write_u32(cp, caps);
    len = make_msg(buf, sid, 0, SPL_FRAME_SESSION_CAP_NEG, 1, 0, 0, 0, 0,
                   SPL_SUB_META, 0, 0, 0, cp, 4);
    spl_ingest_frames(sess, buf, len);

    /* Advance epoch */
    uint8_t ep[4]; spl_write_u32(ep, 1);
    len = make_msg(buf, sid, 0, SPL_FRAME_SESSION_EPOCH_ADV, 2, 0, 0, 0, 0,
                   SPL_SUB_GEN_STAMP, 0, 0, 0, ep, 4);
    int rc = spl_ingest_frames(sess, buf, len);
    if (rc == 1 && spl_session_get_epoch(sess) == 1) PASS();
    else FAIL("epoch");

    spl_context_destroy(ctx);
}

int main(void) {
    printf("libspanlog protocol tests\n");
    printf("=========================\n");

    test_header_parse();
    test_crc_roundtrip();
    test_session_lifecycle();
    test_epoch_advance();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
