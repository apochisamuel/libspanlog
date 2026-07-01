#include "../common/types.h"
#include "../common/crc.h"
#include "../common/util.h"

int spl_protocol_validate_frame_crc(const uint8_t *data, size_t frame_len,
                                    uint32_t expected_crc) {
    if (frame_len < SPL_WIRE_FRAME_HEADER_SIZE)
        return SPL_ERR_TRUNCATED;

    /* CRC over frame header bytes (excluding CRC field at end) plus payload */
    size_t crc_region = frame_len;
    if (crc_region >= 4) crc_region -= 4; /* exclude trailing CRC */
    uint32_t computed = spl_crc32c(data, crc_region);
    if (computed != expected_crc)
        return SPL_ERR_INVALID_CRC;

    return SPL_OK;
}
