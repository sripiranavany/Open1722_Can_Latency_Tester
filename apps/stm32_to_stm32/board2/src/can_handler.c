#include "can_handler.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(can_handler, LOG_LEVEL_INF);

const struct device *const can1_dev = DEVICE_DT_GET(DT_NODELABEL(can1));

CAN_MSGQ_DEFINE(can_rx_msgq, CAN_RX_QUEUE_SZ);

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

    struct can_filter filter = {
        .flags = 0,
        .id    = CAN_FILTER_ID,
        .mask  = CAN_STD_ID_MASK,
    };

    int fid = can_add_rx_filter_msgq(can1_dev, &can_rx_msgq, &filter);
    if (fid < 0) {
        LOG_ERR("CAN1 RX filter failed: %d", fid);
        return fid;
    }

    LOG_INF("CAN1 ready, filter 0x%03x", CAN_FILTER_ID);
    return 0;
}
