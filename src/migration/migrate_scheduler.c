#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Migration transfer scheduler: estimates bandwidth, plans transfer
 * chunks, and handles priority-based migration ordering.
 */

typedef struct {
    uint32_t migration_id;
    uint32_t span_id;
    uint64_t total_bytes;
    uint64_t bytes_remaining;
    uint32_t priority;    /* higher = more urgent */
    uint32_t chunk_size;
    uint32_t estimated_bw_kbps;
    uint32_t elapsed_ms;
    uint32_t eta_seconds;
} spl_migrate_schedule_t;

#define SPL_MAX_SCHEDULED 32

typedef struct {
    spl_migrate_schedule_t entries[SPL_MAX_SCHEDULED];
    uint8_t count;
    uint64_t total_bandwidth_kbps;
    uint32_t max_concurrent;
} spl_migrate_scheduler_t;

int spl_migrate_sched_init(spl_migrate_scheduler_t *sched,
                           uint64_t bandwidth_kbps,
                           uint32_t max_concurrent) {
    if (!sched) return -1;
    memset(sched, 0, sizeof(*sched));
    sched->total_bandwidth_kbps = bandwidth_kbps;
    sched->max_concurrent = max_concurrent;
    return 0;
}

int spl_migrate_sched_add(spl_migrate_scheduler_t *sched,
                          uint32_t migration_id, uint32_t span_id,
                          uint64_t total_bytes, uint32_t priority) {
    if (!sched || sched->count >= SPL_MAX_SCHEDULED) return -1;

    spl_migrate_schedule_t *e = &sched->entries[sched->count++];
    e->migration_id = migration_id;
    e->span_id = span_id;
    e->total_bytes = total_bytes;
    e->bytes_remaining = total_bytes;
    e->priority = priority;

    /* Estimate bandwidth share and chunk size */
    uint32_t active = 0;
    for (uint8_t i = 0; i < sched->count; i++) {
        if (sched->entries[i].bytes_remaining > 0) active++;
    }
    if (active > sched->max_concurrent) active = sched->max_concurrent;

    uint64_t per_migration_bw = sched->total_bandwidth_kbps / (active > 0 ? active : 1);
    e->estimated_bw_kbps = (uint32_t)per_migration_bw;

    /* Chunk size: target 100ms per chunk */
    e->chunk_size = (uint32_t)((per_migration_bw * 1024) / 10);  /* bytes per 100ms */
    if (e->chunk_size < 4096) e->chunk_size = 4096;
    if (e->chunk_size > 1024 * 1024) e->chunk_size = 1024 * 1024;

    /* ETA in seconds */
    if (per_migration_bw > 0) {
        e->eta_seconds = (uint32_t)((total_bytes * 8) / (per_migration_bw * 1024));
    }

    return 0;
}

int spl_migrate_sched_update(spl_migrate_scheduler_t *sched,
                             uint32_t migration_id,
                             uint64_t bytes_transferred,
                             uint32_t elapsed_ms) {
    if (!sched) return -1;

    for (uint8_t i = 0; i < sched->count; i++) {
        spl_migrate_schedule_t *e = &sched->entries[i];
        if (e->migration_id != migration_id) continue;

        if (bytes_transferred >= e->total_bytes) {
            e->bytes_remaining = 0;
        } else {
            e->bytes_remaining = e->total_bytes - bytes_transferred;
        }
        e->elapsed_ms = elapsed_ms;

        /* Update bandwidth estimate from actual throughput */
        if (elapsed_ms > 0 && bytes_transferred > 0) {
            e->estimated_bw_kbps = (uint32_t)(
                (bytes_transferred * 8) / (elapsed_ms));  /* kbps */
        }

        /* Update ETA */
        if (e->estimated_bw_kbps > 0 && e->bytes_remaining > 0) {
            e->eta_seconds = (uint32_t)(
                (e->bytes_remaining * 8) / (e->estimated_bw_kbps * 1024));
        } else {
            e->eta_seconds = 0;
        }

        return 0;
    }
    return -1;
}

/*
 * Select next migration to run based on priority and ETA.
 * Returns migration_id of highest priority migration.
 */
uint32_t spl_migrate_sched_next(const spl_migrate_scheduler_t *sched) {
    if (!sched || sched->count == 0) return 0;

    uint32_t best_id = 0;
    uint32_t best_priority = 0;
    uint32_t best_eta = UINT32_MAX;

    for (uint8_t i = 0; i < sched->count; i++) {
        const spl_migrate_schedule_t *e = &sched->entries[i];
        if (e->bytes_remaining == 0) continue;

        /* Priority first, then ETA for tiebreaker */
        if (e->priority > best_priority ||
            (e->priority == best_priority && e->eta_seconds < best_eta)) {
            best_id = e->migration_id;
            best_priority = e->priority;
            best_eta = e->eta_seconds;
        }
    }

    return best_id;
}

/*
 * Compute chunk sizes for weighted fair scheduling.
 * Higher priority migrations get proportionally larger chunks.
 */
int spl_migrate_sched_compute_chunks(spl_migrate_scheduler_t *sched) {
    if (!sched) return -1;

    /* Sum priorities */
    uint32_t total_priority = 0;
    uint32_t active = 0;
    for (uint8_t i = 0; i < sched->count; i++) {
        if (sched->entries[i].bytes_remaining > 0) {
            total_priority += sched->entries[i].priority;
            active++;
        }
    }
    if (total_priority == 0 || active == 0) return 0;

    /* Distribute bandwidth proportionally */
    for (uint8_t i = 0; i < sched->count; i++) {
        spl_migrate_schedule_t *e = &sched->entries[i];
        if (e->bytes_remaining == 0) continue;

        uint64_t bw_share = (sched->total_bandwidth_kbps * e->priority) /
                            total_priority;
        e->estimated_bw_kbps = (uint32_t)bw_share;
        e->chunk_size = (uint32_t)((bw_share * 1024) / 10);
        if (e->chunk_size < 4096) e->chunk_size = 4096;
    }

    return (int)active;
}
