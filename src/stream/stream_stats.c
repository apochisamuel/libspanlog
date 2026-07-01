#include "../common/types.h"

/*
 * Stream throughput and quality metrics.
 */

typedef struct {
    uint32_t stream_id;
    uint64_t bytes_appended;
    uint64_t bytes_materialized;
    uint32_t fragments_reassembled;
    uint32_t fragments_dropped;
    uint32_t flushes_completed;
    uint32_t reassembly_resets;
    size_t   current_buffer_used;
    size_t   current_buffer_capacity;
    int      buffer_utilization;  /* 0-1000 */
} spl_stream_stats_t;

int spl_stream_collect_stats(const spl_stream_t *stream,
                             spl_stream_stats_t *stats) {
    if (!stream || !stats) return -1;
    memset(stats, 0, sizeof(*stats));

    stats->stream_id = stream->stream_id;
    stats->current_buffer_used = stream->reassembly_used;
    stats->current_buffer_capacity = stream->reassembly_capacity;

    if (stream->reassembly_capacity > 0) {
        stats->buffer_utilization = (int)(
            (stream->reassembly_used * 1000) / stream->reassembly_capacity);
    }

    return 0;
}

/*
 * Estimate stream goodput: actual useful data / total data.
 */
int spl_stream_goodput(const spl_stream_t *stream) {
    if (!stream) return 0;
    if (!stream->active) return 0;

    /* Estimate from reassembly state */
    if (stream->reassembly_capacity > 0) {
        return (int)((stream->reassembly_used * 1000) /
                     stream->reassembly_capacity);
    }
    return 500;  /* default 50% */
}
