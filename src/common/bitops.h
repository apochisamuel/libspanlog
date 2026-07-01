#ifndef SPL_BITOPS_H
#define SPL_BITOPS_H
#include <stdint.h>
#include <stdbool.h>
uint32_t spl_popcount32(uint32_t x);
uint32_t spl_clz32(uint32_t x);
uint32_t spl_ctz32(uint32_t x);
uint32_t spl_next_power_of_2(uint32_t x);
bool     spl_is_power_of_2(uint32_t x);
uint32_t spl_align_up(uint32_t x, uint32_t align);
uint32_t spl_rotl32(uint32_t x, int r);
uint32_t spl_rotr32(uint32_t x, int r);
#endif
