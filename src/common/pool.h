#ifndef SPL_POOL_H
#define SPL_POOL_H
#include <stdint.h>
#include <stddef.h>

typedef struct spl_pool_block {
    struct spl_pool_block *next;
} spl_pool_block_t;

typedef struct {
    spl_pool_block_t *free_list;
    uint8_t          *arena;
    size_t            block_size;
    size_t            block_count;
    size_t            arena_size;
    size_t            used_count;
} spl_pool_t;

spl_pool_t *spl_pool_create(size_t block_size, size_t block_count);
void        spl_pool_destroy(spl_pool_t *pool);
void       *spl_pool_alloc(spl_pool_t *pool);
void        spl_pool_free(spl_pool_t *pool, void *ptr);
size_t      spl_pool_used(const spl_pool_t *pool);
size_t      spl_pool_available(const spl_pool_t *pool);

#endif
