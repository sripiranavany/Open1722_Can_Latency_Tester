#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/posix/sys/socket.h>
#include <unistd.h>
#include "can_handler.h"
#include "net_handler.h"
#include "avtp_handler.h"

LOG_MODULE_REGISTER(avtp_listener, LOG_LEVEL_INF);

int main(void)
{
    uint8_t pdu[MAX_PDU_SIZE];
    int sock, ret;

    ret = can_handler_init();
    if (ret < 0) {
        return ret;
    }

    ret = net_handler_init(&sock);
    if (ret < 0) {
        return ret;
    }

    LOG_INF("AVTP listener + CAN return bridge ready");

    while (1) {
        ret = recv(sock, pdu, sizeof(pdu), 0);
        if (ret < 0) {
            LOG_ERR("recv() failed: %d", errno);
            continue;
        }

        uint32_t ts_us, can_seq, can_id;
        uint8_t avtp_seq;

        if (avtp_parse_frame(pdu, ret, &ts_us, &can_seq, &can_id, &avtp_seq) != 0) {
            continue;
        }

        LOG_INF("[RX] avtp_seq=%u can_id=0x%03x can_seq=%u tx_ts=%u us",
                avtp_seq, can_id, can_seq, ts_us);

        ret = can_send_return(ts_us, can_seq);
        if (ret != 0) {
            LOG_ERR("CAN return TX failed: %d", ret);
        } else {
            LOG_INF("[TX] CAN return: seq=%u ts=%u us", can_seq, ts_us);
        }
    }

    close(sock);
    return 0;
}
