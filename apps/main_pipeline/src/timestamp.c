#include "timestamp.h"
#include <zephyr/drivers/counter.h>
#include <zephyr/devicetree.h>

#define TIMER_NODE DT_NODELABEL(counter2)

const struct device *counter_dev = DEVICE_DT_GET(TIMER_NODE);

void timestamp_init(void)
{
    struct counter_top_cfg top_cfg = {
        .callback = NULL,
        .ticks = counter_get_max_top_value(counter_dev),
        .user_data = NULL,
        .flags = 0,
    };

    counter_set_top_value(counter_dev, &top_cfg);
    counter_start(counter_dev);
}

uint32_t get_hw_timestamp_us(void)
{
    uint32_t ticks;
    counter_get_value(counter_dev, &ticks);
    return counter_ticks_to_us(counter_dev, ticks);
}