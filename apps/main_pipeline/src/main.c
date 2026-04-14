#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>

#define SLEEP_TIME_MS   50

/* Get button from devicetree */
#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static struct gpio_callback button_cb_data;

/* CAN send control flag */
static volatile bool can_send_enabled = true;

const struct device *const can1_dev = DEVICE_DT_GET(DT_NODELABEL(can1));
const struct device *const can2_dev = DEVICE_DT_GET(DT_NODELABEL(can2));

/* Transmission sequence number */
static uint32_t tx_sequence_num = 0;

/* Latency statistics */
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

static struct latency_stats stats = {
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

void can_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data)
{
    uint32_t rx_timestamp_us, tx_timestamp_us, latency_us;
    uint32_t rx_seq_num;
    uint32_t jitter_us = 0;
    
    /* Get receive timestamp in microseconds */
    rx_timestamp_us = k_cyc_to_us_floor32(k_cycle_get_32());
    
    /* Extract send timestamp from payload (first 4 bytes) */
    memcpy(&tx_timestamp_us, frame->data, sizeof(uint32_t));
    
    /* Extract sequence number from payload (next 4 bytes) */
    memcpy(&rx_seq_num, &frame->data[4], sizeof(uint32_t));
    
    /* Calculate latency */
    latency_us = rx_timestamp_us - tx_timestamp_us;
    
    /* Detect packet loss */
    if (stats.count > 0) {
        uint32_t expected_seq = stats.last_rx_seq + 1;
        if (rx_seq_num != expected_seq) {
            uint32_t lost = rx_seq_num - expected_seq;
            stats.lost_packets += lost;
            printk("[WARN] Packet loss detected! Expected seq=%u, got=%u (lost=%u)\n",
                   expected_seq, rx_seq_num, lost);
        }
    }
    stats.last_rx_seq = rx_seq_num;
    
    /* Calculate jitter (variation in latency) */
    if (stats.count > 0) {
        if (latency_us > stats.prev_latency_us) {
            jitter_us = latency_us - stats.prev_latency_us;
        } else {
            jitter_us = stats.prev_latency_us - latency_us;
        }
        stats.total_jitter_us += jitter_us;
        if (jitter_us > stats.max_jitter_us) {
            stats.max_jitter_us = jitter_us;
        }
    }
    stats.prev_latency_us = latency_us;
    
    /* Update statistics */
    stats.count++;
    stats.total_us += latency_us;
    if (latency_us < stats.min_us) {
        stats.min_us = latency_us;
    }
    if (latency_us > stats.max_us) {
        stats.max_us = latency_us;
    }
    
    /* CSV format: SEQ,TX_TIME,RX_TIME,LATENCY,JITTER */
    printk("CSV,%u,%u,%u,%u,%u\n",
           rx_seq_num,
           tx_timestamp_us,
           rx_timestamp_us,
           latency_us,
           jitter_us);
    
    /* Human-readable format */
    printk("[RX] Seq=%u, Latency=%u us, Jitter=%u us (Min=%u, Max=%u, Avg=%u)\n",
           rx_seq_num,
           latency_us,
           jitter_us,
           stats.min_us,
           stats.max_us,
           stats.total_us / stats.count);
}

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    uint32_t timestamp = k_uptime_get_32();
    
    /* Toggle CAN send state */
    can_send_enabled = !can_send_enabled;
    
    if (can_send_enabled) {
        printk("[%u ms] *** CAN SENDING RESUMED ***\n", timestamp);
    } else {
        printk("[%u ms] *** CAN SENDING PAUSED ***\n", timestamp);
        
        /* Print statistics when paused */
        if (stats.count > 0) {
            uint32_t avg_latency = stats.total_us / stats.count;
            uint32_t avg_jitter = (stats.count > 1) ? 
                                  (stats.total_jitter_us / (stats.count - 1)) : 0;
            
            printk("\n");
            printk("==========================================\n");
            printk("       LATENCY STATISTICS SUMMARY        \n");
            printk("==========================================\n");
            printk("Messages Sent:     %u\n", tx_sequence_num);
            printk("Messages Received: %u\n", stats.count);
            printk("Packets Lost:      %u\n", stats.lost_packets);
            printk("Packet Loss Rate:  %u.%02u%%\n", 
                   (stats.lost_packets * 100) / tx_sequence_num,
                   ((stats.lost_packets * 10000) / tx_sequence_num) % 100);
            printk("------------------------------------------\n");
            printk("Latency (µs):\n");
            printk("  Min:      %u\n", stats.min_us);
            printk("  Max:      %u\n", stats.max_us);
            printk("  Average:  %u\n", avg_latency);
            printk("  Range:    %u\n", stats.max_us - stats.min_us);
            printk("------------------------------------------\n");
            printk("Jitter (µs):\n");
            printk("  Max:      %u\n", stats.max_jitter_us);
            printk("  Average:  %u\n", avg_jitter);
            printk("==========================================\n");
            printk("\n");
        }
    }
}

