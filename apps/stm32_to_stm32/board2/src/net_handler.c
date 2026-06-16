#include "net_handler.h"
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/logging/log.h>
#include <arpa/inet.h>
#include <string.h>

LOG_MODULE_REGISTER(net_handler, LOG_LEVEL_INF);

#define AVTP_MCAST_MAC { 0x01, 0x1B, 0x19, 0x00, 0x00, 0x00 }

static K_SEM_DEFINE(iface_ready, 0, 1);

static void iface_up_handler(struct net_mgmt_event_callback *cb,
                             uint64_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IF_UP) {
        k_sem_give(&iface_ready);
    }
}

static void wait_for_iface(void)
{
    struct net_if *iface = net_if_get_default();
    struct net_mgmt_event_callback cb;

    if (net_if_is_up(iface)) {
        return;
    }

    net_mgmt_init_event_callback(&cb, iface_up_handler, NET_EVENT_IF_UP);
    net_mgmt_add_event_callback(&cb);
    k_sem_take(&iface_ready, K_FOREVER);
    net_mgmt_del_event_callback(&cb);
}

int net_handler_init(int *sock_out, struct sockaddr_ll *sk_addr_out)
{
    uint8_t dst_mac[] = AVTP_MCAST_MAC;

    LOG_INF("Waiting for Ethernet link...");
    wait_for_iface();
    LOG_INF("Ethernet link up");

    int sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
    if (sock < 0) {
        LOG_ERR("socket() failed: %d", errno);
        return -1;
    }

    sk_addr_out->sll_family   = AF_PACKET;
    sk_addr_out->sll_protocol = htons(ETH_P_TSN);
    sk_addr_out->sll_halen    = NET_ETH_ADDR_LEN;
    sk_addr_out->sll_ifindex  = net_if_get_by_iface(net_if_get_default());
    memcpy(sk_addr_out->sll_addr, dst_mac, NET_ETH_ADDR_LEN);

    *sock_out = sock;
    return 0;
}
