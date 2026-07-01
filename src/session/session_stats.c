#include "../common/types.h"

/*
 * Session statistics collection and reporting.
 * Tracks frames processed, errors, subsystem utilization.
 */

typedef struct {
    uint64_t frames_processed;
    uint64_t frames_error;
    uint64_t bytes_ingested;
    uint64_t bytes_produced;
    uint32_t spans_active;
    uint32_t segments_active;
    uint32_t channels_active;
    uint32_t streams_active;
    uint32_t cursors_active;
    uint32_t leases_active;
    uint32_t migrations_active;
    uint32_t checkpoints_active;
    uint64_t epoch_advances;
    uint32_t gc_runs;
    uint32_t gc_freed_channels;
    uint32_t gc_freed_spans;
    uint32_t gc_freed_segments;
} spl_session_stats_t;

int spl_session_collect_stats(const spl_session_t *sess,
                              spl_session_stats_t *stats) {
    if (!sess || !stats) return -1;
    memset(stats, 0, sizeof(*stats));

    /* Count active entities */
    for (uint16_t i = 0; i < sess->span_count; i++) {
        if (sess->spans[i] && sess->spans[i]->state == SPL_LIFECYCLE_ACTIVE)
            stats->spans_active++;
    }
    for (uint16_t i = 0; i < sess->segment_count; i++) {
        if (sess->segments[i] && sess->segments[i]->state == SPL_LIFECYCLE_ACTIVE)
            stats->segments_active++;
    }
    for (uint16_t i = 0; i < sess->channel_count; i++) {
        if (sess->channels[i] && sess->channels[i]->state == SPL_LIFECYCLE_ACTIVE)
            stats->channels_active++;
    }
    for (uint16_t i = 0; i < sess->stream_count; i++) {
        if (sess->streams[i] && sess->streams[i]->active)
            stats->streams_active++;
    }
    for (uint16_t i = 0; i < sess->cursor_count; i++) {
        if (sess->cursors[i] && sess->cursors[i]->active)
            stats->cursors_active++;
    }
    for (uint16_t i = 0; i < sess->lease_count; i++) {
        if (sess->leases[i] && sess->leases[i]->active)
            stats->leases_active++;
    }
    for (uint8_t i = 0; i < sess->migration_count; i++) {
        if (sess->migrations[i] && sess->migrations[i]->active)
            stats->migrations_active++;
    }
    for (uint8_t i = 0; i < sess->checkpoint_count; i++) {
        if (sess->checkpoints[i] && sess->checkpoints[i]->active)
            stats->checkpoints_active++;
    }

    /* Calculate total bytes in segments */
    for (uint16_t i = 0; i < sess->segment_count; i++) {
        if (sess->segments[i] && sess->segments[i]->state == SPL_LIFECYCLE_ACTIVE)
            stats->bytes_ingested += sess->segments[i]->used;
    }

    stats->epoch_advances = sess->epoch;
    stats->gc_freed_channels = sess->drain_count;

    return 0;
}

/*
 * Compute memory pressure score: 0-100 where 100 = critical.
 */
int spl_session_memory_pressure(const spl_session_t *sess) {
    if (!sess) return 0;

    int score = 0;

    /* Segment utilization */
    uint64_t total_cap = 0, total_used = 0;
    for (uint16_t i = 0; i < sess->segment_count; i++) {
        if (!sess->segments[i]) continue;
        total_cap += sess->segments[i]->capacity;
        total_used += sess->segments[i]->used;
    }
    if (total_cap > 0) {
        score += (int)((total_used * 40) / total_cap);
    }

    /* Registry occupancy */
    score += (sess->span_count * 15) / SPL_MAX_SPANS;
    score += (sess->channel_count * 15) / SPL_MAX_CHANNELS;
    score += (sess->cursor_count * 15) / SPL_MAX_CURSORS;
    score += (sess->lease_count * 15) / SPL_MAX_LEASES;

    if (score > 100) score = 100;
    return score;
}

/*
 * Compute session health score: 0-1000 where 1000 = healthy.
 */
int spl_session_health_score(const spl_session_t *sess) {
    if (!sess) return 0;
    if (!sess->initialized) return 0;

    int score = 1000;

    /* Penalize for memory pressure */
    int pressure = spl_session_memory_pressure(sess);
    score -= pressure * 3;

    /* Penalize for pending drains */
    score -= (int)sess->drain_count * 20;

    /* Penalize for pending deferred deletes */
    score -= (int)sess->deferred_delete_count * 30;

    /* Penalize for active migrations (instability) */
    for (uint8_t i = 0; i < sess->migration_count; i++) {
        if (sess->migrations[i] && sess->migrations[i]->active)
            score -= 50;
    }

    if (score < 0) score = 0;
    return score;
}
