#ifndef SPL_ALLOC_H
#define SPL_ALLOC_H
#include <stddef.h>
void *spl_alloc(size_t size);
void *spl_realloc_buf(void *ptr, size_t new_size);
void  spl_free(void *ptr);
void *spl_calloc(size_t count, size_t size);
char *spl_strdup(const char *s);
#endif
