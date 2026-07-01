#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Migration state tracker: maintains detailed per-segment
 * transfer progress during migration operations.
 */

typedef enum {
    SPL_XFER_PENDING   = 0,
    SPL_XFER_ACTIVE    = 1,
    SPL_XFER_VERIFYING = 2,
    SPL_XFER_COMPLETE  = 3,
    SPL_XFER_FAILED    = 4
} spl_xfer_state_t;

typedef struct {
    uint32_t        segment_id;
    spl_xfer_state_t state;
    uint64_t        bytes_total;
    uint64_t        bytes_sent;
    uint32_t        checksum;
    uint32_t        retry_count;
    uint32_t        max_retries;
} spl_xfer_segment_t;

typedef struct {
    uint32_t            migration_id;
    spl_xfer_segment_t  segments[SPL_MAX_SEGMENTS];
    uint16_t            segment_count;
    uint16_t            segments_complete;
    uint16_t            segments_failed;
    uint64_t            total_bytes;
    uint64_t            bytes_transferred;
    uint32_t            start_epoch;
    uint32_t            current_epoch;
} spl_migrate_state_tracker_t;

int spl_migrate_state_init(spl_migrate_state_tracker_t *tracker,
                           uint32_t migration_id, spl_span_t *span,
                           uint32_t epoch) {
    if (!tracker || !span) return -1;
    memset(tracker, 0, sizeof(*tracker));

    tracker->migration_id = migration_id;
    tracker->start_epoch = epoch;
    tracker->current_epoch = epoch;

    for (uint16_t i = 0; i < span->segment_count; i++) {
        spl_segment_t *seg = span->segments[i];
        if (!seg || seg->state != SPL_LIFECYCLE_ACTIVE) continue;
        if (tracker->segment_count >= SPL_MAX_SEGMENTS) break;

        spl_xfer_segment_t *xs = &tracker->segments[tracker->segment_count++];
        xs->segment_id = seg->segment_id;
        xs->state = SPL_XFER_PENDING;
        xs->bytes_total = seg->used;
        xs->bytes_sent = 0;
        xs->checksum = spl_crc32c(seg->data, seg->used);
        xs->retry_count = 0;
        xs->max_retries = 3;

        tracker->total_bytes += seg->used;
    }

    return 0;
}

int spl_migrate_state_update_segment(spl_migrate_state_tracker_t *tracker,
                                     uint32_t segment_id,
                                     uint64_t bytes_sent,
                                     spl_xfer_state_t new_state) {
    if (!tracker) return -1;

    for (uint16_t i = 0; i < tracker->segment_count; i++) {
        spl_xfer_segment_t *xs = &tracker->segments[i];
        if (xs->segment_id != segment_id) continue;

        uint64_t delta = bytes_sent - xs->bytes_sent;
        xs->bytes_sent = bytes_sent;
        xs->state = new_state;
        tracker->bytes_transferred += delta;

        if (new_state == SPL_XFER_COMPLETE) tracker->segments_complete++;
        if (new_state == SPL_XFER_FAILED) tracker->segments_failed++;

        return 0;
    }
    return -1;
}

int spl_migrate_state_retry_failed(spl_migrate_state_tracker_t *tracker) {
    if (!tracker) return -1;

    int retried = 0;
    for (uint16_t i = 0; i < tracker->segment_count; i++) {
        spl_xfer_segment_t *xs = &tracker->segments[i];
        if (xs->state != SPL_XFER_FAILED) continue;
        if (xs->retry_count >= xs->max_retries) continue;

        xs->state = SPL_XFER_PENDING;
        xs->bytes_sent = 0;
        xs->retry_count++;
        tracker->segments_failed--;
        retried++;
    }
    return retried;
}

int spl_migrate_state_progress(const spl_migrate_state_tracker_t *tracker) {
    if (!tracker || tracker->total_bytes == 0) return 0;
    return (int)((tracker->bytes_transferred * 100) / tracker->total_bytes);
}

bool spl_migrate_state_is_complete(const spl_migrate_state_tracker_t *tracker) {
    return tracker &&
           tracker->segments_complete + tracker->segments_failed ==
           tracker->segment_count;
}

uint32_t spl_migrate_state_next_pending(const spl_migrate_state_tracker_t *tracker) {
    if (!tracker) return 0;
    for (uint16_t i = 0; i < tracker->segment_count; i++) {
        if (tracker->segments[i].state == SPL_XFER_PENDING)
            return tracker->segments[i].segment_id;
    }
    return 0;
}

uint64_t spl_migrate_state_remaining(const spl_migrate_state_tracker_t *tracker) {
    if (!tracker) return 0;
    if (tracker->bytes_transferred >= tracker->total_bytes) return 0;
    return tracker->total_bytes - tracker->bytes_transferred;
}
