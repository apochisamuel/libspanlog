/*
 * Example: span migration workflow.
 * Demonstrates span creation, segment allocation, migration
 * initiation, and handoff completion.
 */
#include "../include/spanlog.h"
#include "../src/common/types.h"
#include "../src/common/util.h"
#include "../src/common/crc.h"
#include <stdio.h>
#include <string.h>

static size_t build_msg(uint8_t *buf, uint32_t sid, uint32_t epoch,
                        uint8_t type, uint16_t seq, uint16_t ch_id,
                        uint16_t route_id, uint32_t owner) {
    spl_write_u32(buf, SPL_MAGIC);
    spl_write_u16(buf + 4, 0x0100);
    spl_write_u32(buf + 6, sid);
    spl_write_u32(buf + 10, epoch);
    spl_write_u16(buf + 14, 1);
    spl_write_u32(buf + 16, spl_crc32c(buf, 14));
    size_t off = SPL_WIRE_HEADER_SIZE;

    spl_write_u16(buf + off, SPL_WIRE_FRAME_HEADER_SIZE);
    buf[off + 2] = type;
    spl_write_u16(buf + off + 3, seq);
    spl_write_u32(buf + off + 5, sid);
    spl_write_u16(buf + off + 9, ch_id);
    spl_write_u16(buf + off + 11, route_id);
    spl_write_u32(buf + off + 13, owner);
    spl_write_u16(buf + off + 17, 0);
    buf[off + 19] = 0;

    return off + SPL_WIRE_FRAME_HEADER_SIZE;
}

int main(void) {
    printf("libspanlog migration demo\n");
    printf("=========================\n\n");

    spl_context_t *ctx = spl_context_create();
    uint32_t caps = SPL_CAP_CHANNEL | SPL_CAP_STREAM | SPL_CAP_CURSOR |
                    SPL_CAP_MIGRATION;
    spl_session_t *sess = spl_session_create(ctx, caps);
    uint32_t sid = spl_session_get_id(sess);

    printf("Session %u created with migration capability\n", sid);

    uint8_t buf[512];

    /* Init session */
    size_t len = build_msg(buf, sid, 0, SPL_FRAME_SESSION_INIT, 0, 0, 0, 0);
    spl_ingest_frames(sess, buf, len);
    printf("Session initialized\n");

    /* Manually create span and segment for demo */
    spl_span_t *span = spl_span_registry_create(sess, 200, 0xCAFE);
    printf("Span %u created\n", span->span_id);

    spl_segment_t *seg = spl_segment_alloc(sess, 200, 8192);
    printf("Segment %u attached (%zu bytes capacity)\n",
           seg->segment_id, seg->capacity);

    /* Write some data */
    uint8_t data[256];
    for (int i = 0; i < 256; i++) data[i] = (uint8_t)(i * 37);
    spl_segment_write(seg, data, 256);
    printf("Wrote 256 bytes to segment\n");

    /* Initiate migration */
    sess->negotiated = true;  /* skip cap_neg for demo */
    sess->capabilities |= SPL_CAP_MIGRATION;
    int rc = spl_migrate_initiate(sess, 200, 42);
    printf("Migration initiated: rc=%d, target_node=42\n", rc);

    spl_migration_t *mig = spl_migrate_find_by_span(sess, 200);
    if (mig) {
        printf("Migration %u: phase=%d, span=%u\n",
               mig->migration_id, mig->phase, mig->span_id);
    }

    printf("\nDone. (Migration would continue with prepare, transfer, handoff)\n");

    spl_context_destroy(ctx);
    return 0;
}
