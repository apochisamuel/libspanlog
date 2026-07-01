#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Replication log: ordered sequence of operations for replication.
 * Each entry has a term, index, and operation type.
 * Used for leader election and log replication (Raft-inspired).
 */

#define SPL_REPL_LOG_MAX 4096

typedef enum {
    SPL_LOG_NOOP         = 0,
    SPL_LOG_SPAN_CREATE  = 1,
    SPL_LOG_SPAN_CLOSE   = 2,
    SPL_LOG_SEG_ATTACH   = 3,
    SPL_LOG_SEG_DETACH   = 4,
    SPL_LOG_DATA_APPEND  = 5,
    SPL_LOG_LEASE_GRANT  = 6,
    SPL_LOG_LEASE_REVOKE = 7,
    SPL_LOG_CONFIG_CHANGE = 8
} spl_log_op_t;

typedef struct {
    uint64_t    index;
    uint32_t    term;
    spl_log_op_t op;
    uint32_t    target_id;   /* span_id, lease_id, etc. */
    uint32_t    data_hash;   /* hash of operation data */
    uint32_t    data_size;
    bool        committed;
    bool        applied;
} spl_log_entry_t;

typedef struct {
    spl_log_entry_t entries[SPL_REPL_LOG_MAX];
    uint64_t        first_index;
    uint64_t        last_index;
    uint64_t        commit_index;
    uint64_t        apply_index;
    uint32_t        current_term;
    uint32_t        voted_for;
    uint32_t        leader_id;
    uint32_t        entry_count;
} spl_repl_log_t;

int spl_repl_log_init(spl_repl_log_t *log) {
    if (!log) return -1;
    memset(log, 0, sizeof(*log));
    log->first_index = 1;
    log->last_index = 0;
    log->current_term = 1;
    return 0;
}

int spl_repl_log_append(spl_repl_log_t *log, uint32_t term,
                        spl_log_op_t op, uint32_t target_id,
                        uint32_t data_hash, uint32_t data_size) {
    if (!log || log->entry_count >= SPL_REPL_LOG_MAX) return -1;

    uint64_t idx = log->last_index + 1;
    uint32_t slot = (uint32_t)(idx % SPL_REPL_LOG_MAX);

    spl_log_entry_t *e = &log->entries[slot];
    e->index = idx;
    e->term = term;
    e->op = op;
    e->target_id = target_id;
    e->data_hash = data_hash;
    e->data_size = data_size;
    e->committed = false;
    e->applied = false;

    log->last_index = idx;
    log->entry_count++;
    return 0;
}

spl_log_entry_t *spl_repl_log_get(const spl_repl_log_t *log, uint64_t index) {
    if (!log) return NULL;
    if (index < log->first_index || index > log->last_index) return NULL;
    uint32_t slot = (uint32_t)(index % SPL_REPL_LOG_MAX);
    return (spl_log_entry_t *)&log->entries[slot];
}

int spl_repl_log_commit(spl_repl_log_t *log, uint64_t up_to_index) {
    if (!log) return -1;
    if (up_to_index > log->last_index) up_to_index = log->last_index;

    for (uint64_t i = log->commit_index + 1; i <= up_to_index; i++) {
        spl_log_entry_t *e = spl_repl_log_get(log, i);
        if (e) e->committed = true;
    }
    log->commit_index = up_to_index;
    return 0;
}

int spl_repl_log_apply(spl_repl_log_t *log) {
    if (!log) return -1;

    int applied = 0;
    while (log->apply_index < log->commit_index) {
        log->apply_index++;
        spl_log_entry_t *e = spl_repl_log_get(log, log->apply_index);
        if (e) {
            e->applied = true;
            applied++;
        }
    }
    return applied;
}

/*
 * Truncate log after a given index (for conflict resolution).
 */
int spl_repl_log_truncate(spl_repl_log_t *log, uint64_t after_index) {
    if (!log) return -1;
    if (after_index >= log->last_index) return 0;

    for (uint64_t i = after_index + 1; i <= log->last_index; i++) {
        spl_log_entry_t *e = spl_repl_log_get(log, i);
        if (e) {
            memset(e, 0, sizeof(*e));
            log->entry_count--;
        }
    }
    log->last_index = after_index;
    return 0;
}

/*
 * Check if log entries match between leader and follower.
 * Used for AppendEntries RPC validation.
 */
bool spl_repl_log_match(const spl_repl_log_t *log, uint64_t prev_index,
                        uint32_t prev_term) {
    if (!log) return false;
    if (prev_index == 0) return true;  /* empty log matches */
    if (prev_index > log->last_index) return false;

    spl_log_entry_t *e = spl_repl_log_get(log, prev_index);
    return e && e->term == prev_term;
}

/*
 * Vote tracking for leader election.
 */
typedef struct {
    uint32_t candidate_id;
    uint32_t term;
    uint32_t votes_received;
    uint32_t votes_needed;
    bool     granted[SPL_MAX_NODES];
    uint8_t  voter_count;
} spl_vote_tracker_t;

int spl_vote_init(spl_vote_tracker_t *vt, uint32_t candidate_id,
                  uint32_t term, uint8_t voter_count) {
    if (!vt) return -1;
    memset(vt, 0, sizeof(*vt));
    vt->candidate_id = candidate_id;
    vt->term = term;
    vt->voter_count = voter_count;
    vt->votes_needed = voter_count / 2 + 1;
    return 0;
}

int spl_vote_record(spl_vote_tracker_t *vt, uint32_t voter_idx,
                    bool granted) {
    if (!vt || voter_idx >= SPL_MAX_NODES) return -1;
    if (vt->granted[voter_idx]) return 0;  /* already voted */
    vt->granted[voter_idx] = granted;
    if (granted) vt->votes_received++;
    return 0;
}

bool spl_vote_has_majority(const spl_vote_tracker_t *vt) {
    return vt && vt->votes_received >= vt->votes_needed;
}
