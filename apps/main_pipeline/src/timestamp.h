#ifndef TIMESTAMP_H
#define TIMESTAMP_H

#include <zephyr/device.h>
#include <stdint.h>

extern const struct device *counter_dev; 

void timestamp_init(void);
uint32_t get_hw_timestamp_us(void);
uint32_t get_timer_max_us(void);

#endif