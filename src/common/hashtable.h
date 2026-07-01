#ifndef SPL_HASHTABLE_H
#define SPL_HASHTABLE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SPL_HT_INITIAL_CAP 16
#define SPL_HT_LOAD_FACTOR_NUM 3
#define SPL_HT_LOAD_FACTOR_DEN 4

typedef struct spl_ht_entry {
    uint64_t          key;
    void             *value;
    struct spl_ht_entry *next;
    uint32_t          hash;
    bool              occupied;
} spl_ht_entry_t;

typedef struct {
    spl_ht_entry_t **buckets;
    uint32_t         capacity;
    uint32_t         count;
    uint32_t         mask;
} spl_hashtable_t;

spl_hashtable_t *spl_ht_create(uint32_t initial_cap);
void             spl_ht_destroy(spl_hashtable_t *ht);
int              spl_ht_insert(spl_hashtable_t *ht, uint64_t key, void *value);
void            *spl_ht_lookup(spl_hashtable_t *ht, uint64_t key);
int              spl_ht_remove(spl_hashtable_t *ht, uint64_t key);
int              spl_ht_resize(spl_hashtable_t *ht, uint32_t new_cap);
uint32_t         spl_ht_count(const spl_hashtable_t *ht);
bool             spl_ht_contains(spl_hashtable_t *ht, uint64_t key);

typedef void (*spl_ht_iter_fn)(uint64_t key, void *value, void *ctx);
void spl_ht_iterate(spl_hashtable_t *ht, spl_ht_iter_fn fn, void *ctx);

#endif
