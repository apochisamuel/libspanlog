#include "ringbuf.h"
#include "alloc.h"
#include <string.h>

spl_ringbuf_t *spl_ringbuf_create(size_t capacity) {
    if (capacity == 0) return NULL;

    spl_ringbuf_t *rb = spl_calloc(1, sizeof(spl_ringbuf_t));
    if (!rb) return NULL;

    rb->data = spl_alloc(capacity);
    if (!rb->data) { spl_free(rb); return NULL; }

    rb->capacity = capacity;
    rb->read_pos = 0;
    rb->write_pos = 0;
    rb->count = 0;
    return rb;
}

void spl_ringbuf_destroy(spl_ringbuf_t *rb) {
    if (!rb) return;
    spl_free(rb->data);
    spl_free(rb);
}

int spl_ringbuf_write(spl_ringbuf_t *rb, const uint8_t *data, size_t len) {
    if (!rb || !data) return -1;
    if (rb->count + len > rb->capacity) return -1;

    for (size_t i = 0; i < len; i++) {
        rb->data[rb->write_pos] = data[i];
        rb->write_pos = (rb->write_pos + 1) % rb->capacity;
    }
    rb->count += len;
    return 0;
}

int spl_ringbuf_read(spl_ringbuf_t *rb, uint8_t *out, size_t len) {
    if (!rb || !out) return -1;
    if (rb->count < len) return -1;

    for (size_t i = 0; i < len; i++) {
        out[i] = rb->data[rb->read_pos];
        rb->read_pos = (rb->read_pos + 1) % rb->capacity;
    }
    rb->count -= len;
    return 0;
}

int spl_ringbuf_peek(const spl_ringbuf_t *rb, uint8_t *out, size_t len) {
    if (!rb || !out) return -1;
    if (rb->count < len) return -1;

    size_t pos = rb->read_pos;
    for (size_t i = 0; i < len; i++) {
        out[i] = rb->data[pos];
        pos = (pos + 1) % rb->capacity;
    }
    return 0;
}

size_t spl_ringbuf_available(const spl_ringbuf_t *rb) {
    return rb ? rb->count : 0;
}

size_t spl_ringbuf_space(const spl_ringbuf_t *rb) {
    return rb ? rb->capacity - rb->count : 0;
}

void spl_ringbuf_reset(spl_ringbuf_t *rb) {
    if (!rb) return;
    rb->read_pos = 0;
    rb->write_pos = 0;
    rb->count = 0;
}

bool spl_ringbuf_is_empty(const spl_ringbuf_t *rb) {
    return rb ? rb->count == 0 : true;
}

bool spl_ringbuf_is_full(const spl_ringbuf_t *rb) {
    return rb ? rb->count == rb->capacity : false;
}
