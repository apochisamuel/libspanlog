#include "../include/spanlog.h"
#include "../src/common/types.h"
#include "../src/common/hashtable.h"
#include "../src/common/ringbuf.h"
#include "../src/common/pool.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int p = 0, f = 0;
#define T(n) printf("  %s... ", #n)
#define P() do { printf("PASS\n"); p++; } while(0)
#define F(m) do { printf("FAIL: %s\n", m); f++; } while(0)

static void test_hashtable(void) {
    T(ht_create);
    spl_hashtable_t *ht = spl_ht_create(16);
    if (ht) P(); else { F("null"); return; }

    T(ht_insert_lookup);
    spl_ht_insert(ht, 42, (void *)0xDEAD);
    void *v = spl_ht_lookup(ht, 42);
    if (v == (void *)0xDEAD) P(); else F("lookup");

    T(ht_contains);
    if (spl_ht_contains(ht, 42) && !spl_ht_contains(ht, 99)) P();
    else F("contains");

    T(ht_remove);
    spl_ht_remove(ht, 42);
    if (!spl_ht_contains(ht, 42)) P(); else F("remove");

    T(ht_resize);
    for (int i = 0; i < 100; i++)
        spl_ht_insert(ht, (uint64_t)i, (void *)(uintptr_t)(i + 1));
    if (spl_ht_count(ht) == 100) P(); else F("count");

    T(ht_iterate);
    int sum = 0;
    spl_ht_iterate(ht, (spl_ht_iter_fn)(void *)NULL, &sum);
    P(); /* just testing no crash */

    spl_ht_destroy(ht);
}

static void test_ringbuf(void) {
    T(rb_create);
    spl_ringbuf_t *rb = spl_ringbuf_create(64);
    if (rb) P(); else { F("null"); return; }

    T(rb_write_read);
    uint8_t data[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    spl_ringbuf_write(rb, data, 16);
    uint8_t out[16];
    spl_ringbuf_read(rb, out, 16);
    if (memcmp(data, out, 16) == 0) P(); else F("data");

    T(rb_wrap_around);
    /* Fill and drain multiple times to test wrap */
    for (int i = 0; i < 10; i++) {
        spl_ringbuf_write(rb, data, 16);
        spl_ringbuf_read(rb, out, 16);
    }
    if (spl_ringbuf_is_empty(rb)) P(); else F("empty");

    T(rb_full);
    uint8_t big[64];
    memset(big, 0xAA, 64);
    spl_ringbuf_write(rb, big, 64);
    if (spl_ringbuf_is_full(rb)) P(); else F("full");

    spl_ringbuf_destroy(rb);
}

static void test_pool(void) {
    T(pool_create);
    spl_pool_t *pool = spl_pool_create(64, 16);
    if (pool) P(); else { F("null"); return; }

    T(pool_alloc_free);
    void *a = spl_pool_alloc(pool);
    void *b = spl_pool_alloc(pool);
    if (a && b && a != b) P(); else F("alloc");

    T(pool_free_reuse);
    spl_pool_free(pool, a);
    void *c = spl_pool_alloc(pool);
    if (c == a) P(); else F("reuse");  /* freed block should be reused */

    T(pool_exhaustion);
    /* Allocate all remaining */
    for (int i = 0; i < 14; i++) spl_pool_alloc(pool);
    void *last = spl_pool_alloc(pool);
    if (last == NULL) P(); else F("exhaust");

    spl_pool_destroy(pool);
}

static void test_bitops(void) {
    T(clz32);
    if (spl_clz32(1) == 31 && spl_clz32(0x80000000) == 0) P();
    else F("clz");

    T(ctz32);
    if (spl_ctz32(1) == 0 && spl_ctz32(8) == 3) P();
    else F("ctz");

    T(rotl_rotr);
    uint32_t x = 0x12345678;
    if (spl_rotr32(spl_rotl32(x, 7), 7) == x) P();
    else F("rotate");

    T(align_up);
    if (spl_align_up(13, 8) == 16 && spl_align_up(16, 8) == 16) P();
    else F("align");
}

int main(void) {
    printf("libspanlog subsystem tests\n");
    printf("==========================\n");

    test_hashtable();
    test_ringbuf();
    test_pool();
    test_bitops();

    printf("\nResults: %d passed, %d failed\n", p, f);
    return f > 0 ? 1 : 0;
}
