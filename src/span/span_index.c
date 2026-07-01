#include "../common/types.h"
#include "../common/alloc.h"

/*
 * B-tree index for efficient span lookup by ID range.
 * Used for route resolution and partition mapping.
 *
 * Simple B-tree with order B=4 (max 3 keys, 4 children per node).
 */

#define SPL_BTREE_ORDER 4
#define SPL_BTREE_MAX_KEYS (SPL_BTREE_ORDER - 1)

typedef struct spl_btree_node {
    uint32_t keys[SPL_BTREE_MAX_KEYS];
    spl_span_t *values[SPL_BTREE_MAX_KEYS];
    struct spl_btree_node *children[SPL_BTREE_ORDER];
    uint8_t key_count;
    bool    is_leaf;
} spl_btree_node_t;

typedef struct {
    spl_btree_node_t *root;
    uint32_t          count;
    uint32_t          height;
} spl_span_index_t;

static spl_btree_node_t *btree_create_node(bool is_leaf) {
    spl_btree_node_t *node = spl_calloc(1, sizeof(spl_btree_node_t));
    if (node) node->is_leaf = is_leaf;
    return node;
}

static void btree_destroy_node(spl_btree_node_t *node) {
    if (!node) return;
    if (!node->is_leaf) {
        for (uint8_t i = 0; i <= node->key_count; i++) {
            btree_destroy_node(node->children[i]);
        }
    }
    spl_free(node);
}

spl_span_index_t *spl_span_index_create(void) {
    spl_span_index_t *idx = spl_calloc(1, sizeof(spl_span_index_t));
    if (!idx) return NULL;
    idx->root = btree_create_node(true);
    if (!idx->root) { spl_free(idx); return NULL; }
    idx->height = 1;
    return idx;
}

void spl_span_index_destroy(spl_span_index_t *idx) {
    if (!idx) return;
    btree_destroy_node(idx->root);
    spl_free(idx);
}

spl_span_t *spl_span_index_search(spl_span_index_t *idx, uint32_t span_id) {
    if (!idx || !idx->root) return NULL;

    spl_btree_node_t *node = idx->root;
    while (node) {
        uint8_t i = 0;
        while (i < node->key_count && span_id > node->keys[i])
            i++;

        if (i < node->key_count && span_id == node->keys[i])
            return node->values[i];

        if (node->is_leaf) return NULL;
        node = node->children[i];
    }
    return NULL;
}

static void btree_split_child(spl_btree_node_t *parent, uint8_t idx) {
    spl_btree_node_t *child = parent->children[idx];
    spl_btree_node_t *sibling = btree_create_node(child->is_leaf);
    if (!sibling) return;

    uint8_t mid = SPL_BTREE_MAX_KEYS / 2;

    /* Move upper half of keys to sibling */
    sibling->key_count = child->key_count - mid - 1;
    for (uint8_t j = 0; j < sibling->key_count; j++) {
        sibling->keys[j] = child->keys[mid + 1 + j];
        sibling->values[j] = child->values[mid + 1 + j];
    }
    if (!child->is_leaf) {
        for (uint8_t j = 0; j <= sibling->key_count; j++) {
            sibling->children[j] = child->children[mid + 1 + j];
        }
    }

    /* Shift parent's keys right */
    for (uint8_t j = parent->key_count; j > idx; j--) {
        parent->keys[j] = parent->keys[j - 1];
        parent->values[j] = parent->values[j - 1];
        parent->children[j + 1] = parent->children[j];
    }

    /* Insert median into parent */
    parent->keys[idx] = child->keys[mid];
    parent->values[idx] = child->values[mid];
    parent->children[idx + 1] = sibling;
    parent->key_count++;
    child->key_count = mid;
}

static int btree_insert_nonfull(spl_btree_node_t *node,
                                uint32_t key, spl_span_t *value) {
    int i = node->key_count - 1;

    if (node->is_leaf) {
        while (i >= 0 && node->keys[i] > key) {
            node->keys[i + 1] = node->keys[i];
            node->values[i + 1] = node->values[i];
            i--;
        }
        node->keys[i + 1] = key;
        node->values[i + 1] = value;
        node->key_count++;
        return 0;
    }

    while (i >= 0 && node->keys[i] > key) i--;
    i++;

    if (node->children[i]->key_count == SPL_BTREE_MAX_KEYS) {
        btree_split_child(node, (uint8_t)i);
        if (key > node->keys[i]) i++;
    }

    return btree_insert_nonfull(node->children[i], key, value);
}

int spl_span_index_insert(spl_span_index_t *idx,
                          uint32_t span_id, spl_span_t *span) {
    if (!idx || !span) return -1;

    spl_btree_node_t *root = idx->root;
    if (root->key_count == SPL_BTREE_MAX_KEYS) {
        spl_btree_node_t *new_root = btree_create_node(false);
        if (!new_root) return -1;
        new_root->children[0] = root;
        btree_split_child(new_root, 0);
        idx->root = new_root;
        idx->height++;

        int i = 0;
        if (new_root->keys[0] < span_id) i++;
        btree_insert_nonfull(new_root->children[i], span_id, span);
    } else {
        btree_insert_nonfull(root, span_id, span);
    }

    idx->count++;
    return 0;
}

/*
 * Range query: find all spans with IDs in [lo, hi].
 */
static void btree_range_query(spl_btree_node_t *node, uint32_t lo,
                              uint32_t hi, spl_span_t **out,
                              uint16_t *count, uint16_t max) {
    if (!node || *count >= max) return;

    uint8_t i = 0;
    while (i < node->key_count) {
        if (!node->is_leaf)
            btree_range_query(node->children[i], lo, hi, out, count, max);

        if (node->keys[i] >= lo && node->keys[i] <= hi) {
            if (*count < max) {
                out[*count] = node->values[i];
                (*count)++;
            }
        }

        if (node->keys[i] > hi) break;
        i++;
    }

    if (!node->is_leaf && i <= node->key_count)
        btree_range_query(node->children[i], lo, hi, out, count, max);
}

int spl_span_index_range(spl_span_index_t *idx, uint32_t lo, uint32_t hi,
                         spl_span_t **out, uint16_t max_results) {
    if (!idx || !out) return -1;
    uint16_t count = 0;
    btree_range_query(idx->root, lo, hi, out, &count, max_results);
    return (int)count;
}
