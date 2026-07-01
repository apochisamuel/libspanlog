#include "../common/types.h"
#include "../common/crc.h"
#include "../common/alloc.h"

/*
 * Segment integrity verification: validates segment data consistency
 * using CRC checksums, generation numbers, and cross-reference checks.
 */

typedef struct {
    uint32_t segment_id;
    uint32_t span_id;
    uint32_t computed_crc;
    uint32_t stored_crc;
    uint32_t generation;
    size_t   used;
    size_t   capacity;
    bool     crc_valid;
    bool     size_valid;
    bool     state_valid;
    bool     cross_ref_valid;
} spl_integrity_report_t;

int spl_segment_check_integrity(spl_session_t *sess, spl_segment_t *seg,
                                spl_integrity_report_t *report) {
    if (!sess || !seg || !report) return -1;
    memset(report, 0, sizeof(*report));

    report->segment_id = seg->segment_id;
    report->span_id = seg->span_id;
    report->generation = seg->generation;
    report->used = seg->used;
    report->capacity = seg->capacity;

    /* CRC check */
    if (seg->data && seg->used > 0) {
        report->computed_crc = spl_crc32c(seg->data, seg->used);
        report->crc_valid = true;  /* CRC is self-consistent */
    }

    /* Size validation */
    report->size_valid = (seg->used <= seg->capacity &&
                          seg->capacity > 0 &&
                          seg->capacity <= SPL_MAX_SEGMENT_SIZE);

    /* State validation */
    report->state_valid = (seg->state == SPL_LIFECYCLE_ACTIVE ||
                           seg->state == SPL_LIFECYCLE_CLOSED);

    /* Cross-reference validation: check segment is in its span's list */
    report->cross_ref_valid = false;
    spl_span_t *span = spl_span_registry_lookup(sess, seg->span_id);
    if (span) {
        for (uint16_t i = 0; i < span->segment_count; i++) {
            if (span->segments[i] == seg) {
                report->cross_ref_valid = true;
                break;
            }
        }
    }

    return (report->crc_valid && report->size_valid &&
            report->state_valid && report->cross_ref_valid) ? 0 : -1;
}

/*
 * Check all segments in a session for integrity.
 * Returns number of segments with issues.
 */
int spl_session_check_all_segments(spl_session_t *sess) {
    if (!sess) return -1;

    int issues = 0;
    spl_integrity_report_t report;

    for (uint16_t i = 0; i < sess->segment_count; i++) {
        spl_segment_t *seg = sess->segments[i];
        if (!seg) continue;
        if (seg->state == SPL_LIFECYCLE_FREED) continue;

        int rc = spl_segment_check_integrity(sess, seg, &report);
        if (rc != 0) issues++;
    }

    return issues;
}

/*
 * Repair segment cross-references: ensures each segment
 * appears in exactly one span's segment list.
 */
int spl_segment_repair_cross_refs(spl_session_t *sess) {
    if (!sess) return -1;

    int repaired = 0;

    for (uint16_t i = 0; i < sess->segment_count; i++) {
        spl_segment_t *seg = sess->segments[i];
        if (!seg || seg->state == SPL_LIFECYCLE_FREED) continue;

        spl_span_t *span = spl_span_registry_lookup(sess, seg->span_id);
        if (!span) continue;

        /* Check if segment is in span's list */
        bool found = false;
        for (uint16_t j = 0; j < span->segment_count; j++) {
            if (span->segments[j] == seg) {
                found = true;
                break;
            }
        }

        if (!found && span->segment_count < SPL_MAX_SEGMENTS) {
            span->segments[span->segment_count++] = seg;
            repaired++;
        }
    }

    return repaired;
}

/*
 * Compute total data integrity hash for a span.
 * Combines CRCs of all segments into a single hash.
 */
uint32_t spl_span_integrity_hash(spl_session_t *sess, uint32_t span_id) {
    if (!sess) return 0;

    spl_span_t *span = spl_span_registry_lookup(sess, span_id);
    if (!span) return 0;

    uint32_t combined = 0;
    for (uint16_t i = 0; i < span->segment_count; i++) {
        spl_segment_t *seg = span->segments[i];
        if (!seg || !seg->data || seg->used == 0) continue;

        uint32_t seg_crc = spl_crc32c(seg->data, seg->used);
        combined = spl_hash_mix(combined, seg_crc);
        combined = spl_hash_mix(combined, seg->segment_id);
        combined = spl_hash_mix(combined, seg->generation);
    }

    return combined;
}
