#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Stream flow control: sliding window protocol with
 * per-stream congestion detection.
 */

typedef struct {
    uint32_t stream_id;
    uint32_t window_size;
    uint32_t inflight;
    uint32_t acked;
    uint32_t nacked;
    uint32_t send_next;
    uint32_t recv_next;
    uint32_t recv_window;
    bool     congested;
    uint32_t congestion_count;

    /* RTT tracking */
    uint32_t rtt_samples[16];
    uint8_t  rtt_sample_count;
    uint8_t  rtt_sample_idx;
    uint32_t srtt;  /* smoothed RTT in microseconds */
    uint32_t rttvar;
    uint32_t rto;   /* retransmit timeout */
} spl_stream_flow_t;

int spl_flow_init(spl_stream_flow_t *flow, uint32_t stream_id,
                  uint32_t initial_window) {
    if (!flow) return -1;
    memset(flow, 0, sizeof(*flow));
    flow->stream_id = stream_id;
    flow->window_size = initial_window > 0 ? initial_window : 16;
    flow->recv_window = initial_window > 0 ? initial_window : 16;
    flow->rto = 1000000;  /* 1 second initial RTO */
    return 0;
}

int spl_flow_on_send(spl_stream_flow_t *flow) {
    if (!flow) return -1;
    if (flow->inflight >= flow->window_size) return -1;
    flow->inflight++;
    flow->send_next++;
    return 0;
}

int spl_flow_on_ack(spl_stream_flow_t *flow, uint32_t rtt_us) {
    if (!flow) return -1;
    if (flow->inflight == 0) return -1;

    flow->inflight--;
    flow->acked++;

    /* Update RTT */
    flow->rtt_samples[flow->rtt_sample_idx] = rtt_us;
    flow->rtt_sample_idx = (flow->rtt_sample_idx + 1) % 16;
    if (flow->rtt_sample_count < 16) flow->rtt_sample_count++;

    /* RFC 6298 SRTT/RTTVAR computation */
    if (flow->srtt == 0) {
        flow->srtt = rtt_us;
        flow->rttvar = rtt_us / 2;
    } else {
        int32_t delta = (int32_t)rtt_us - (int32_t)flow->srtt;
        if (delta < 0) delta = -delta;
        flow->rttvar = (flow->rttvar * 3 + (uint32_t)delta) / 4;
        flow->srtt = (flow->srtt * 7 + rtt_us) / 8;
    }
    flow->rto = flow->srtt + 4 * flow->rttvar;
    if (flow->rto < 100000) flow->rto = 100000;  /* min 100ms */
    if (flow->rto > 10000000) flow->rto = 10000000;  /* max 10s */

    /* Congestion avoidance: grow window on ACKs */
    if (!flow->congested) {
        if (flow->window_size < 64) {
            flow->window_size++;  /* slow start */
        }
    }

    return 0;
}

int spl_flow_on_nack(spl_stream_flow_t *flow) {
    if (!flow) return -1;
    flow->nacked++;
    flow->congested = true;
    flow->congestion_count++;

    /* Halve window */
    flow->window_size = flow->window_size / 2;
    if (flow->window_size < 1) flow->window_size = 1;

    return 0;
}

int spl_flow_on_timeout(spl_stream_flow_t *flow) {
    if (!flow) return -1;

    flow->congested = true;
    flow->congestion_count++;
    flow->window_size = 1;

    /* Double RTO (exponential backoff) */
    flow->rto *= 2;
    if (flow->rto > 10000000) flow->rto = 10000000;

    return 0;
}

bool spl_flow_can_send(const spl_stream_flow_t *flow) {
    if (!flow) return false;
    return flow->inflight < flow->window_size;
}

uint32_t spl_flow_effective_window(const spl_stream_flow_t *flow) {
    if (!flow) return 0;
    uint32_t effective = flow->window_size;
    if (flow->recv_window < effective)
        effective = flow->recv_window;
    return effective;
}

/*
 * Compute goodput efficiency: actual throughput / max possible.
 * Returns 0-1000 (1000 = maximum efficiency).
 */
int spl_flow_efficiency(const spl_stream_flow_t *flow) {
    if (!flow) return 0;
    uint32_t total = flow->acked + flow->nacked;
    if (total == 0) return 1000;
    return (int)((flow->acked * 1000) / total);
}
