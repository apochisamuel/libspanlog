#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Simple Run-Length Encoding (RLE) compression for segment data.
 * Used to reduce transfer size during migration and replication.
 *
 * Format: [run_byte, count_byte] pairs.
 * If count == 0, next 2 bytes are literal count (up to 255 literals follow).
 * If count > 0, run_byte is repeated count times.
 */

size_t spl_rle_compress(const uint8_t *input, size_t input_len,
                        uint8_t *output, size_t output_cap) {
    if (!input || !output || input_len == 0) return 0;

    size_t in_pos = 0, out_pos = 0;

    while (in_pos < input_len && out_pos + 2 <= output_cap) {
        uint8_t current = input[in_pos];
        size_t run_len = 1;

        /* Count run */
        while (in_pos + run_len < input_len &&
               input[in_pos + run_len] == current &&
               run_len < 255) {
            run_len++;
        }

        if (run_len >= 3) {
            /* Encode as run */
            output[out_pos++] = current;
            output[out_pos++] = (uint8_t)run_len;
            in_pos += run_len;
        } else {
            /* Collect literals */
            size_t lit_start = in_pos;
            size_t lit_count = 0;

            while (in_pos + lit_count < input_len && lit_count < 255) {
                /* Check if a run starts here */
                uint8_t c = input[in_pos + lit_count];
                size_t ahead = 1;
                while (in_pos + lit_count + ahead < input_len &&
                       input[in_pos + lit_count + ahead] == c &&
                       ahead < 4) {
                    ahead++;
                }
                if (ahead >= 3) break;
                lit_count++;
            }

            if (lit_count == 0) {
                /* Single byte that happens to not form a run */
                output[out_pos++] = current;
                output[out_pos++] = 1;
                in_pos++;
            } else {
                /* Encode literals: marker(0x00) + count + data */
                if (out_pos + 2 + lit_count > output_cap) break;
                output[out_pos++] = 0x00;  /* literal marker (will be escaped) */
                output[out_pos++] = 0;     /* count=0 means literal mode */
                /* Actually, simpler: just encode each as run of 1 */
                out_pos -= 2;
                for (size_t i = 0; i < lit_count && out_pos + 2 <= output_cap; i++) {
                    output[out_pos++] = input[in_pos + i];
                    output[out_pos++] = 1;
                }
                in_pos += lit_count;
            }
        }
    }

    return out_pos;
}

size_t spl_rle_decompress(const uint8_t *input, size_t input_len,
                          uint8_t *output, size_t output_cap) {
    if (!input || !output || input_len == 0) return 0;

    size_t in_pos = 0, out_pos = 0;

    while (in_pos + 1 < input_len && out_pos < output_cap) {
        uint8_t byte = input[in_pos];
        uint8_t count = input[in_pos + 1];
        in_pos += 2;

        if (count == 0) continue;

        for (uint8_t i = 0; i < count && out_pos < output_cap; i++) {
            output[out_pos++] = byte;
        }
    }

    return out_pos;
}

/*
 * Estimate compression ratio without actually compressing.
 * Returns ratio * 100 (e.g., 150 = 1.5x compression).
 */
int spl_rle_estimate_ratio(const uint8_t *data, size_t len) {
    if (!data || len == 0) return 100;

    size_t runs = 0, run_bytes = 0;
    size_t pos = 0;

    while (pos < len) {
        uint8_t c = data[pos];
        size_t run = 1;
        while (pos + run < len && data[pos + run] == c && run < 255)
            run++;

        if (run >= 3) {
            runs++;
            run_bytes += run;
        }
        pos += run;
    }

    if (run_bytes == 0) return 100;

    /* Compressed size estimate: (len - run_bytes) * 2 + runs * 2 */
    size_t compressed_est = (len - run_bytes) * 2 + runs * 2;
    size_t original = len;

    if (compressed_est >= original) return 100;
    return (int)((original * 100) / compressed_est);
}

/*
 * Segment-level compression: compress segment data in-place.
 * Returns bytes saved, or -1 on error.
 */
int spl_segment_compress(spl_segment_t *seg) {
    if (!seg || !seg->data || seg->used == 0) return -1;

    uint8_t *compressed = spl_alloc(seg->used * 2);
    if (!compressed) return -1;

    size_t comp_len = spl_rle_compress(seg->data, seg->used,
                                       compressed, seg->used * 2);

    if (comp_len >= seg->used) {
        /* Compression didn't help */
        spl_free(compressed);
        return 0;
    }

    /* Replace segment data with compressed version */
    memcpy(seg->data, compressed, comp_len);
    int saved = (int)(seg->used - comp_len);
    seg->used = comp_len;
    seg->compacted = true;

    spl_free(compressed);
    return saved;
}
