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
    .last_rx_seq = 0
};

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
};

uint32_t stats_record(uint32_t latency_us, uint32_t rx_seq_num) {
    // Packet loss detection
    if (stats.count > 0) {
        uint32_t expected_seq = stats.last_rx_seq + 1;
        if (rx_seq_num != expected_seq) {
            uint32_t lost = rx_seq_num - expected_seq;
            stats.lost_packets += lost;
            printk("Packet loss detected: expected seq %u, got %u, lost %u\n", expected_seq, rx_seq_num, lost);
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

    // Min/Max/Total
    stats.count++;
    stats.total_us += latency_us;
    if (latency_us < stats.min_us) stats.min_us = latency_us;
    if (latency_us > stats.max_us) stats.max_us = latency_us;
    return jitter_us;
}

void stats_print(uint32_t tx_count)
{
    if (stats.count == 0) return;

    uint32_t avg_latency = stats.total_us / stats.count;
    uint32_t avg_jitter = (stats.count > 1)
                          ? stats.total_jitter_us / (stats.count - 1) : 0;

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
    printk("  Range:    %u\n", stats.max_us - stats.min_us);
    printk("------------------------------------------\n");
    printk("Jitter (us):\n");
    printk("  Max:      %u\n", stats.max_jitter_us);
    printk("  Average:  %u\n", avg_jitter);
    printk("==========================================\n\n");
}