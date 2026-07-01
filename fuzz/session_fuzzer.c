/*
 * Session fuzzer harness for libspanlog.
 * Exercises: protocol, session, lease, retention, span, segment,
 *            replication, checkpoint subsystems.
 */
#include "../include/spanlog.h"
#include "../src/common/types.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < SPL_WIRE_HEADER_SIZE + SPL_WIRE_FRAME_HEADER_SIZE)
        return 0;

    spl_context_t *ctx = spl_context_create();
    if (!ctx) return 0;

    uint32_t caps = SPL_CAP_CHANNEL | SPL_CAP_STREAM | SPL_CAP_CURSOR | SPL_CAP_REPLICATION | SPL_CAP_MIGRATION | SPL_CAP_CHECKPOINT | SPL_CAP_RETENTION | SPL_CAP_BACKPRESSURE | SPL_CAP_CRYPTO;
    spl_session_t *sess = spl_session_create(ctx, caps);
    if (!sess) {
        spl_context_destroy(ctx);
        return 0;
    }

    spl_ingest_frames(sess, data, size);

    spl_session_destroy(sess);
    free(ctx);
    return 0;
}
