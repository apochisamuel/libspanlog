#include "../common/types.h"

/*
 * Additive-Increase Multiplicative-Decrease (AIMD) credit algorithm.
 * Used for flow control between channels and partitions.
 *
 * On success: credits += additive_increment
 * On backpressure signal: credits = credits * multiplicative_factor / 1024
 * (multiplicative_factor is typically 512, i.e., halve on congestion)
 */

typedef struct {
    int32_t  credits;
    int32_t  max_credits;
    int32_t  min_credits;
    int32_t  additive_incr;
    int32_t  mult_factor;  /* fixed-point /1024 */
    uint32_t window_size;
    uint32_t inflight;
    uint32_t congestion_count;
    uint32_t success_count;
    uint32_t rtt_avg_us;
    uint32_t rtt_min_us;
    bool     in_slow_start;
    int32_t  ssthresh;
} spl_aimd_state_t;

int spl_aimd_init(spl_aimd_state_t *state, int32_t initial_credits,
                  int32_t max_credits) {
    if (!state) return -1;

    state->credits = initial_credits;
    state->max_credits = max_credits;
    state->min_credits = 1;
    state->additive_incr = 1;
    state->mult_factor = 512;  /* halve on congestion */
    state->window_size = (uint32_t)initial_credits;
    state->inflight = 0;
    state->congestion_count = 0;
    state->success_count = 0;
    state->rtt_avg_us = 1000;
    state->rtt_min_us = 1000;
    state->in_slow_start = true;
    state->ssthresh = max_credits / 2;

    return 0;
}

int spl_aimd_on_success(spl_aimd_state_t *state, uint32_t rtt_us) {
    if (!state) return -1;

    state->success_count++;
    if (state->inflight > 0) state->inflight--;

    /* Update RTT estimates */
    if (rtt_us < state->rtt_min_us)
        state->rtt_min_us = rtt_us;
    /* EWMA: avg = avg * 7/8 + sample * 1/8 */
    state->rtt_avg_us = (state->rtt_avg_us * 7 + rtt_us) / 8;

    if (state->in_slow_start) {
        /* Exponential growth in slow start */
        state->credits++;
        if (state->credits >= state->ssthresh) {
            state->in_slow_start = false;
        }
    } else {
        /* Additive increase: +1 per window of ACKs */
        if (state->success_count % state->window_size == 0) {
            state->credits += state->additive_incr;
        }
    }

    if (state->credits > state->max_credits)
        state->credits = state->max_credits;

    state->window_size = (uint32_t)state->credits;
    return state->credits;
}

int spl_aimd_on_congestion(spl_aimd_state_t *state) {
    if (!state) return -1;

    state->congestion_count++;
    state->in_slow_start = false;

    /* Multiplicative decrease */
    int32_t new_credits = (state->credits * state->mult_factor) / 1024;
    if (new_credits < state->min_credits)
        new_credits = state->min_credits;

    state->ssthresh = new_credits;
    state->credits = new_credits;
    state->window_size = (uint32_t)new_credits;

    return state->credits;
}

int spl_aimd_on_timeout(spl_aimd_state_t *state) {
    if (!state) return -1;

    /* Timeout: aggressive reduction, enter slow start */
    state->ssthresh = state->credits / 2;
    if (state->ssthresh < state->min_credits)
        state->ssthresh = state->min_credits;

    state->credits = state->min_credits;
    state->in_slow_start = true;
    state->window_size = 1;
    state->congestion_count++;

    return state->credits;
}

int spl_aimd_can_send(const spl_aimd_state_t *state) {
    if (!state) return 0;
    return state->inflight < state->window_size ? 1 : 0;
}

int spl_aimd_on_send(spl_aimd_state_t *state) {
    if (!state) return -1;
    state->inflight++;
    return 0;
}

/*
 * Compute congestion score: 0-100 where 100 = fully congested.
 * Based on credit utilization, loss rate, and RTT inflation.
 */
int spl_aimd_congestion_score(const spl_aimd_state_t *state) {
    if (!state) return 0;

    int score = 0;

    /* Credit utilization: low credits relative to max */
    if (state->max_credits > 0) {
        int util = 100 - (state->credits * 100) / state->max_credits;
        score += util / 3;  /* up to 33 points */
    }

    /* Loss rate */
    uint32_t total = state->congestion_count + state->success_count;
    if (total > 0) {
        int loss_pct = (int)((state->congestion_count * 100) / total);
        score += loss_pct / 3;  /* up to 33 points */
    }

    /* RTT inflation: current avg vs minimum observed */
    if (state->rtt_min_us > 0) {
        int inflation = (int)((state->rtt_avg_us * 100) / state->rtt_min_us) - 100;
        if (inflation < 0) inflation = 0;
        if (inflation > 34) inflation = 34;
        score += inflation;  /* up to 34 points */
    }

    if (score > 100) score = 100;
    return score;
}
