#include "stats.h"
#include <zephyr/kernel.h>
#include <limits.h>

struct latency_stats stats = {
    .min_us = UINT32_MAX,
    .max_us = 0,
    .total_us = 0,
    .count = 0,
    .prev_latency_us = 0,
    .total_jitter_us = 0,
    .max_jitter_us = 0,
    .lost_packets = 0,
    .last_rx_seq = 0,
    .sum_sq_us = 0
};

/* Integer square root via Newton's method — no FPU needed */
static uint32_t isqrt64(uint64_t n)
{
    if (n == 0) return 0;
    uint64_t x = n;
    uint64_t y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return (uint32_t)x;
}

void stats_reset(void){
    stats.min_us = UINT32_MAX;
    stats.max_us = 0;
    stats.total_us = 0;
    stats.count = 0;
    stats.prev_latency_us = 0;
    stats.total_jitter_us = 0;
    stats.max_jitter_us = 0;
    stats.lost_packets = 0;
    stats.last_rx_seq = 0;
    stats.sum_sq_us = 0;
};

uint32_t stats_record(uint32_t latency_us, uint32_t rx_seq_num) {
    // Packet loss detection
    if (stats.count > 0) {
        uint32_t expected_seq = stats.last_rx_seq + 1;
        if (rx_seq_num != expected_seq) {
            uint32_t lost = rx_seq_num - expected_seq;
            stats.lost_packets += lost;
            /* only print for small gaps — large gaps (e.g. Pi in loop) flood the UART */
            if (lost <= 10) {
                printk("Packet loss: seq gap %u→%u (lost %u)\n",
                       expected_seq, rx_seq_num, lost);
            }
        }
    }
    stats.last_rx_seq = rx_seq_num;

    // Jitter calculation
    uint32_t jitter_us = 0;
    if (stats.count > 0) {
        jitter_us = (latency_us > stats.prev_latency_us) ? (latency_us - stats.prev_latency_us) : (stats.prev_latency_us - latency_us);
        stats.total_jitter_us += jitter_us;
        if (jitter_us > stats.max_jitter_us) {
            stats.max_jitter_us = jitter_us;
        }
    }
    stats.prev_latency_us = latency_us;

    // Min/Max/Total/SumSq
    stats.count++;
    stats.total_us += latency_us;
    stats.sum_sq_us += (uint64_t)latency_us * latency_us;
    if (latency_us < stats.min_us) stats.min_us = latency_us;
    if (latency_us > stats.max_us) stats.max_us = latency_us;
    return jitter_us;
}

void stats_get_snapshot(can_stats_snapshot_t *out)
{
    out->count     = stats.count;
    out->lost      = stats.lost_packets;
    out->min_us    = stats.min_us;
    out->max_us    = stats.max_us;
    out->mean_us   = (stats.count > 0) ? stats.total_us / stats.count : 0;
    out->jitter_us = (stats.count > 1)
                     ? stats.total_jitter_us / (stats.count - 1) : 0;

    /* Population stddev = sqrt(E[X²] - (E[X])²), integer arithmetic only */
    if (stats.count > 1) {
        uint64_t avg_sq  = stats.sum_sq_us / stats.count;
        uint64_t mean_sq = (uint64_t)out->mean_us * out->mean_us;
        uint64_t variance = (avg_sq > mean_sq) ? avg_sq - mean_sq : 0;
        out->stddev_us = isqrt64(variance);
    } else {
        out->stddev_us = 0;
    }
}

void stats_print(uint32_t tx_count)
{
    if (stats.count == 0) return;

    uint32_t avg_latency = stats.total_us / stats.count;
    uint32_t avg_jitter  = (stats.count > 1)
                           ? stats.total_jitter_us / (stats.count - 1) : 0;
    uint64_t avg_sq      = stats.sum_sq_us / stats.count;
    uint64_t mean_sq     = (uint64_t)avg_latency * avg_latency;
    uint32_t stddev      = isqrt64((avg_sq > mean_sq) ? avg_sq - mean_sq : 0);

    printk("\n==========================================\n");
    printk("       LATENCY STATISTICS SUMMARY        \n");
    printk("==========================================\n");
    printk("Messages Sent:     %u\n", tx_count);
    printk("Messages Received: %u\n", stats.count);
    printk("Packets Lost:      %u\n", stats.lost_packets);
    printk("Packet Loss Rate:  %u.%02u%%\n",
           (stats.lost_packets * 100) / tx_count,
           ((stats.lost_packets * 10000) / tx_count) % 100);
    printk("------------------------------------------\n");
    printk("Latency (us):\n");
    printk("  Min:      %u\n", stats.min_us);
    printk("  Max:      %u\n", stats.max_us);
    printk("  Average:  %u\n", avg_latency);
    printk("  Std Dev:  %u\n", stddev);
    printk("  Range:    %u\n", stats.max_us - stats.min_us);
    printk("------------------------------------------\n");
    printk("Jitter (us):\n");
    printk("  Max:      %u\n", stats.max_jitter_us);
    printk("  Average:  %u\n", avg_jitter);
    printk("==========================================\n\n");
}