void main(void)
{
    int ret;
    struct can_frame frame_can1 = {
        .id = 0x123,
        .flags = 0,
        .dlc = 8,
        .data = {0}  // Will be filled with timestamp and sequence
    };

    /* Check if CAN devices are ready */
    if (!device_is_ready(can1_dev) || !device_is_ready(can2_dev)) {
        printk("CAN devices not ready\n");
        return;
    }

    /* Check if button is ready */
    if (!gpio_is_ready_dt(&button)) {
        printk("Error: button device %s is not ready\n", button.port->name);
        return;
    }

    /* Configure button GPIO */
    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret != 0) {
        printk("Error %d: failed to configure %s pin %d\n",
               ret, button.port->name, button.pin);
        return;
    }

    /* Configure button interrupt */
    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        printk("Error %d: failed to configure interrupt on %s pin %d\n",
               ret, button.port->name, button.pin);
        return;
    }

    /* Initialize button callback */
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    printk("User button configured. Press to toggle CAN sending.\n");

    /* Start CAN1 (sender) */
    can_set_mode(can1_dev, CAN_MODE_NORMAL);
    can_start(can1_dev);

    /* Start CAN2 (receiver) */
    ret = can_set_mode(can2_dev, CAN_MODE_NORMAL);
    if (ret != 0) {
        printk("Error setting CAN2 mode [%d]\n", ret);
        return;
    }

    ret = can_start(can2_dev);
    if (ret != 0) {
        printk("Error starting CAN2 [%d]\n", ret);
        return;
    }

    /* Add receive filter for CAN2 */
    struct can_filter filter = {
        .flags = 0,
        .id = 0x123,
        .mask = CAN_STD_ID_MASK
    };

    int filter_id = can_add_rx_filter(can2_dev, can_rx_callback, NULL, &filter);
    if (filter_id < 0) {
        printk("Error adding CAN2 RX filter [%d]\n", filter_id);
        return;
    }

    printk("\n");
    printk("==========================================\n");
    printk("    CAN Latency Tester - Initialized     \n");
    printk("==========================================\n");
    printk("Configuration:\n");
    printk("  CAN1 (TX) -> Pi1 -> Ethernet -> Pi2 -> CAN2 (RX)\n");
    printk("  Bus Speed: 250 kbps\n");
    printk("  Send Interval: %d ms\n", SLEEP_TIME_MS);
    printk("==========================================\n");
    printk("CSV Header: SEQ,TX_TIME_US,RX_TIME_US,LATENCY_US,JITTER_US\n");
    printk("==========================================\n");
    printk("\n");

    while (1) {
        /* Only send if enabled */
        if (can_send_enabled) {
            /* Get current timestamp in microseconds */
            uint32_t tx_timestamp_us = k_cyc_to_us_floor32(k_cycle_get_32());
            
            /* Embed timestamp in first 4 bytes of payload */
            memcpy(frame_can1.data, &tx_timestamp_us, sizeof(uint32_t));
            
            /* Embed sequence number in next 4 bytes */
            memcpy(&frame_can1.data[4], &tx_sequence_num, sizeof(uint32_t));
            
            ret = can_send(can1_dev, &frame_can1, K_MSEC(100), NULL, NULL);
            if (ret != 0) {
                printk("[TX] Send failed [%d] for seq=%u\n", ret, tx_sequence_num);
            } else {
                printk("[TX] Seq=%u sent @ %u us\n", tx_sequence_num, tx_timestamp_us);
            }
            
            tx_sequence_num++;
        }
        
        k_msleep(SLEEP_TIME_MS);
    }
}