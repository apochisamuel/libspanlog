#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Retention curve fitting and watermark interpolation.
 * Models data growth rate and predicts when GC will be needed.
 * Uses linear regression on recent watermark samples.
 */

#define SPL_RETENTION_SAMPLE_COUNT 32

typedef struct {
    uint64_t timestamp;
    uint64_t watermark;
} spl_retention_sample_t;

typedef struct {
    spl_retention_sample_t samples[SPL_RETENTION_SAMPLE_COUNT];
    uint8_t  sample_count;
    uint8_t  sample_idx;
    uint64_t threshold;
    /* Linear regression coefficients: watermark = slope * time + intercept */
    int64_t  slope;       /* bytes per second * 1000 (fixed point) */
    int64_t  intercept;
    uint32_t r_squared;   /* 0-1000 (fixed point, 1000 = perfect fit) */
} spl_retention_curve_t;

int spl_retention_curve_init(spl_retention_curve_t *curve, uint64_t threshold) {
    if (!curve) return -1;
    memset(curve, 0, sizeof(*curve));
    curve->threshold = threshold;
    return 0;
}

int spl_retention_curve_add_sample(spl_retention_curve_t *curve,
                                   uint64_t timestamp, uint64_t watermark) {
    if (!curve) return -1;

    spl_retention_sample_t *s = &curve->samples[curve->sample_idx];
    s->timestamp = timestamp;
    s->watermark = watermark;

    curve->sample_idx = (curve->sample_idx + 1) % SPL_RETENTION_SAMPLE_COUNT;
    if (curve->sample_count < SPL_RETENTION_SAMPLE_COUNT)
        curve->sample_count++;

    /* Recompute linear regression after adding sample */
    if (curve->sample_count >= 2) {
        /* Compute means */
        int64_t sum_t = 0, sum_w = 0;
        for (uint8_t i = 0; i < curve->sample_count; i++) {
            sum_t += (int64_t)curve->samples[i].timestamp;
            sum_w += (int64_t)curve->samples[i].watermark;
        }
        int64_t mean_t = sum_t / curve->sample_count;
        int64_t mean_w = sum_w / curve->sample_count;

        /* Compute slope: sum((t-mean_t)*(w-mean_w)) / sum((t-mean_t)^2) */
        int64_t num = 0, den = 0;
        int64_t ss_res = 0, ss_tot = 0;
        for (uint8_t i = 0; i < curve->sample_count; i++) {
            int64_t dt = (int64_t)curve->samples[i].timestamp - mean_t;
            int64_t dw = (int64_t)curve->samples[i].watermark - mean_w;
            num += dt * dw;
            den += dt * dt;
            ss_tot += dw * dw;
        }

        if (den != 0) {
            curve->slope = (num * 1000) / den;  /* slope * 1000 */
            curve->intercept = mean_w - (curve->slope * mean_t) / 1000;
        }

        /* R-squared = 1 - SS_res/SS_tot */
        if (ss_tot != 0 && den != 0) {
            for (uint8_t i = 0; i < curve->sample_count; i++) {
                int64_t predicted = (curve->slope * (int64_t)curve->samples[i].timestamp) / 1000
                                    + curve->intercept;
                int64_t residual = (int64_t)curve->samples[i].watermark - predicted;
                ss_res += residual * residual;
            }
            if (ss_res < ss_tot) {
                curve->r_squared = (uint32_t)(1000 - (ss_res * 1000) / ss_tot);
            } else {
                curve->r_squared = 0;
            }
        }
    }

    return 0;
}

/*
 * Predict watermark value at a future timestamp.
 * Returns predicted watermark in bytes.
 */
uint64_t spl_retention_curve_predict(const spl_retention_curve_t *curve,
                                     uint64_t future_timestamp) {
    if (!curve || curve->sample_count < 2) return 0;

    int64_t predicted = (curve->slope * (int64_t)future_timestamp) / 1000
                        + curve->intercept;
    return predicted > 0 ? (uint64_t)predicted : 0;
}

/*
 * Estimate seconds until threshold is reached.
 * Returns 0 if already exceeded, UINT32_MAX if growth rate is zero/negative.
 */
uint32_t spl_retention_curve_time_to_threshold(const spl_retention_curve_t *curve,
                                               uint64_t current_watermark,
                                               uint64_t current_timestamp) {
    if (!curve || curve->slope <= 0) return UINT32_MAX;
    if (current_watermark >= curve->threshold) return 0;

    uint64_t remaining = curve->threshold - current_watermark;
    /* time = remaining / (slope / 1000) = remaining * 1000 / slope */
    uint64_t seconds = (remaining * 1000) / (uint64_t)curve->slope;
    return seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
}

/*
 * Compute adaptive GC scheduling interval based on growth rate.
 * Returns recommended GC interval in seconds.
 */
uint32_t spl_retention_gc_interval(const spl_retention_curve_t *curve,
                                   uint64_t current_watermark) {
    if (!curve) return 300;  /* default: 5 minutes */

    uint32_t tta = spl_retention_curve_time_to_threshold(
        curve, current_watermark, 0);

    if (tta == UINT32_MAX) return 3600;  /* no growth: check hourly */
    if (tta == 0) return 10;             /* urgent: every 10s */
    if (tta < 60) return 10;             /* <1min: every 10s */
    if (tta < 300) return 30;            /* <5min: every 30s */
    if (tta < 3600) return 60;           /* <1hr: every minute */
    return 300;                          /* otherwise: every 5min */
}
