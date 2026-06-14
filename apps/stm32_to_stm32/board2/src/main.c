#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/logging/log.h>
#include <zephyr/posix/sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#include "avtp/acf/Ntscf.h"
#include "avtp/acf/Gpc.h"

LOG_MODULE_REGISTER(avtp_can_bridge, LOG_LEVEL_INF);

/* CAN config — matches main_pipeline CAN1 */
#define CAN_FILTER_ID    0x123
#define CAN_RX_QUEUE_SZ  10

/* AVTP config */
#define AVTP_MCAST_MAC   { 0x01, 0x1B, 0x19, 0x00, 0x00, 0x00 }
#define STREAM_ID        0xAABBCCDDEEFF0001ULL
#define GPC_MSG_ID       0x01
#define MAX_PDU_SIZE     1500

/* CAN data layout from main_pipeline:
 *   data[0..3] = timestamp_us (uint32_t, little-endian)
 *   data[4..7] = seq_num      (uint32_t, little-endian)
 */
struct can_payload {
	uint32_t can_id;
	uint8_t  dlc;
	uint8_t  data[8];
} __packed;

const struct device *const can1_dev = DEVICE_DT_GET(DT_NODELABEL(can1));

CAN_MSGQ_DEFINE(can_rx_msgq, CAN_RX_QUEUE_SZ);

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

static int init_can(void)
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

static int build_avtp_frame(uint8_t *pdu, uint8_t seq_num,
			    const struct can_frame *cf)
{
	uint16_t pdu_len = 0;

	/* NTSCF header */
	Avtp_Ntscf_t *ntscf = (Avtp_Ntscf_t *)pdu;
	memset(ntscf, 0, AVTP_NTSCF_HEADER_LEN);
	Avtp_Ntscf_Init(ntscf);
	Avtp_Ntscf_SetSequenceNum(ntscf, seq_num);
	Avtp_Ntscf_SetStreamId(ntscf, STREAM_ID);
	pdu_len += AVTP_NTSCF_HEADER_LEN;

	/* GPC header + CAN payload */
	struct can_payload cpkt = {
		.can_id = cf->id,
		.dlc    = cf->dlc,
	};
	memcpy(cpkt.data, cf->data, MIN(cf->dlc, 8));

	Avtp_Gpc_t *gpc = (Avtp_Gpc_t *)(pdu + pdu_len);
	memset(gpc, 0, AVTP_GPC_HEADER_LEN);
	Avtp_Gpc_Init(gpc);
	Avtp_Gpc_SetGpcMsgId(gpc, GPC_MSG_ID);

	uint16_t payload_len  = sizeof(struct can_payload);
	uint8_t  acf_len_quad = (uint8_t)((AVTP_GPC_HEADER_LEN + payload_len + 3) / 4);
	Avtp_Gpc_SetAcfMsgLength(gpc, acf_len_quad);

	uint8_t *dst = pdu + pdu_len + AVTP_GPC_HEADER_LEN;
	memcpy(dst, &cpkt, payload_len);
	memset(dst + payload_len, 0, acf_len_quad * 4 - AVTP_GPC_HEADER_LEN - payload_len);

	uint16_t acf_total = acf_len_quad * 4;
	pdu_len += acf_total;

	Avtp_Ntscf_SetNtscfDataLength(ntscf, acf_total);

	return pdu_len;
}

int main(void)
{
	uint8_t dst_mac[] = AVTP_MCAST_MAC;
	uint8_t pdu[MAX_PDU_SIZE];
	uint8_t seq_num = 0;
	int sock, ret;
	struct sockaddr_ll sk_addr = {0};

	ret = init_can();
	if (ret < 0) {
		return ret;
	}

	LOG_INF("Waiting for Ethernet link...");
	wait_for_iface();
	LOG_INF("Ethernet link up");

	sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
	if (sock < 0) {
		LOG_ERR("socket() failed: %d", errno);
		return -1;
	}

	sk_addr.sll_family   = AF_PACKET;
	sk_addr.sll_protocol = htons(ETH_P_TSN);
	sk_addr.sll_halen    = NET_ETH_ADDR_LEN;
	sk_addr.sll_ifindex  = net_if_get_by_iface(net_if_get_default());
	memcpy(sk_addr.sll_addr, dst_mac, NET_ETH_ADDR_LEN);

	LOG_INF("CAN→AVTP bridge running (CAN ID=0x%03x → Ethernet)", CAN_FILTER_ID);

	struct can_frame cf;

	while (1) {
		/* Block until a CAN frame arrives */
		ret = k_msgq_get(&can_rx_msgq, &cf, K_FOREVER);
		if (ret != 0) {
			continue;
		}

		uint32_t ts_us, rx_seq;
		memcpy(&ts_us,  &cf.data[0], sizeof(uint32_t));
		memcpy(&rx_seq, &cf.data[4], sizeof(uint32_t));

		LOG_INF("CAN RX: id=0x%03x seq=%u ts=%u us", cf.id, rx_seq, ts_us);

		int len = build_avtp_frame(pdu, seq_num++, &cf);

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
