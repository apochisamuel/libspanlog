#ifndef SPL_RINGBUF_H
#define SPL_RINGBUF_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint8_t  *data;
    size_t    capacity;
    size_t    read_pos;
    size_t    write_pos;
    size_t    count;
} spl_ringbuf_t;

spl_ringbuf_t *spl_ringbuf_create(size_t capacity);
void           spl_ringbuf_destroy(spl_ringbuf_t *rb);
int            spl_ringbuf_write(spl_ringbuf_t *rb, const uint8_t *data, size_t len);
int            spl_ringbuf_read(spl_ringbuf_t *rb, uint8_t *out, size_t len);
int            spl_ringbuf_peek(const spl_ringbuf_t *rb, uint8_t *out, size_t len);
size_t         spl_ringbuf_available(const spl_ringbuf_t *rb);
size_t         spl_ringbuf_space(const spl_ringbuf_t *rb);
void           spl_ringbuf_reset(spl_ringbuf_t *rb);
bool           spl_ringbuf_is_empty(const spl_ringbuf_t *rb);
bool           spl_ringbuf_is_full(const spl_ringbuf_t *rb);

#endif
