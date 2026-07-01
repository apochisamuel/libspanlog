#include "bitops.h"

uint32_t spl_popcount32(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555);
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
    x = (x + (x >> 4)) & 0x0F0F0F0F;
    return (x * 0x01010101) >> 24;
}

uint32_t spl_clz32(uint32_t x) {
    if (x == 0) return 32;
    uint32_t n = 0;
    if ((x & 0xFFFF0000) == 0) { n += 16; x <<= 16; }
    if ((x & 0xFF000000) == 0) { n += 8;  x <<= 8;  }
    if ((x & 0xF0000000) == 0) { n += 4;  x <<= 4;  }
    if ((x & 0xC0000000) == 0) { n += 2;  x <<= 2;  }
    if ((x & 0x80000000) == 0) { n += 1; }
    return n;
}

uint32_t spl_ctz32(uint32_t x) {
    if (x == 0) return 32;
    uint32_t n = 0;
    if ((x & 0x0000FFFF) == 0) { n += 16; x >>= 16; }
    if ((x & 0x000000FF) == 0) { n += 8;  x >>= 8;  }
    if ((x & 0x0000000F) == 0) { n += 4;  x >>= 4;  }
    if ((x & 0x00000003) == 0) { n += 2;  x >>= 2;  }
    if ((x & 0x00000001) == 0) { n += 1; }
    return n;
}

uint32_t spl_next_power_of_2(uint32_t x) {
    if (x == 0) return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

bool spl_is_power_of_2(uint32_t x) {
    return x != 0 && (x & (x - 1)) == 0;
}

uint32_t spl_align_up(uint32_t x, uint32_t align) {
    if (!spl_is_power_of_2(align)) return x;
    return (x + align - 1) & ~(align - 1);
}

uint32_t spl_rotl32(uint32_t x, int r) {
    return (x << r) | (x >> (32 - r));
}

uint32_t spl_rotr32(uint32_t x, int r) {
    return (x >> r) | (x << (32 - r));
}
