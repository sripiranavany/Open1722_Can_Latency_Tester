#ifndef AVTP_HANDLER_H
#define AVTP_HANDLER_H

#include <zephyr/drivers/can.h>
#include <stdint.h>

#define STREAM_ID    0xAABBCCDDEEFF0001ULL
#define GPC_MSG_ID   0x01
#define MAX_PDU_SIZE 1500

int avtp_build_frame(uint8_t *pdu, uint8_t seq_num, const struct can_frame *cf);

#endif
