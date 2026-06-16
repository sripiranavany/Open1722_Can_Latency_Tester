#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <zephyr/drivers/can.h>
#include <stdint.h>

#define CAN_FILTER_ID   0x123
#define CAN_RX_QUEUE_SZ 10

struct can_payload {
    uint32_t can_id;
    uint8_t  dlc;
    uint8_t  data[8];
} __packed;

extern const struct device *const can1_dev;
extern struct k_msgq can_rx_msgq;

int can_handler_init(void);

#endif
