#include "../common/types.h"

/*
 * Replication sync protocol finite state machine.
 * Manages the multi-phase sync process between leader and follower.
 *
 * Phases: INIT → NEGOTIATE → SNAPSHOT → STREAM → CAUGHT_UP → ACTIVE
 */

typedef enum {
    SPL_SYNC_INIT      = 0,
    SPL_SYNC_NEGOTIATE = 1,
    SPL_SYNC_SNAPSHOT  = 2,
    SPL_SYNC_STREAM    = 3,
    SPL_SYNC_CAUGHT_UP = 4,
    SPL_SYNC_ACTIVE    = 5,
    SPL_SYNC_ERROR     = 6,
    SPL_SYNC_STALLED   = 7
} spl_sync_phase_t;

typedef struct {
    spl_sync_phase_t phase;
    uint32_t replica_set_id;
    uint32_t leader_id;
    uint32_t follower_id;
    uint64_t leader_offset;
    uint64_t follower_offset;
    uint64_t snapshot_size;
    uint64_t bytes_transferred;
    uint32_t segments_synced;
    uint32_t total_segments;
    uint32_t retries;
    uint32_t max_retries;
    uint32_t stall_epochs;
    uint32_t max_stall_epochs;
    bool     snapshot_complete;
} spl_sync_fsm_t;

int spl_sync_fsm_init(spl_sync_fsm_t *fsm, uint32_t rs_id,
                      uint32_t leader, uint32_t follower) {
    if (!fsm) return -1;
    memset(fsm, 0, sizeof(*fsm));
    fsm->phase = SPL_SYNC_INIT;
    fsm->replica_set_id = rs_id;
    fsm->leader_id = leader;
    fsm->follower_id = follower;
    fsm->max_retries = 3;
    fsm->max_stall_epochs = 10;
    return 0;
}

int spl_sync_fsm_advance(spl_sync_fsm_t *fsm, uint64_t bytes_received,
                         uint32_t segments_done) {
    if (!fsm) return -1;

    fsm->bytes_transferred += bytes_received;
    fsm->segments_synced += segments_done;
    fsm->stall_epochs = 0;

    switch (fsm->phase) {
    case SPL_SYNC_INIT:
        fsm->phase = SPL_SYNC_NEGOTIATE;
        break;

    case SPL_SYNC_NEGOTIATE:
        fsm->phase = SPL_SYNC_SNAPSHOT;
        break;

    case SPL_SYNC_SNAPSHOT:
        if (fsm->segments_synced >= fsm->total_segments) {
            fsm->snapshot_complete = true;
            fsm->phase = SPL_SYNC_STREAM;
        }
        break;

    case SPL_SYNC_STREAM:
        /* Check if follower has caught up to leader */
        if (fsm->follower_offset + bytes_received >= fsm->leader_offset) {
            fsm->phase = SPL_SYNC_CAUGHT_UP;
        }
        fsm->follower_offset += bytes_received;
        break;

    case SPL_SYNC_CAUGHT_UP:
        fsm->phase = SPL_SYNC_ACTIVE;
        break;

    case SPL_SYNC_ACTIVE:
        /* Stay active, keep tracking offsets */
        fsm->follower_offset += bytes_received;
        break;

    case SPL_SYNC_ERROR:
        if (fsm->retries < fsm->max_retries) {
            fsm->retries++;
            fsm->phase = SPL_SYNC_SNAPSHOT;
        }
        break;

    case SPL_SYNC_STALLED:
        fsm->phase = SPL_SYNC_STREAM;
        break;
    }

    return 0;
}

int spl_sync_fsm_on_error(spl_sync_fsm_t *fsm) {
    if (!fsm) return -1;
    fsm->phase = SPL_SYNC_ERROR;
    return 0;
}

int spl_sync_fsm_tick(spl_sync_fsm_t *fsm) {
    if (!fsm) return -1;

    if (fsm->phase == SPL_SYNC_STREAM || fsm->phase == SPL_SYNC_SNAPSHOT) {
        fsm->stall_epochs++;
        if (fsm->stall_epochs > fsm->max_stall_epochs) {
            fsm->phase = SPL_SYNC_STALLED;
        }
    }
    return 0;
}

/*
 * Compute sync progress as percentage.
 */
int spl_sync_progress(const spl_sync_fsm_t *fsm) {
    if (!fsm) return 0;

    switch (fsm->phase) {
    case SPL_SYNC_INIT:      return 0;
    case SPL_SYNC_NEGOTIATE: return 5;
    case SPL_SYNC_SNAPSHOT:
        if (fsm->total_segments == 0) return 10;
        return 10 + (int)((fsm->segments_synced * 40) / fsm->total_segments);
    case SPL_SYNC_STREAM:
        if (fsm->leader_offset == 0) return 50;
        return 50 + (int)((fsm->follower_offset * 40) / fsm->leader_offset);
    case SPL_SYNC_CAUGHT_UP: return 95;
    case SPL_SYNC_ACTIVE:    return 100;
    case SPL_SYNC_ERROR:     return -1;
    case SPL_SYNC_STALLED:   return -2;
    default:                 return 0;
    }
}

bool spl_sync_is_active(const spl_sync_fsm_t *fsm) {
    return fsm && fsm->phase == SPL_SYNC_ACTIVE;
}

uint64_t spl_sync_lag(const spl_sync_fsm_t *fsm) {
    if (!fsm) return UINT64_MAX;
    if (fsm->leader_offset <= fsm->follower_offset) return 0;
    return fsm->leader_offset - fsm->follower_offset;
}
