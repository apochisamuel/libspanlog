#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Batch frame processing: buffers multiple frames and processes them
 * as an atomic unit. Implements two-phase commit for multi-frame
 * operations that must succeed or fail together.
 */

#define SPL_BATCH_MAX_FRAMES 64

typedef enum {
    SPL_BATCH_IDLE     = 0,
    SPL_BATCH_ACTIVE   = 1,
    SPL_BATCH_PREPARED = 2,
    SPL_BATCH_COMMITTED = 3,
    SPL_BATCH_ROLLED_BACK = 4
} spl_batch_phase_t;

typedef struct {
    spl_parsed_frame_t frames[SPL_BATCH_MAX_FRAMES];
    uint32_t           frame_count;
    spl_batch_phase_t  phase;
    uint32_t           batch_id;
    uint32_t           epoch_at_start;
    uint32_t           span_count_at_start;
    uint32_t           segment_count_at_start;
    uint32_t           channel_count_at_start;
    bool               rollback_needed;
} spl_batch_ctx_t;

static uint32_t next_batch_id = 1;

int spl_batch_begin(spl_batch_ctx_t *batch, spl_session_t *sess) {
    if (!batch || !sess) return -1;
    memset(batch, 0, sizeof(*batch));
    batch->batch_id = next_batch_id++;
    batch->phase = SPL_BATCH_ACTIVE;
    batch->epoch_at_start = sess->epoch;
    batch->span_count_at_start = sess->span_count;
    batch->segment_count_at_start = sess->segment_count;
    batch->channel_count_at_start = sess->channel_count;
    return 0;
}

int spl_batch_add_frame(spl_batch_ctx_t *batch, const spl_parsed_frame_t *frame) {
    if (!batch || !frame) return -1;
    if (batch->phase != SPL_BATCH_ACTIVE) return -1;
    if (batch->frame_count >= SPL_BATCH_MAX_FRAMES) return -1;
    batch->frames[batch->frame_count++] = *frame;
    return 0;
}

int spl_batch_prepare(spl_batch_ctx_t *batch, spl_session_t *sess) {
    if (!batch || !sess) return -1;
    if (batch->phase != SPL_BATCH_ACTIVE) return -1;

    /* Validate all frames can be applied */
    for (uint32_t i = 0; i < batch->frame_count; i++) {
        const spl_parsed_frame_t *f = &batch->frames[i];

        /* Basic validation checks */
        if (f->frame_type == 0) {
            batch->rollback_needed = true;
            return -1;
        }

        /* Check ordering constraints */
        if (i > 0) {
            const spl_parsed_frame_t *prev = &batch->frames[i - 1];
            if (f->sequence <= prev->sequence && f->sequence != 0) {
                batch->rollback_needed = true;
                return -1;
            }
        }
    }

    batch->phase = SPL_BATCH_PREPARED;
    return 0;
}

int spl_batch_commit(spl_batch_ctx_t *batch, spl_session_t *sess) {
    if (!batch || !sess) return -1;
    if (batch->phase != SPL_BATCH_PREPARED) return -1;

    /* Execute all frames */
    int processed = 0;
    for (uint32_t i = 0; i < batch->frame_count; i++) {
        int rc = spl_frame_dispatch(sess, &batch->frames[i]);
        if (rc != SPL_OK) {
            batch->rollback_needed = true;
            batch->phase = SPL_BATCH_ROLLED_BACK;
            return -(int)(i + 1);
        }
        processed++;
    }

    batch->phase = SPL_BATCH_COMMITTED;
    return processed;
}

int spl_batch_rollback(spl_batch_ctx_t *batch, spl_session_t *sess) {
    if (!batch || !sess) return -1;

    /* Restore session state to pre-batch values.
     * Note: this is a best-effort rollback — some side effects
     * may not be fully reversible (e.g., segment allocations). */
    sess->epoch = batch->epoch_at_start;

    batch->phase = SPL_BATCH_ROLLED_BACK;
    return 0;
}

/*
 * Compute batch complexity score: estimates solver difficulty
 * based on the number and types of frames in the batch.
 */
int spl_batch_complexity_score(const spl_batch_ctx_t *batch) {
    if (!batch) return 0;

    int score = 0;
    uint32_t unique_types = 0;
    uint8_t type_seen[256] = {0};

    for (uint32_t i = 0; i < batch->frame_count; i++) {
        uint8_t ft = batch->frames[i].frame_type;
        if (!type_seen[ft]) {
            type_seen[ft] = 1;
            unique_types++;
        }

        /* Score based on subsystem crossings */
        switch (ft) {
        case SPL_FRAME_SESSION_EPOCH_ADV:
            score += 10;  /* epoch advance crosses many subsystems */
            break;
        case SPL_FRAME_MIGRATE_INITIATE:
        case SPL_FRAME_MIGRATE_PREPARE:
            score += 8;
            break;
        case SPL_FRAME_CHECKPOINT_BEGIN:
        case SPL_FRAME_REPL_SYNC_BEGIN:
            score += 6;
            break;
        case SPL_FRAME_CHANNEL_BIND:
        case SPL_FRAME_CURSOR_ADVANCE:
            score += 4;
            break;
        default:
            score += 2;
        }

        /* Subrecord complexity */
        score += batch->frames[i].subrecord_count * 2;
    }

    /* Diversity bonus */
    score += (int)unique_types * 5;

    return score;
}
