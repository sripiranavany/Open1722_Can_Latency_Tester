#ifndef STATS_H
#define STATS_H

#include <stdint.h>

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
};

extern struct latency_stats stats;

void stats_reset(void);
uint32_t stats_record(uint32_t latency_us, uint32_t rx_seq_num);
void stats_print(uint32_t tx_count);

#endif