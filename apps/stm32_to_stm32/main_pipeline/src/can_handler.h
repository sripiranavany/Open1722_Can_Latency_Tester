#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <zephyr/drivers/can.h>
#include <stdbool.h>
#include <stdint.h>

extern const struct device *const can1_dev;
extern const struct device *const can2_dev;
extern volatile bool can_send_enabled;
extern uint32_t tx_sequence_num;

int can_handler_init(void);
int can_send_frame(uint32_t timestamp_us, uint32_t seq_num);

#endif