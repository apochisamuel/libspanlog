#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Session drain coordinator: orchestrates graceful shutdown
 * of all session resources in dependency order.
 *
 * Drain order: streams → cursors → channels → leases →
 *              checkpoints → migrations → replicas → segments → spans
 */

typedef enum {
    SPL_DRAIN_STREAMS     = 0,
    SPL_DRAIN_CURSORS     = 1,
    SPL_DRAIN_CHANNELS    = 2,
    SPL_DRAIN_LEASES      = 3,
    SPL_DRAIN_CHECKPOINTS = 4,
    SPL_DRAIN_MIGRATIONS  = 5,
    SPL_DRAIN_REPLICAS    = 6,
    SPL_DRAIN_SEGMENTS    = 7,
    SPL_DRAIN_SPANS       = 8,
    SPL_DRAIN_COMPLETE    = 9
} spl_drain_phase_t;

typedef struct {
    spl_drain_phase_t phase;
    uint32_t items_remaining;
    uint32_t items_drained;
    uint32_t errors;
    bool     force_mode;
} spl_drain_state_t;

int spl_drain_init(spl_drain_state_t *state, bool force) {
    if (!state) return -1;
    memset(state, 0, sizeof(*state));
    state->phase = SPL_DRAIN_STREAMS;
    state->force_mode = force;
    return 0;
}

int spl_drain_step(spl_session_t *sess, spl_drain_state_t *state) {
    if (!sess || !state) return -1;
    if (state->phase == SPL_DRAIN_COMPLETE) return 0;

    state->items_remaining = 0;
    state->items_drained = 0;

    switch (state->phase) {
    case SPL_DRAIN_STREAMS:
        for (uint16_t i = 0; i < sess->stream_count; i++) {
            if (sess->streams[i] && sess->streams[i]->active) {
                spl_stream_close(sess, sess->streams[i]);
                state->items_drained++;
            }
        }
        state->phase = SPL_DRAIN_CURSORS;
        break;

    case SPL_DRAIN_CURSORS:
        for (uint16_t i = 0; i < sess->cursor_count; i++) {
            if (sess->cursors[i] && sess->cursors[i]->active) {
                spl_cursor_close(sess, sess->cursors[i]);
                state->items_drained++;
            }
        }
        state->phase = SPL_DRAIN_CHANNELS;
        break;

    case SPL_DRAIN_CHANNELS:
        for (uint16_t i = 0; i < sess->channel_count; i++) {
            if (sess->channels[i] &&
                sess->channels[i]->state == SPL_LIFECYCLE_ACTIVE) {
                sess->channels[i]->state = SPL_LIFECYCLE_DRAINING;
                if (sess->drain_count < SPL_MAX_CHANNELS) {
                    sess->drain_list[sess->drain_count++] = sess->channels[i];
                }
                state->items_drained++;
            }
        }
        state->phase = SPL_DRAIN_LEASES;
        break;

    case SPL_DRAIN_LEASES:
        for (uint16_t i = 0; i < sess->lease_count; i++) {
            if (sess->leases[i] && sess->leases[i]->active) {
                spl_lease_force_release(sess, sess->leases[i]);
                state->items_drained++;
            }
        }
        state->phase = SPL_DRAIN_CHECKPOINTS;
        break;

    case SPL_DRAIN_CHECKPOINTS:
        for (uint8_t i = 0; i < sess->checkpoint_count; i++) {
            if (sess->checkpoints[i] && sess->checkpoints[i]->active) {
                sess->checkpoints[i]->active = false;
                if (sess->checkpoints[i]->listener_active)
                    spl_checkpoint_cleanup_listener(sess, sess->checkpoints[i]);
                state->items_drained++;
            }
        }
        state->phase = SPL_DRAIN_MIGRATIONS;
        break;

    case SPL_DRAIN_MIGRATIONS:
        for (uint8_t i = 0; i < sess->migration_count; i++) {
            if (sess->migrations[i] && sess->migrations[i]->active) {
                spl_migrate_abort(sess, sess->migrations[i]);
                state->items_drained++;
            }
        }
        state->phase = SPL_DRAIN_REPLICAS;
        break;

    case SPL_DRAIN_REPLICAS:
        for (uint8_t i = 0; i < sess->replica_set_count; i++) {
            if (sess->replica_sets[i] && sess->replica_sets[i]->active) {
                sess->replica_sets[i]->active = false;
                sess->replica_sets[i]->snap_active = false;
                state->items_drained++;
            }
        }
        state->phase = SPL_DRAIN_SEGMENTS;
        break;

    case SPL_DRAIN_SEGMENTS:
        /* GC all segments */
        spl_session_gc_drained_channels(sess);
        state->phase = SPL_DRAIN_SPANS;
        break;

    case SPL_DRAIN_SPANS:
        for (uint16_t i = 0; i < sess->span_count; i++) {
            if (sess->spans[i] && sess->spans[i]->state == SPL_LIFECYCLE_ACTIVE) {
                spl_cursor_invalidate_for_span(sess, sess->spans[i]->span_id);
                state->items_drained++;
            }
        }
        state->phase = SPL_DRAIN_COMPLETE;
        break;

    case SPL_DRAIN_COMPLETE:
        break;
    }

    return (int)state->items_drained;
}

int spl_drain_run_all(spl_session_t *sess, spl_drain_state_t *state) {
    if (!sess || !state) return -1;

    int total = 0;
    while (state->phase != SPL_DRAIN_COMPLETE) {
        int rc = spl_drain_step(sess, state);
        if (rc < 0) return rc;
        total += rc;
    }
    return total;
}

bool spl_drain_is_complete(const spl_drain_state_t *state) {
    return state && state->phase == SPL_DRAIN_COMPLETE;
}

const char *spl_drain_phase_name(spl_drain_phase_t phase) {
    switch (phase) {
    case SPL_DRAIN_STREAMS:     return "streams";
    case SPL_DRAIN_CURSORS:     return "cursors";
    case SPL_DRAIN_CHANNELS:    return "channels";
    case SPL_DRAIN_LEASES:      return "leases";
    case SPL_DRAIN_CHECKPOINTS: return "checkpoints";
    case SPL_DRAIN_MIGRATIONS:  return "migrations";
    case SPL_DRAIN_REPLICAS:    return "replicas";
    case SPL_DRAIN_SEGMENTS:    return "segments";
    case SPL_DRAIN_SPANS:       return "spans";
    case SPL_DRAIN_COMPLETE:    return "complete";
    default:                    return "unknown";
    }
}
