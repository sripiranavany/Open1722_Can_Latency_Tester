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

LOG_MODULE_REGISTER(avtp_listener, LOG_LEVEL_INF);

#define STREAM_ID      0xAABBCCDDEEFF0001ULL
#define MAX_PDU_SIZE   1500

/* CAN return path — must match main_pipeline CAN2 filter */
#define CAN_RETURN_ID  0x123

/* Must match board2's can_payload struct */
struct can_payload {
	uint32_t can_id;
	uint8_t  dlc;
	uint8_t  data[8];
} __packed;

const struct device *const can1_dev = DEVICE_DT_GET(DT_NODELABEL(can1));

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

	LOG_INF("CAN1 ready (TX back to main_pipeline)");
	return 0;
}

static int send_can_return(uint32_t ts_us, uint32_t seq)
{
	struct can_frame frame = {
		.id   = CAN_RETURN_ID,
		.flags = 0,
		.dlc  = 8,
		.data = {0},
	};

	/* Same layout main_pipeline expects in can_rx_callback:
	 *   data[0..3] = tx_timestamp_us
	 *   data[4..7] = seq_num
	 */
	memcpy(&frame.data[0], &ts_us, sizeof(uint32_t));
	memcpy(&frame.data[4], &seq,   sizeof(uint32_t));

	return can_send(can1_dev, &frame, K_MSEC(100), NULL, NULL);
}

int main(void)
{
	uint8_t pdu[MAX_PDU_SIZE];
	int sock, ret;
	struct sockaddr_ll bind_addr = {0};

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

	bind_addr.sll_family   = AF_PACKET;
	bind_addr.sll_protocol = htons(ETH_P_TSN);
	bind_addr.sll_ifindex  = net_if_get_by_iface(net_if_get_default());

	ret = bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
	if (ret < 0) {
		LOG_ERR("bind() failed: %d", errno);
		close(sock);
		return -1;
	}

	LOG_INF("AVTP listener + CAN return bridge ready");

	while (1) {
		ret = recv(sock, pdu, sizeof(pdu), 0);
		if (ret < 0) {
			LOG_ERR("recv() failed: %d", errno);
			continue;
		}

		if (ret < (int)AVTP_NTSCF_HEADER_LEN) {
			continue;
		}

		Avtp_Ntscf_t *ntscf = (Avtp_Ntscf_t *)pdu;
		if (!Avtp_Ntscf_IsValid(ntscf, ret)) {
			continue;
		}

		if (Avtp_Ntscf_GetStreamId(ntscf) != STREAM_ID) {
			continue;
		}

		uint8_t avtp_seq = Avtp_Ntscf_GetSequenceNum(ntscf);

		/* Parse GPC ACF payload */
		uint8_t *acf_ptr = pdu + AVTP_NTSCF_HEADER_LEN;
		int remaining    = ret - AVTP_NTSCF_HEADER_LEN;

		if (remaining < (int)AVTP_GPC_HEADER_LEN) {
			continue;
		}

		Avtp_Gpc_t *gpc      = (Avtp_Gpc_t *)acf_ptr;
		uint16_t acf_total   = Avtp_Gpc_GetAcfMsgLength(gpc) * 4;
		uint16_t payload_len = acf_total - AVTP_GPC_HEADER_LEN;

		if (payload_len < sizeof(struct can_payload)) {
			continue;
		}

		struct can_payload cpkt;
		memcpy(&cpkt, acf_ptr + AVTP_GPC_HEADER_LEN, sizeof(cpkt));

		uint32_t ts_us, can_seq;
		memcpy(&ts_us,   &cpkt.data[0], sizeof(uint32_t));
		memcpy(&can_seq, &cpkt.data[4], sizeof(uint32_t));

		LOG_INF("[RX] avtp_seq=%u can_id=0x%03x can_seq=%u tx_ts=%u us",
			avtp_seq, cpkt.can_id, can_seq, ts_us);

		/* Send CAN frame back to main_pipeline CAN2 */
		ret = send_can_return(ts_us, can_seq);
		if (ret != 0) {
			LOG_ERR("CAN return TX failed: %d", ret);
		} else {
			LOG_INF("[TX] CAN return: seq=%u ts=%u us", can_seq, ts_us);
		}
	}

	close(sock);
	return 0;
}
