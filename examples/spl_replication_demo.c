/*
 * Example: replication workflow.
 * Demonstrates replica set setup, member management, quorum checking,
 * snapshot-based sync, and health monitoring.
 */
#include "../include/spanlog.h"
#include "../src/common/types.h"
#include "../src/common/util.h"
#include "../src/common/crc.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("libspanlog replication demo\n");
    printf("===========================\n\n");

    spl_context_t *ctx = spl_context_create();
    uint32_t caps = SPL_CAP_CHANNEL | SPL_CAP_STREAM | SPL_CAP_CURSOR |
                    SPL_CAP_REPLICATION | SPL_CAP_MIGRATION;
    spl_session_t *sess = spl_session_create(ctx, caps);
    sess->initialized = true;
    sess->negotiated = true;

    printf("Session %u created\n", spl_session_get_id(sess));

    /* Create span with data */
    spl_span_t *span = spl_span_registry_create(sess, 300, 0xAAAA);
    spl_segment_t *seg = spl_segment_alloc(sess, 300, 16384);
    uint8_t data[1024];
    for (int i = 0; i < 1024; i++) data[i] = (uint8_t)(i * 7);
    spl_segment_write(seg, data, 1024);
    printf("Span %u: 1 segment, %zu bytes\n", span->span_id, seg->used);

    /* Add nodes */
    spl_route_add_node(sess, 1, "node-1.local", 5000);
    spl_route_add_node(sess, 2, "node-2.local", 5000);
    spl_route_add_node(sess, 3, "node-3.local", 5000);
    printf("Added 3 nodes to cluster\n");

    /* Setup replica set */
    int rc = spl_repl_setup(sess, 300, 2);  /* quorum of 2 */
    printf("Replica set created: rc=%d\n", rc);

    spl_replica_set_t *rs = spl_repl_find_by_span(sess, 300);
    if (rs) {
        spl_repl_add_member(sess, rs->replica_set_id, 1);
        spl_repl_add_member(sess, rs->replica_set_id, 2);
        spl_repl_add_member(sess, rs->replica_set_id, 3);
        printf("Added 3 members, quorum=%d\n", rs->quorum_size);

        /* Check quorum */
        rc = spl_repl_check_quorum(sess, rs);
        printf("Quorum check: %s\n", rc == SPL_OK ? "PASSED" : "FAILED");

        /* Snapshot segments for sync */
        rc = spl_repl_snapshot_segments(sess, rs);
        printf("Snapshot taken: %d segments\n", rs->snap_segment_count);

        /* Serialize a segment */
        rc = spl_repl_serialize_segment(sess, rs, 0);
        printf("Serialized segment 0: rc=%d\n", rc);
    }

    printf("\nDemo complete.\n");
    spl_context_destroy(ctx);
    return 0;
}
