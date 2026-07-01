#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Lease coalescing: merges multiple leases on the same span
 * from the same owner into a single lease with extended TTL.
 * Reduces overhead from redundant lease tracking.
 */

typedef struct {
    uint32_t span_id;
    uint32_t owner_token;
    uint32_t lease_ids[16];
    uint8_t  lease_count;
    uint32_t max_epoch;
    uint32_t max_generation;
} spl_coalesce_group_t;

int spl_lease_find_coalesce_groups(spl_session_t *sess,
                                   spl_coalesce_group_t *groups,
                                   uint32_t max_groups) {
    if (!sess || !groups) return -1;

    uint32_t group_count = 0;

    for (uint16_t i = 0; i < sess->lease_count; i++) {
        spl_lease_t *l = sess->leases[i];
        if (!l || !l->active) continue;

        /* Find existing group */
        spl_coalesce_group_t *group = NULL;
        for (uint32_t g = 0; g < group_count; g++) {
            if (groups[g].span_id == l->span_id &&
                groups[g].owner_token == l->owner_token) {
                group = &groups[g];
                break;
            }
        }

        if (!group && group_count < max_groups) {
            group = &groups[group_count++];
            group->span_id = l->span_id;
            group->owner_token = l->owner_token;
            group->lease_count = 0;
        }

        if (group && group->lease_count < 16) {
            group->lease_ids[group->lease_count++] = l->lease_id;
            if (l->epoch_acquired > group->max_epoch)
                group->max_epoch = l->epoch_acquired;
            if (l->generation > group->max_generation)
                group->max_generation = l->generation;
        }
    }

    return (int)group_count;
}

int spl_lease_coalesce_group(spl_session_t *sess,
                             spl_coalesce_group_t *group) {
    if (!sess || !group || group->lease_count < 2) return 0;

    /* Keep the lease with the highest epoch, release others */
    uint32_t keep_id = group->lease_ids[0];
    uint32_t keep_epoch = 0;

    for (uint8_t i = 0; i < group->lease_count; i++) {
        spl_lease_t *l = spl_lease_lookup(sess, group->lease_ids[i]);
        if (l && l->epoch_acquired > keep_epoch) {
            keep_epoch = l->epoch_acquired;
            keep_id = l->lease_id;
        }
    }

    /* Release all except the keeper */
    int released = 0;
    for (uint8_t i = 0; i < group->lease_count; i++) {
        if (group->lease_ids[i] == keep_id) continue;
        spl_lease_t *l = spl_lease_lookup(sess, group->lease_ids[i]);
        if (l && l->active) {
            spl_lease_release(sess, l);
            released++;
        }
    }

    /* Update keeper with best generation */
    spl_lease_t *keeper = spl_lease_lookup(sess, keep_id);
    if (keeper) {
        keeper->generation = group->max_generation;
        keeper->renewal_count += (uint32_t)released;
    }

    return released;
}

int spl_lease_coalesce_all(spl_session_t *sess) {
    if (!sess) return -1;

    spl_coalesce_group_t groups[64];
    int group_count = spl_lease_find_coalesce_groups(sess, groups, 64);
    if (group_count <= 0) return 0;

    int total = 0;
    for (int i = 0; i < group_count; i++) {
        int rc = spl_lease_coalesce_group(sess, &groups[i]);
        if (rc > 0) total += rc;
    }

    return total;
}

/*
 * Estimate memory savings from coalescing.
 */
uint64_t spl_lease_coalesce_savings(spl_session_t *sess) {
    if (!sess) return 0;

    spl_coalesce_group_t groups[64];
    int group_count = spl_lease_find_coalesce_groups(sess, groups, 64);
    if (group_count <= 0) return 0;

    uint64_t savings = 0;
    for (int i = 0; i < group_count; i++) {
        if (groups[i].lease_count > 1) {
            savings += (uint64_t)(groups[i].lease_count - 1) * sizeof(spl_lease_t);
        }
    }
    return savings;
}
