#include "../common/types.h"
#include "../common/crc.h"
#include "../common/util.h"

int spl_protocol_parse_header(const uint8_t *data, size_t len,
                              spl_parsed_header_t *out) {
    if (!data || !out) return SPL_ERR_INVALID_FRAME;
    if (len < SPL_WIRE_HEADER_SIZE) return SPL_ERR_TRUNCATED;

    out->magic       = spl_read_u32(data);
    out->version     = spl_read_u16(data + 4);
    out->session_id  = spl_read_u32(data + 6);
    out->epoch       = spl_read_u32(data + 10);
    out->frame_count = spl_read_u16(data + 14);
    out->header_crc  = spl_read_u32(data + 16);

    if (out->magic != SPL_MAGIC)
        return SPL_ERR_INVALID_MAGIC;
    if ((out->version >> 8) != 1)
        return SPL_ERR_PROTOCOL;

    uint32_t computed_crc = spl_crc32c(data, 14);
    if (computed_crc != out->header_crc)
        return SPL_ERR_INVALID_CRC;

    if (out->frame_count == 0 || out->frame_count > 256)
        return SPL_ERR_INVALID_FRAME;

    return SPL_OK;
}

static int parse_subrecords(const uint8_t *data, size_t len,
                            spl_parsed_frame_t *frame) {
    size_t offset = 0;
    uint8_t count = 0;

    while (offset < len && count < frame->subrecord_count &&
           count < SPL_MAX_SUBRECORDS) {
        if (offset + SPL_WIRE_SUBRECORD_HEADER_SIZE > len)
            return SPL_ERR_TRUNCATED;

        spl_subrecord_t *sub = &frame->subrecords[count];
        sub->kind        = data[offset];
        sub->length      = spl_read_u16(data + offset + 1);
        sub->local_epoch = spl_read_u32(data + offset + 3);
        sub->generation  = spl_read_u32(data + offset + 7);
        sub->lease_token = spl_read_u32(data + offset + 11);

        uint16_t payload_len = sub->length;
        if (payload_len > 0) {
            if (offset + SPL_WIRE_SUBRECORD_HEADER_SIZE + payload_len > len)
                return SPL_ERR_TRUNCATED;
            sub->payload = data + offset + SPL_WIRE_SUBRECORD_HEADER_SIZE;
            sub->payload_len = payload_len;
        } else {
            sub->payload = NULL;
            sub->payload_len = 0;
        }

        offset += SPL_WIRE_SUBRECORD_HEADER_SIZE + payload_len;
        count++;
    }

    frame->parsed_subrecord_count = count;
    if (count < frame->subrecord_count)
        return SPL_ERR_TRUNCATED;
    return SPL_OK;
}

int spl_protocol_parse_frame(const uint8_t *data, size_t len,
                             spl_parsed_frame_t *out) {
    if (!data || !out) return SPL_ERR_INVALID_FRAME;
    if (len < SPL_WIRE_FRAME_HEADER_SIZE) return SPL_ERR_TRUNCATED;

    memset(out, 0, sizeof(*out));
    out->total_length     = spl_read_u16(data);
    out->frame_type       = data[2];
    out->sequence         = spl_read_u16(data + 3);
    out->session_id       = spl_read_u32(data + 5);
    out->channel_id       = spl_read_u16(data + 9);
    out->route_id         = spl_read_u16(data + 11);
    out->owner_token      = spl_read_u32(data + 13);
    out->flags            = spl_read_u16(data + 17);
    out->subrecord_count  = data[19];

    if (out->total_length < SPL_WIRE_FRAME_HEADER_SIZE)
        return SPL_ERR_INVALID_FRAME;
    if ((size_t)out->total_length > len)
        return SPL_ERR_TRUNCATED;
    if (out->frame_type == SPL_FRAME_PADDING)
        return SPL_OK;
    if (out->frame_type == 0)
        return SPL_ERR_INVALID_FRAME;

    size_t payload_offset = SPL_WIRE_FRAME_HEADER_SIZE;
    size_t payload_len = out->total_length - SPL_WIRE_FRAME_HEADER_SIZE;

    if (payload_len >= 4) {
        out->payload_crc = spl_read_u32(data + out->total_length - 4);
        payload_len -= 4;
        if (payload_len > 0) {
            uint32_t computed = spl_crc32c(data + payload_offset, payload_len);
            if (computed != out->payload_crc)
                return SPL_ERR_INVALID_CRC;
        }
    }

    out->parsed_subrecord_count = 0;
    if (out->subrecord_count > 0 && payload_len > 0) {
        int rc = parse_subrecords(data + payload_offset, payload_len, out);
        if (rc != SPL_OK) return rc;
    }
    return SPL_OK;
}
