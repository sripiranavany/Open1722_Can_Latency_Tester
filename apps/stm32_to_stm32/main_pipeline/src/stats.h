#ifndef STATS_H
#define STATS_H

/*
 * stats.h — CAN latency statistics (extended)
 *
 * Uses Welford's online algorithm for mean + population std-dev,
 * which requires only O(1) memory and runs stably for large sample counts.
 *
 * Thread-safety: all functions are protected by an internal Zephyr mutex
 * and safe to call from both the CAN RX ISR thread and a logging thread.
 *
 * Zephyr config needed in prj.conf:
 *   CONFIG_FPU=y               # STM32F4 FPU for double arithmetic
 *   CONFIG_FPU_SHARING=y       # share FPU between threads
 *   CONFIG_NEWLIB_LIBC=y       # for sqrt()  (or use CONFIG_MINIMAL_LIBC
 *                              #   and provide your own isqrt if FP is off)
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct latency_stats {
    uint32_t min_us;
    uint32_t max_us;
    uint32_t total_us;
    uint32_t count;
    uint32_t prev_latency_us;
    uint32_t total_jitter_us;
    uint32_t max_jitter_us;
    uint32_t lost_packets;
    uint32_t last_rx_seq;
    uint64_t sum_sq_us;   /* sum of latency_us^2 — for population stddev */
};

typedef struct {
    uint32_t count;
    uint32_t lost;
    uint32_t min_us;
    uint32_t max_us;
    uint32_t mean_us;
    uint32_t stddev_us;
    uint32_t jitter_us;
} can_stats_snapshot_t;

/**
 * @brief Record a new latency sample.
 *
 * Call from the CAN RX callback after computing latency_us.
 *
 * @param latency_us  One-way frame latency in microseconds.
 * @param seq_num     Sequence number from the CAN frame payload.
 * @return            Jitter (|latency_us - previous latency_us|).
 */
uint32_t stats_record(uint32_t latency_us, uint32_t seq_num);
void stats_get_snapshot(can_stats_snapshot_t *out);
void stats_reset(void);
void stats_print(uint32_t tx_count);

#ifdef __cplusplus
}
#endif

#endif /* STATS_H */