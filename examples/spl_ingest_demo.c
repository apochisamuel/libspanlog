/*
 * Example: basic frame ingestion workflow.
 * Demonstrates session creation, capability negotiation, and span management.
 */
#include "../include/spanlog.h"
#include "../src/common/types.h"
#include "../src/common/util.h"
#include "../src/common/crc.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    printf("libspanlog ingest demo\n");
    printf("======================\n\n");

    /* Create context and session */
    spl_context_t *ctx = spl_context_create();
    uint32_t caps = SPL_CAP_CHANNEL | SPL_CAP_STREAM | SPL_CAP_CURSOR;
    spl_session_t *sess = spl_session_create(ctx, caps);

    printf("Session created: id=%u, epoch=%u\n",
           spl_session_get_id(sess), spl_session_get_epoch(sess));

    /* Build a minimal valid session init + cap_neg message */
    uint8_t msg[512];
    size_t off = 0;

    /* Header */
    uint32_t sid = spl_session_get_id(sess);
    spl_write_u32(msg, SPL_MAGIC);
    spl_write_u16(msg + 4, 0x0100);
    spl_write_u32(msg + 6, sid);
    spl_write_u32(msg + 10, 0);
    spl_write_u16(msg + 14, 1);
    spl_write_u32(msg + 16, spl_crc32c(msg, 14));
    off = SPL_WIRE_HEADER_SIZE;

    /* Session init frame */
    uint16_t frame_len = SPL_WIRE_FRAME_HEADER_SIZE;
    spl_write_u16(msg + off, frame_len);
    msg[off + 2] = SPL_FRAME_SESSION_INIT;
    spl_write_u16(msg + off + 3, 0);
    spl_write_u32(msg + off + 5, sid);
    spl_write_u16(msg + off + 9, 0);
    spl_write_u16(msg + off + 11, 0);
    spl_write_u32(msg + off + 13, 0);
    spl_write_u16(msg + off + 17, 0);
    msg[off + 19] = 0;

    int rc = spl_ingest_frames(sess, msg, off + frame_len);
    printf("Ingested %d frames (session init)\n", rc);
    printf("Session initialized: id=%u\n", spl_session_get_id(sess));

    spl_context_destroy(ctx);
    printf("\nDone.\n");
    return 0;
}
