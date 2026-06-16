#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <zephyr/drivers/can.h>
#include <stdint.h>

#define CAN_RETURN_ID 0x123

struct can_payload {
    uint32_t can_id;
    uint8_t  dlc;
    uint8_t  data[8];
} __packed;

extern const struct device *const can1_dev;

int can_handler_init(void);
int can_send_return(uint32_t ts_us, uint32_t seq);

#endif
