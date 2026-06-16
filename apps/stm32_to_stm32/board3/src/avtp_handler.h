#ifndef AVTP_HANDLER_H
#define AVTP_HANDLER_H

#include <stdint.h>

#define STREAM_ID    0xAABBCCDDEEFF0001ULL
#define MAX_PDU_SIZE 1500

int avtp_parse_frame(const uint8_t *pdu, int len,
                     uint32_t *ts_us_out, uint32_t *can_seq_out,
                     uint32_t *can_id_out, uint8_t *avtp_seq_out);

#endif
