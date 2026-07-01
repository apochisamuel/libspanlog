#ifndef SPL_CRC_H
#define SPL_CRC_H
#include <stdint.h>
#include <stddef.h>
uint32_t spl_crc32c(const uint8_t *data, size_t len);
uint32_t spl_crc32c_combine(uint32_t crc1, uint32_t crc2, size_t len2);
uint32_t spl_crc32c_update(uint32_t crc, const uint8_t *data, size_t len);
#endif
