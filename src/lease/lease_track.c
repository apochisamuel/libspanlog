#include "../common/types.h"

/*
 * Lease ownership tracking and audit.
 * Maps lease IDs to their owning subsystem identifiers.
 */

typedef struct {
    uint32_t lease_id;
    uint32_t owner_subsystem;  /* which subsystem owns this lease */
    uint32_t owner_entity_id;  /* channel_id, repl_set_id, etc. */
    bool     active;
} spl_lease_ownership_t;

static spl_lease_ownership_t ownership_table[SPL_MAX_LEASES];
static uint16_t ownership_count = 0;

int spl_lease_track_register(uint32_t lease_id, uint32_t subsystem,
                             uint32_t entity_id) {
    if (ownership_count >= SPL_MAX_LEASES) return SPL_ERR_FULL;

    spl_lease_ownership_t *entry = &ownership_table[ownership_count++];
    entry->lease_id = lease_id;
    entry->owner_subsystem = subsystem;
    entry->owner_entity_id = entity_id;
    entry->active = true;

    return SPL_OK;
}

int spl_lease_track_deregister(uint32_t lease_id) {
    for (uint16_t i = 0; i < ownership_count; i++) {
        if (ownership_table[i].lease_id == lease_id) {
            ownership_table[i].active = false;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}

int spl_lease_track_query_owner(uint32_t lease_id, uint32_t *subsystem,
                                uint32_t *entity_id) {
    for (uint16_t i = 0; i < ownership_count; i++) {
        if (ownership_table[i].lease_id == lease_id &&
            ownership_table[i].active) {
            if (subsystem) *subsystem = ownership_table[i].owner_subsystem;
            if (entity_id) *entity_id = ownership_table[i].owner_entity_id;
            return SPL_OK;
        }
    }
    return SPL_ERR_NOT_FOUND;
}

int spl_lease_track_count_for_subsystem(uint32_t subsystem) {
    int count = 0;
    for (uint16_t i = 0; i < ownership_count; i++) {
        if (ownership_table[i].owner_subsystem == subsystem &&
            ownership_table[i].active)
            count++;
    }
    return count;
}

void spl_lease_track_reset(void) {
    ownership_count = 0;
}
