#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Bloom filter for fast segment content lookup.
 * Used to quickly determine if a segment might contain a specific
 * key range before doing a full scan.
 *
 * Uses k=3 hash functions derived from FNV-1a with different seeds.
 */

#define SPL_BLOOM_BITS 1024
#define SPL_BLOOM_BYTES (SPL_BLOOM_BITS / 8)
#define SPL_BLOOM_K 3

typedef struct {
    uint8_t  bits[SPL_BLOOM_BYTES];
    uint32_t insert_count;
    uint32_t false_positive_est;  /* per-mille */
} spl_bloom_filter_t;

static uint32_t bloom_hash(const uint8_t *data, size_t len, uint32_t seed) {
    uint32_t h = 0x811C9DC5 ^ seed;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x01000193;
    }
    return h;
}

int spl_bloom_init(spl_bloom_filter_t *bf) {
    if (!bf) return -1;
    memset(bf, 0, sizeof(*bf));
    return 0;
}

void spl_bloom_insert(spl_bloom_filter_t *bf, const uint8_t *key, size_t len) {
    if (!bf || !key) return;

    for (uint32_t k = 0; k < SPL_BLOOM_K; k++) {
        uint32_t h = bloom_hash(key, len, k * 0x9E3779B9);
        uint32_t bit = h % SPL_BLOOM_BITS;
        bf->bits[bit / 8] |= (1 << (bit % 8));
    }
    bf->insert_count++;

    /* Estimate false positive rate: (1 - e^(-kn/m))^k * 1000
     * Approximation: (n * k * 1000) / (m * 3) for small n */
    uint32_t n = bf->insert_count;
    bf->false_positive_est = (n * SPL_BLOOM_K * 1000) / (SPL_BLOOM_BITS * 3);
    if (bf->false_positive_est > 1000) bf->false_positive_est = 1000;
}

bool spl_bloom_maybe_contains(const spl_bloom_filter_t *bf,
                              const uint8_t *key, size_t len) {
    if (!bf || !key) return false;

    for (uint32_t k = 0; k < SPL_BLOOM_K; k++) {
        uint32_t h = bloom_hash(key, len, k * 0x9E3779B9);
        uint32_t bit = h % SPL_BLOOM_BITS;
        if (!(bf->bits[bit / 8] & (1 << (bit % 8))))
            return false;
    }
    return true;
}

void spl_bloom_reset(spl_bloom_filter_t *bf) {
    if (!bf) return;
    memset(bf->bits, 0, SPL_BLOOM_BYTES);
    bf->insert_count = 0;
    bf->false_positive_est = 0;
}

uint32_t spl_bloom_bit_count(const spl_bloom_filter_t *bf) {
    if (!bf) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < SPL_BLOOM_BYTES; i++) {
        count += spl_popcount32((uint32_t)bf->bits[i]);
    }
    return count;
}

/*
 * Segment defragmentation: reorders data within a segment
 * to eliminate internal gaps left by deletions.
 */
int spl_segment_defrag(spl_segment_t *seg) {
    if (!seg || !seg->data) return -1;
    if (seg->state != SPL_LIFECYCLE_ACTIVE) return -1;

    /* Walk segment data looking for gap markers (0xFF 0xFF prefix).
     * Move valid records forward to fill gaps. */
    size_t read = 0, write = 0;
    while (read < seg->used) {
        /* Check for gap marker */
        if (read + 2 <= seg->used &&
            seg->data[read] == 0xFF && seg->data[read + 1] == 0xFF) {
            /* Gap: skip 2-byte marker + 2-byte gap length */
            if (read + 4 <= seg->used) {
                uint16_t gap_len = spl_read_u16(seg->data + read + 2);
                read += 4 + gap_len;
                continue;
            } else {
                break;  /* truncated gap marker */
            }
        }

        /* Valid data: copy forward */
        if (write != read) {
            seg->data[write] = seg->data[read];
        }
        write++;
        read++;
    }

    /* Zero out reclaimed space */
    if (write < seg->used) {
        memset(seg->data + write, 0, seg->used - write);
    }
    seg->used = write;

    return 0;
}

/*
 * Compute segment utilization score: 0-1000 where 1000 = fully utilized.
 */
int spl_segment_utilization(const spl_segment_t *seg) {
    if (!seg || seg->capacity == 0) return 0;
    return (int)((seg->used * 1000) / seg->capacity);
}

/*
 * Estimate compaction benefit for a set of segments.
 * Returns estimated bytes saved by compacting.
 */
uint64_t spl_segment_compaction_benefit(spl_segment_t **segments,
                                        uint16_t count) {
    if (!segments || count < 2) return 0;

    uint64_t total_capacity = 0;
    uint64_t total_used = 0;

    for (uint16_t i = 0; i < count; i++) {
        if (!segments[i]) continue;
        total_capacity += segments[i]->capacity;
        total_used += segments[i]->used;
    }

    /* Benefit = total_capacity - next_power_of_2(total_used) */
    uint32_t compact_cap = spl_next_power_of_2((uint32_t)total_used);
    if (compact_cap < total_used) compact_cap = (uint32_t)total_used;

    if (total_capacity > compact_cap)
        return total_capacity - compact_cap;

    return 0;
}
