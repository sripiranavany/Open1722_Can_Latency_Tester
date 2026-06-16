#include "avtp_handler.h"
#include "can_handler.h"
#include "avtp/acf/Ntscf.h"
#include "avtp/acf/Gpc.h"
#include <string.h>

int avtp_parse_frame(const uint8_t *pdu, int len,
                     uint32_t *ts_us_out, uint32_t *can_seq_out,
                     uint32_t *can_id_out, uint8_t *avtp_seq_out)
{
    if (len < (int)AVTP_NTSCF_HEADER_LEN) {
        return -1;
    }

    const Avtp_Ntscf_t *ntscf = (const Avtp_Ntscf_t *)pdu;
    if (!Avtp_Ntscf_IsValid(ntscf, len)) {
        return -1;
    }

    if (Avtp_Ntscf_GetStreamId(ntscf) != STREAM_ID) {
        return -1;
    }

    *avtp_seq_out = Avtp_Ntscf_GetSequenceNum(ntscf);

    const uint8_t *acf_ptr = pdu + AVTP_NTSCF_HEADER_LEN;
    int remaining           = len - AVTP_NTSCF_HEADER_LEN;

    if (remaining < (int)AVTP_GPC_HEADER_LEN) {
        return -1;
    }

    const Avtp_Gpc_t *gpc = (const Avtp_Gpc_t *)acf_ptr;
    uint16_t acf_total     = Avtp_Gpc_GetAcfMsgLength(gpc) * 4;
    uint16_t payload_len   = acf_total - AVTP_GPC_HEADER_LEN;

    if (payload_len < sizeof(struct can_payload)) {
        return -1;
    }

    struct can_payload cpkt;
    memcpy(&cpkt, acf_ptr + AVTP_GPC_HEADER_LEN, sizeof(cpkt));

    memcpy(ts_us_out,   &cpkt.data[0], sizeof(uint32_t));
    memcpy(can_seq_out, &cpkt.data[4], sizeof(uint32_t));
    *can_id_out = cpkt.can_id;

    return 0;
}
