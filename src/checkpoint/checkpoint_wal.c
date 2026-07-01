#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Write-Ahead Log (WAL) for checkpoint durability.
 * Records operations before they're applied to support crash recovery.
 */

#define SPL_WAL_MAX_ENTRIES 2048
#define SPL_WAL_ENTRY_MAX_DATA 256

typedef enum {
    SPL_WAL_BEGIN    = 1,
    SPL_WAL_WRITE    = 2,
    SPL_WAL_COMMIT   = 3,
    SPL_WAL_ABORT    = 4,
    SPL_WAL_BARRIER  = 5
} spl_wal_type_t;

typedef struct {
    uint64_t       lsn;        /* log sequence number */
    spl_wal_type_t type;
    uint32_t       txn_id;
    uint32_t       span_id;
    uint32_t       segment_id;
    uint32_t       offset;
    uint16_t       data_len;
    uint8_t        data[SPL_WAL_ENTRY_MAX_DATA];
    uint32_t       checksum;
} spl_wal_entry_t;

typedef struct {
    spl_wal_entry_t entries[SPL_WAL_MAX_ENTRIES];
    uint64_t        head_lsn;
    uint64_t        tail_lsn;
    uint64_t        flush_lsn;
    uint32_t        entry_count;
    uint32_t        active_txn_count;
} spl_wal_t;

int spl_wal_init(spl_wal_t *wal) {
    if (!wal) return -1;
    memset(wal, 0, sizeof(*wal));
    wal->head_lsn = 1;
    wal->tail_lsn = 0;
    wal->flush_lsn = 0;
    return 0;
}

static uint32_t wal_checksum(const spl_wal_entry_t *entry) {
    uint32_t h = spl_hash_mix((uint32_t)entry->lsn, entry->txn_id);
    h = spl_hash_mix(h, (uint32_t)entry->type);
    h = spl_hash_mix(h, entry->span_id);
    h = spl_hash_mix(h, entry->segment_id);
    if (entry->data_len > 0) {
        h = spl_hash_mix(h, spl_fnv1a(entry->data, entry->data_len));
    }
    return h;
}

int spl_wal_append(spl_wal_t *wal, spl_wal_type_t type,
                   uint32_t txn_id, uint32_t span_id,
                   uint32_t segment_id, uint32_t offset,
                   const uint8_t *data, uint16_t data_len) {
    if (!wal || wal->entry_count >= SPL_WAL_MAX_ENTRIES) return -1;
    if (data_len > SPL_WAL_ENTRY_MAX_DATA) return -1;

    uint32_t slot = (uint32_t)(wal->head_lsn % SPL_WAL_MAX_ENTRIES);
    spl_wal_entry_t *entry = &wal->entries[slot];

    entry->lsn = wal->head_lsn++;
    entry->type = type;
    entry->txn_id = txn_id;
    entry->span_id = span_id;
    entry->segment_id = segment_id;
    entry->offset = offset;
    entry->data_len = data_len;
    if (data && data_len > 0)
        memcpy(entry->data, data, data_len);
    entry->checksum = wal_checksum(entry);

    wal->tail_lsn = entry->lsn;
    wal->entry_count++;

    if (type == SPL_WAL_BEGIN) wal->active_txn_count++;
    if (type == SPL_WAL_COMMIT || type == SPL_WAL_ABORT)
        wal->active_txn_count--;

    return 0;
}

int spl_wal_flush(spl_wal_t *wal) {
    if (!wal) return -1;
    wal->flush_lsn = wal->tail_lsn;
    return 0;
}

/*
 * Recover: replay committed transactions from the WAL.
 * Returns number of transactions replayed.
 */
int spl_wal_recover(spl_wal_t *wal) {
    if (!wal) return -1;

    /* Find all committed transactions */
    uint32_t committed_txns[256];
    uint32_t committed_count = 0;

    for (uint32_t i = 0; i < wal->entry_count && i < SPL_WAL_MAX_ENTRIES; i++) {
        spl_wal_entry_t *e = &wal->entries[i];
        if (e->type == SPL_WAL_COMMIT && committed_count < 256) {
            /* Verify checksum */
            uint32_t expected = wal_checksum(e);
            if (expected == e->checksum) {
                committed_txns[committed_count++] = e->txn_id;
            }
        }
    }

    /* Replay write entries for committed transactions */
    int replayed = 0;
    for (uint32_t i = 0; i < wal->entry_count && i < SPL_WAL_MAX_ENTRIES; i++) {
        spl_wal_entry_t *e = &wal->entries[i];
        if (e->type != SPL_WAL_WRITE) continue;

        /* Check if this write's txn was committed */
        bool committed = false;
        for (uint32_t j = 0; j < committed_count; j++) {
            if (committed_txns[j] == e->txn_id) {
                committed = true;
                break;
            }
        }

        if (committed) {
            /* Would apply the write to the segment here */
            replayed++;
        }
    }

    return replayed;
}

/*
 * Truncate WAL entries up to the given LSN.
 * Called after a successful checkpoint to free space.
 */
int spl_wal_truncate(spl_wal_t *wal, uint64_t up_to_lsn) {
    if (!wal) return -1;

    uint32_t truncated = 0;
    for (uint32_t i = 0; i < SPL_WAL_MAX_ENTRIES; i++) {
        if (wal->entries[i].lsn <= up_to_lsn && wal->entries[i].lsn > 0) {
            memset(&wal->entries[i], 0, sizeof(spl_wal_entry_t));
            truncated++;
        }
    }

    if (truncated > wal->entry_count) truncated = wal->entry_count;
    wal->entry_count -= truncated;
    return (int)truncated;
}

uint64_t spl_wal_size_bytes(const spl_wal_t *wal) {
    if (!wal) return 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < SPL_WAL_MAX_ENTRIES; i++) {
        if (wal->entries[i].lsn > 0)
            total += sizeof(spl_wal_entry_t);
    }
    return total;
}
