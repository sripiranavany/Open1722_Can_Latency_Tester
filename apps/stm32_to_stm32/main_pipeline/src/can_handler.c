#include "can_handler.h"
#include "timestamp.h"
#include "stats.h"
#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(can_handler, LOG_LEVEL_INF);

const struct device *const can1_dev = DEVICE_DT_GET(DT_NODELABEL(can1));
const struct device *const can2_dev = DEVICE_DT_GET(DT_NODELABEL(can2));

volatile bool can_send_enabled = false;
uint32_t tx_sequence_num = 0;

static void can_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data)
{
    uint32_t rx_timestamp_us = get_hw_timestamp_us();
    uint32_t tx_timestamp_us, latency_us, rx_seq_num;

    memcpy(&tx_timestamp_us, frame->data, sizeof(uint32_t));
    memcpy(&rx_seq_num, &frame->data[4], sizeof(uint32_t));

    if (rx_timestamp_us >= tx_timestamp_us) {
        latency_us = rx_timestamp_us - tx_timestamp_us;
    } else {
        latency_us = rx_timestamp_us + (get_timer_max_us() - tx_timestamp_us);
    }

    uint32_t jitter_us = stats_record(latency_us, rx_seq_num);

    LOG_INF("CSV,%u,%u,%u,%u,%u", rx_seq_num, tx_timestamp_us, rx_timestamp_us, latency_us, jitter_us);
    // printk("CSV,%u,%u,%u,%u,%u\n", rx_seq_num, tx_timestamp_us, rx_timestamp_us, latency_us, jitter_us);
    // printk("[RX] Seq=%u, Latency=%u us, Jitter=%u us\n", rx_seq_num, latency_us, jitter_us);
}

int can_handler_init(void)
{
    int ret;

    if(!device_is_ready(can1_dev) || !device_is_ready(can2_dev)) {
        printk("CAN devices not ready\n");
        return -ENODEV;
    }

    // for the testing use the CAN_NORMAL mode, which is the default. In this mode, the CAN controller will automatically handle ACKs and retransmissions.
    // This allows us to focus on measuring the latency without worrying about packet loss or retries.
    can_set_mode(can1_dev, CAN_MODE_NORMAL);
    can_start(can1_dev);

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

    struct can_filter filter = {
        .flags = 0,
        .id = 0x123,
        .mask = CAN_STD_ID_MASK
    };

    int filter_id = can_add_rx_filter(can2_dev, can_rx_callback, NULL, &filter);
    if (filter_id < 0) {
        printk("Error adding CAN2 RX filter [%d]\n", filter_id);
        return filter_id;
    }

    return 0;
}

int can_send_frame(uint32_t timestamp_us, uint32_t seq_num)
{
    struct can_frame frame = {
        .id = 0x123,
        .flags = 0,
        .dlc = 8,
        .data = {0}
    };

    memcpy(frame.data, &timestamp_us, sizeof(uint32_t));
    memcpy(&frame.data[4], &seq_num, sizeof(uint32_t));

    return can_send(can1_dev, &frame, K_MSEC(100), NULL, NULL);
}