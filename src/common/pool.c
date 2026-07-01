#include "pool.h"
#include "alloc.h"
#include "bitops.h"
#include <string.h>

spl_pool_t *spl_pool_create(size_t block_size, size_t block_count) {
    if (block_size < sizeof(spl_pool_block_t))
        block_size = sizeof(spl_pool_block_t);

    /* Align block size to 8 bytes */
    block_size = spl_align_up((uint32_t)block_size, 8);

    size_t arena_size = block_size * block_count;
    if (arena_size == 0) return NULL;

    spl_pool_t *pool = spl_calloc(1, sizeof(spl_pool_t));
    if (!pool) return NULL;

    pool->arena = spl_alloc(arena_size);
    if (!pool->arena) { spl_free(pool); return NULL; }

    memset(pool->arena, 0, arena_size);
    pool->block_size = block_size;
    pool->block_count = block_count;
    pool->arena_size = arena_size;
    pool->used_count = 0;

    /* Build free list */
    pool->free_list = NULL;
    for (size_t i = 0; i < block_count; i++) {
        spl_pool_block_t *blk = (spl_pool_block_t *)(pool->arena + i * block_size);
        blk->next = pool->free_list;
        pool->free_list = blk;
    }

    return pool;
}

void spl_pool_destroy(spl_pool_t *pool) {
    if (!pool) return;
    spl_free(pool->arena);
    spl_free(pool);
}

void *spl_pool_alloc(spl_pool_t *pool) {
    if (!pool || !pool->free_list) return NULL;

    spl_pool_block_t *blk = pool->free_list;
    pool->free_list = blk->next;
    pool->used_count++;

    memset(blk, 0, pool->block_size);
    return blk;
}

void spl_pool_free(spl_pool_t *pool, void *ptr) {
    if (!pool || !ptr) return;

    /* Verify ptr is within arena bounds */
    uint8_t *p = (uint8_t *)ptr;
    if (p < pool->arena || p >= pool->arena + pool->arena_size) return;

    spl_pool_block_t *blk = (spl_pool_block_t *)ptr;
    blk->next = pool->free_list;
    pool->free_list = blk;
    pool->used_count--;
}

size_t spl_pool_used(const spl_pool_t *pool) {
    return pool ? pool->used_count : 0;
}

size_t spl_pool_available(const spl_pool_t *pool) {
    return pool ? pool->block_count - pool->used_count : 0;
}
