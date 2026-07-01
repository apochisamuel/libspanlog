#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Stream reassembly finite state machine.
 * Handles fragment ordering, duplicate detection, and gap filling.
 *
 * States: IDLE → BUFFERING → COMPLETE → DELIVERED → IDLE
 *
 * Transitions:
 *   IDLE + first fragment → BUFFERING
 *   BUFFERING + in-order fragment → BUFFERING
 *   BUFFERING + final fragment → COMPLETE
 *   COMPLETE + consume → DELIVERED
 *   DELIVERED → IDLE
 *   BUFFERING + timeout → IDLE (with reset)
 *   BUFFERING + duplicate → BUFFERING (drop)
 *   BUFFERING + gap → BUFFERING (wait for retransmit)
 */

typedef enum {
    SPL_REASM_IDLE       = 0,
    SPL_REASM_BUFFERING  = 1,
    SPL_REASM_COMPLETE   = 2,
    SPL_REASM_DELIVERED  = 3,
    SPL_REASM_ERROR      = 4
} spl_reasm_state_t;

#define SPL_REASM_MAX_FRAGMENTS 64

typedef struct {
    uint32_t seq;
    uint16_t offset;
    uint16_t length;
    bool     received;
    bool     is_final;
} spl_fragment_slot_t;

typedef struct {
    spl_reasm_state_t state;
    uint32_t stream_id;
    uint32_t expected_seq;
    spl_fragment_slot_t fragments[SPL_REASM_MAX_FRAGMENTS];
    uint32_t fragment_count;
    uint32_t received_count;
    uint32_t total_expected;
    uint32_t total_bytes;
    uint32_t gap_count;
    uint32_t duplicate_count;
    uint32_t timeout_count;
    uint32_t start_epoch;
    uint32_t max_wait_epochs;
    bool     final_received;
} spl_reasm_fsm_t;

int spl_reasm_init(spl_reasm_fsm_t *fsm, uint32_t stream_id,
                   uint32_t max_wait) {
    if (!fsm) return -1;
    memset(fsm, 0, sizeof(*fsm));
    fsm->stream_id = stream_id;
    fsm->state = SPL_REASM_IDLE;
    fsm->max_wait_epochs = max_wait > 0 ? max_wait : 5;
    return 0;
}

int spl_reasm_on_fragment(spl_reasm_fsm_t *fsm, uint32_t seq,
                          uint16_t offset, uint16_t length,
                          bool is_final, uint32_t current_epoch) {
    if (!fsm) return -1;

    switch (fsm->state) {
    case SPL_REASM_IDLE:
        fsm->state = SPL_REASM_BUFFERING;
        fsm->expected_seq = seq;
        fsm->start_epoch = current_epoch;
        break;

    case SPL_REASM_BUFFERING:
        /* Check for timeout */
        if (current_epoch - fsm->start_epoch > fsm->max_wait_epochs) {
            fsm->state = SPL_REASM_ERROR;
            fsm->timeout_count++;
            return -1;
        }
        break;

    case SPL_REASM_COMPLETE:
    case SPL_REASM_DELIVERED:
        return -1;  /* can't accept fragments in these states */

    case SPL_REASM_ERROR:
        return -1;
    }

    /* Check for duplicate */
    for (uint32_t i = 0; i < fsm->fragment_count; i++) {
        if (fsm->fragments[i].seq == seq && fsm->fragments[i].received) {
            fsm->duplicate_count++;
            return 0;
        }
    }

    /* Check for gap */
    if (seq > fsm->expected_seq) {
        fsm->gap_count++;
    }

    /* Record fragment */
    if (fsm->fragment_count >= SPL_REASM_MAX_FRAGMENTS) {
        fsm->state = SPL_REASM_ERROR;
        return -1;
    }

    spl_fragment_slot_t *slot = &fsm->fragments[fsm->fragment_count++];
    slot->seq = seq;
    slot->offset = offset;
    slot->length = length;
    slot->received = true;
    slot->is_final = is_final;

    fsm->received_count++;
    fsm->total_bytes += length;

    if (is_final) {
        fsm->final_received = true;
        fsm->total_expected = seq + 1;
    }

    /* Update expected sequence */
    if (seq == fsm->expected_seq)
        fsm->expected_seq = seq + 1;

    /* Check if all fragments received */
    if (fsm->final_received && fsm->received_count >= fsm->total_expected) {
        /* Verify no gaps */
        bool complete = true;
        for (uint32_t s = 0; s < fsm->total_expected; s++) {
            bool found = false;
            for (uint32_t i = 0; i < fsm->fragment_count; i++) {
                if (fsm->fragments[i].seq == s && fsm->fragments[i].received) {
                    found = true;
                    break;
                }
            }
            if (!found) { complete = false; break; }
        }

        if (complete)
            fsm->state = SPL_REASM_COMPLETE;
    }

    return 0;
}

int spl_reasm_consume(spl_reasm_fsm_t *fsm) {
    if (!fsm || fsm->state != SPL_REASM_COMPLETE) return -1;
    fsm->state = SPL_REASM_DELIVERED;
    return 0;
}

int spl_reasm_reset(spl_reasm_fsm_t *fsm) {
    if (!fsm) return -1;
    uint32_t stream_id = fsm->stream_id;
    uint32_t max_wait = fsm->max_wait_epochs;
    uint32_t timeouts = fsm->timeout_count;
    uint32_t dups = fsm->duplicate_count;
    return spl_reasm_init(fsm, stream_id, max_wait);
}

bool spl_reasm_is_complete(const spl_reasm_fsm_t *fsm) {
    return fsm && fsm->state == SPL_REASM_COMPLETE;
}

/*
 * Compute reassembly efficiency: received / (received + gaps + duplicates).
 * Returns 0-1000 (1000 = perfect).
 */
int spl_reasm_efficiency(const spl_reasm_fsm_t *fsm) {
    if (!fsm) return 0;
    uint32_t total = fsm->received_count + fsm->gap_count + fsm->duplicate_count;
    if (total == 0) return 1000;
    return (int)((fsm->received_count * 1000) / total);
}

/*
 * Estimate completion percentage.
 */
int spl_reasm_completion_pct(const spl_reasm_fsm_t *fsm) {
    if (!fsm) return 0;
    if (fsm->total_expected == 0) return 0;
    return (int)((fsm->received_count * 100) / fsm->total_expected);
}
