#include "alloc.h"
#include "util.h"
#include <string.h>

/*
 * Sorted array with binary search operations.
 * Used by span registry, route table, and lease tracking.
 */

typedef struct {
    uint64_t *keys;
    void    **values;
    uint32_t  count;
    uint32_t  capacity;
} spl_sorted_array_t;

spl_sorted_array_t *spl_sa_create(uint32_t initial_cap) {
    spl_sorted_array_t *sa = spl_calloc(1, sizeof(spl_sorted_array_t));
    if (!sa) return NULL;
    if (initial_cap < 8) initial_cap = 8;
    sa->keys = spl_calloc(initial_cap, sizeof(uint64_t));
    sa->values = spl_calloc(initial_cap, sizeof(void *));
    if (!sa->keys || !sa->values) {
        spl_free(sa->keys); spl_free(sa->values); spl_free(sa);
        return NULL;
    }
    sa->capacity = initial_cap;
    return sa;
}

void spl_sa_destroy(spl_sorted_array_t *sa) {
    if (!sa) return;
    spl_free(sa->keys);
    spl_free(sa->values);
    spl_free(sa);
}

static int sa_grow(spl_sorted_array_t *sa) {
    uint32_t new_cap = sa->capacity * 2;
    uint64_t *nk = spl_realloc_buf(sa->keys, new_cap * sizeof(uint64_t));
    void **nv = spl_realloc_buf(sa->values, new_cap * sizeof(void *));
    if (!nk || !nv) return -1;
    sa->keys = nk;
    sa->values = nv;
    sa->capacity = new_cap;
    return 0;
}

int spl_sa_insert(spl_sorted_array_t *sa, uint64_t key, void *value) {
    if (!sa) return -1;
    if (sa->count >= sa->capacity) {
        if (sa_grow(sa) != 0) return -1;
    }

    /* Binary search for insertion point */
    uint32_t lo = 0, hi = sa->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (sa->keys[mid] < key) lo = mid + 1;
        else hi = mid;
    }

    /* Check for duplicate */
    if (lo < sa->count && sa->keys[lo] == key) {
        sa->values[lo] = value;
        return 0;
    }

    /* Shift elements right */
    for (uint32_t i = sa->count; i > lo; i--) {
        sa->keys[i] = sa->keys[i - 1];
        sa->values[i] = sa->values[i - 1];
    }

    sa->keys[lo] = key;
    sa->values[lo] = value;
    sa->count++;
    return 0;
}

void *spl_sa_lookup(const spl_sorted_array_t *sa, uint64_t key) {
    if (!sa || sa->count == 0) return NULL;

    uint32_t lo = 0, hi = sa->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (sa->keys[mid] < key) lo = mid + 1;
        else if (sa->keys[mid] > key) hi = mid;
        else return sa->values[mid];
    }
    return NULL;
}

int spl_sa_remove(spl_sorted_array_t *sa, uint64_t key) {
    if (!sa || sa->count == 0) return -1;

    uint32_t lo = 0, hi = sa->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (sa->keys[mid] < key) lo = mid + 1;
        else if (sa->keys[mid] > key) hi = mid;
        else {
            /* Found: shift elements left */
            for (uint32_t i = mid; i < sa->count - 1; i++) {
                sa->keys[i] = sa->keys[i + 1];
                sa->values[i] = sa->values[i + 1];
            }
            sa->count--;
            return 0;
        }
    }
    return -1;
}

/* Range query: find all keys in [lo_key, hi_key] */
int spl_sa_range(const spl_sorted_array_t *sa, uint64_t lo_key,
                 uint64_t hi_key, void **out, uint32_t max_results) {
    if (!sa || !out) return -1;

    /* Find first key >= lo_key */
    uint32_t lo = 0, hi = sa->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (sa->keys[mid] < lo_key) lo = mid + 1;
        else hi = mid;
    }

    uint32_t found = 0;
    while (lo < sa->count && sa->keys[lo] <= hi_key && found < max_results) {
        out[found++] = sa->values[lo];
        lo++;
    }
    return (int)found;
}

uint32_t spl_sa_count(const spl_sorted_array_t *sa) {
    return sa ? sa->count : 0;
}

/* Find the rank (0-based position) of a key */
int spl_sa_rank(const spl_sorted_array_t *sa, uint64_t key) {
    if (!sa) return -1;
    uint32_t lo = 0, hi = sa->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (sa->keys[mid] < key) lo = mid + 1;
        else if (sa->keys[mid] > key) hi = mid;
        else return (int)mid;
    }
    return -1;
}

/* Get the k-th smallest key (0-indexed) */
int spl_sa_select(const spl_sorted_array_t *sa, uint32_t k,
                  uint64_t *out_key, void **out_value) {
    if (!sa || k >= sa->count) return -1;
    if (out_key) *out_key = sa->keys[k];
    if (out_value) *out_value = sa->values[k];
    return 0;
}

/* Compute median key value (for load balancing) */
uint64_t spl_sa_median(const spl_sorted_array_t *sa) {
    if (!sa || sa->count == 0) return 0;
    return sa->keys[sa->count / 2];
}
