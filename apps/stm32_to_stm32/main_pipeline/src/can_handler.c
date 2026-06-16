/*
 * can_handler.c — CAN1 TX / CAN2 RX latency measurement
 *
 * Extended CSV log format (parsed by bridge/mqtt_bridge.py):
 *
 *   CSV,<seq>,<tx_ticks>,<rx_ticks>,<lat_us>,<jit_us>,
 *       <min_us>,<max_us>,<mean_us>,<stddev_us>,<total>,<lost>
 *
 *   Field       Type     Description
 *   ─────────── ──────── ────────────────────────────────────────────
 *   seq         uint32   Frame sequence number (monotonic, from TX)
 *   tx_ticks    uint32   HW timer ticks captured just before can_send()
 *   rx_ticks    uint32   HW timer ticks captured at start of RX callback
 *   lat_us      uint32   One-way latency: ticks_to_us(rx_ticks - tx_ticks)
 *   jit_us      uint32   |lat_us - previous lat_us|
 *   min_us      uint32   Running minimum latency since boot/reset
 *   max_us      uint32   Running maximum latency since boot/reset
 *   mean_us     uint32   Running mean latency (Welford, truncated)
 *   stddev_us   uint32   Running population std-dev (Welford, truncated)
 *   total       uint32   Total RX frames counted
 *   lost        uint32   Frames lost (sequence-number gaps)
 *
 * prj.conf additions needed:
 *   CONFIG_FPU=y
 *   CONFIG_FPU_SHARING=y
 *   CONFIG_NEWLIB_LIBC=y
 */

#include "can_handler.h"
#include "timestamp.h"
#include "stats.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(can_handler, LOG_LEVEL_INF);

const struct device *const can1_dev = DEVICE_DT_GET(DT_NODELABEL(can1));
const struct device *const can2_dev = DEVICE_DT_GET(DT_NODELABEL(can2));

volatile bool can_send_enabled = false;
uint32_t      tx_sequence_num  = 0;

/* ── RX callback ────────────────────────────────────────────────────────── */

static void can_rx_callback(const struct device *dev,
                             struct can_frame    *frame,
                             void                *user_data)
{
    /* Capture RX timestamp as early as possible */
    uint32_t rx_ticks = get_hw_timestamp_ticks();

    /* Unpack TX timestamp + sequence number from frame payload */
    uint32_t tx_ticks, rx_seq_num;
    memcpy(&tx_ticks,   frame->data,     sizeof(uint32_t));
    memcpy(&rx_seq_num, &frame->data[4], sizeof(uint32_t));

    /* Tick subtraction handles 32-bit wraparound via unsigned underflow */
    uint32_t latency_us = ticks_to_us(rx_ticks - tx_ticks);

    /* Update statistics; returns jitter */
    uint32_t jitter_us = stats_record(latency_us, rx_seq_num);

    /* Grab a consistent snapshot for logging */
    can_stats_snapshot_t snap;
    stats_get_snapshot(&snap);

    /*
     * Extended CSV line — 11 data fields after the "CSV" prefix.
     * The Python bridge (mqtt_bridge.py) parses this and publishes
     * a rich JSON payload to MQTT topic stm32/can/metrics.
     *
     * Field order must match parse_csv_line() in mqtt_bridge.py.
     */
    printk("CSV,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
           rx_seq_num,
           tx_ticks,
           rx_ticks,
           latency_us,
           jitter_us,
           snap.min_us,
           snap.max_us,
           snap.mean_us,
           snap.stddev_us,
           snap.count,
           snap.lost);
}

/* ── Initialisation ─────────────────────────────────────────────────────── */

int can_handler_init(void)
{
    int ret;

    if (!device_is_ready(can1_dev) || !device_is_ready(can2_dev)) {
        printk("CAN devices not ready\n");
        return -ENODEV;
    }

    /* CAN1 — transmitter */
    can_set_mode(can1_dev, CAN_MODE_NORMAL);
    can_start(can1_dev);

    /* CAN2 — receiver */
    ret = can_set_mode(can2_dev, CAN_MODE_NORMAL);
    if (ret != 0) {
        printk("Error setting CAN2 mode [%d]\n", ret);
        return ret;
    }

    ret = can_start(can2_dev);
    if (ret != 0) {
        printk("Error starting CAN2 [%d]\n", ret);
        return ret;
    }

    /* Accept only frames with ID 0x123 */
    struct can_filter filter = {
        .flags = 0,
        .id    = 0x123,
        .mask  = CAN_STD_ID_MASK,
    };

    int filter_id = can_add_rx_filter(can2_dev, can_rx_callback, NULL, &filter);
    if (filter_id < 0) {
        printk("Error adding CAN2 RX filter [%d]\n", filter_id);
        return filter_id;
    }

    return 0;
}

/* ── TX ─────────────────────────────────────────────────────────────────── */

int can_send_frame(uint32_t seq_num)
{
    struct can_frame frame = {
        .id   = 0x123,
        .flags = 0,
        .dlc  = 8,
        .data = {0},
    };

    /* Pack sequence number first so the RX callback can detect gaps */
    memcpy(&frame.data[4], &seq_num, sizeof(uint32_t));

    /* Capture TX timestamp as late as possible — right before sending */
    uint32_t tx_ticks = get_hw_timestamp_ticks();
    memcpy(frame.data, &tx_ticks, sizeof(uint32_t));

    return can_send(can1_dev, &frame, K_MSEC(100), NULL, NULL);
}