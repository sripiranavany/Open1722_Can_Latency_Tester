#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <unistd.h>
#include <string.h>
#include "can_handler.h"
#include "net_handler.h"
#include "avtp_handler.h"

LOG_MODULE_REGISTER(avtp_can_bridge, LOG_LEVEL_INF);

int main(void)
{
    uint8_t pdu[MAX_PDU_SIZE];
    uint8_t seq_num = 0;
    int sock, ret;
    struct sockaddr_ll sk_addr = {0};

    ret = can_handler_init();
    if (ret < 0) {
        return ret;
    }

    ret = net_handler_init(&sock, &sk_addr);
    if (ret < 0) {
        return ret;
    }

    LOG_INF("CAN->AVTP bridge running (CAN ID=0x%03x -> Ethernet)", CAN_FILTER_ID);

    struct can_frame cf;

    while (1) {
        ret = k_msgq_get(&can_rx_msgq, &cf, K_FOREVER);
        if (ret != 0) {
            continue;
        }

        uint32_t ts_us, rx_seq;
        memcpy(&ts_us,  &cf.data[0], sizeof(uint32_t));
        memcpy(&rx_seq, &cf.data[4], sizeof(uint32_t));

        LOG_INF("CAN RX: id=0x%03x seq=%u ts=%u us", cf.id, rx_seq, ts_us);

        int len = avtp_build_frame(pdu, seq_num++, &cf);

        ret = sendto(sock, pdu, len, 0,
                     (struct sockaddr *)&sk_addr, sizeof(sk_addr));
        if (ret < 0) {
            LOG_ERR("sendto() failed: %d", errno);
        } else {
            LOG_INF("AVTP TX: avtp_seq=%d can_seq=%u (%d bytes)",
                    seq_num - 1, rx_seq, ret);
        }
    }

    close(sock);
    return 0;
}
