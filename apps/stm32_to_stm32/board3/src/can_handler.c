#include "can_handler.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(can_handler, LOG_LEVEL_INF);

const struct device *const can1_dev = DEVICE_DT_GET(DT_NODELABEL(can1));

int can_handler_init(void)
{
    if (!device_is_ready(can1_dev)) {
        LOG_ERR("CAN1 device not ready");
        return -ENODEV;
    }

    can_set_mode(can1_dev, CAN_MODE_NORMAL);

    int ret = can_start(can1_dev);
    if (ret != 0) {
        LOG_ERR("CAN1 start failed: %d", ret);
        return ret;
    }

    LOG_INF("CAN1 ready (TX back to main_pipeline)");
    return 0;
}

int can_send_return(uint32_t ts_us, uint32_t seq)
{
    struct can_frame frame = {
        .id    = CAN_RETURN_ID,
        .flags = 0,
        .dlc   = 8,
        .data  = {0},
    };

    memcpy(&frame.data[0], &ts_us, sizeof(uint32_t));
    memcpy(&frame.data[4], &seq,   sizeof(uint32_t));

    return can_send(can1_dev, &frame, K_MSEC(100), NULL, NULL);
}
