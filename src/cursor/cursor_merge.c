#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Multi-cursor merge iterator: reads from multiple cursors simultaneously,
 * merging their data in segment order. Used for scanning across spans.
 *
 * Implements a k-way merge using a min-heap ordered by segment generation.
 */

#define SPL_MERGE_MAX_CURSORS 16

typedef struct {
    spl_cursor_t *cursor;
    uint32_t      priority;  /* lower = higher priority (earlier data) */
    bool          exhausted;
} spl_merge_entry_t;

typedef struct {
    spl_merge_entry_t entries[SPL_MERGE_MAX_CURSORS];
    uint8_t           count;
    uint8_t           active;
    uint64_t          total_bytes_read;
} spl_cursor_merge_t;

static void merge_sift_down(spl_cursor_merge_t *m, uint8_t idx) {
    while (2 * idx + 1 < m->count) {
        uint8_t left = 2 * idx + 1;
        uint8_t right = 2 * idx + 2;
        uint8_t smallest = idx;

        if (left < m->count && !m->entries[left].exhausted &&
            m->entries[left].priority < m->entries[smallest].priority)
            smallest = left;
        if (right < m->count && !m->entries[right].exhausted &&
            m->entries[right].priority < m->entries[smallest].priority)
            smallest = right;

        if (smallest != idx) {
            spl_merge_entry_t tmp = m->entries[idx];
            m->entries[idx] = m->entries[smallest];
            m->entries[smallest] = tmp;
            idx = smallest;
        } else break;
    }
}

int spl_merge_init(spl_cursor_merge_t *merge) {
    if (!merge) return -1;
    memset(merge, 0, sizeof(*merge));
    return 0;
}

int spl_merge_add_cursor(spl_cursor_merge_t *merge, spl_cursor_t *cursor,
                         uint32_t priority) {
    if (!merge || !cursor || merge->count >= SPL_MERGE_MAX_CURSORS)
        return -1;

    spl_merge_entry_t *e = &merge->entries[merge->count++];
    e->cursor = cursor;
    e->priority = priority;
    e->exhausted = false;
    merge->active++;

    return 0;
}

int spl_merge_build_heap(spl_cursor_merge_t *merge) {
    if (!merge) return -1;
    for (int i = (int)merge->count / 2 - 1; i >= 0; i--) {
        merge_sift_down(merge, (uint8_t)i);
    }
    return 0;
}

/*
 * Read from the highest-priority (lowest priority value) cursor.
 * Returns bytes read, or 0 if all cursors exhausted.
 */
int spl_merge_read_next(spl_cursor_merge_t *merge, spl_session_t *sess,
                        uint8_t *out, size_t out_len, size_t *bytes_read) {
    if (!merge || !sess || !out || !bytes_read) return -1;
    if (merge->active == 0) return 0;

    /* Find first non-exhausted entry (heap root) */
    spl_merge_entry_t *top = &merge->entries[0];
    if (top->exhausted) {
        *bytes_read = 0;
        return 0;
    }

    spl_cursor_t *cursor = top->cursor;
    int rc = spl_cursor_materialize(sess, cursor, out, out_len, bytes_read);

    if (rc != SPL_OK || *bytes_read == 0) {
        top->exhausted = true;
        merge->active--;
        merge_sift_down(merge, 0);
    } else {
        merge->total_bytes_read += *bytes_read;
        /* Advance to next segment */
        top->priority += cursor->generation;
        merge_sift_down(merge, 0);
    }

    return rc;
}

bool spl_merge_is_exhausted(const spl_cursor_merge_t *merge) {
    return merge && merge->active == 0;
}

uint64_t spl_merge_total_bytes(const spl_cursor_merge_t *merge) {
    return merge ? merge->total_bytes_read : 0;
}

/*
 * Estimate remaining bytes across all active cursors.
 */
uint64_t spl_merge_remaining_estimate(const spl_cursor_merge_t *merge) {
    if (!merge) return 0;
    uint64_t remaining = 0;
    for (uint8_t i = 0; i < merge->count; i++) {
        if (merge->entries[i].exhausted) continue;
        spl_cursor_t *c = merge->entries[i].cursor;
        if (c && c->cached_data_len > c->position)
            remaining += c->cached_data_len - c->position;
    }
    return remaining;
}
