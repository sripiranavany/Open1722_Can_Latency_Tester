#include "avtp_handler.h"
#include "can_handler.h"
#include "avtp/acf/Ntscf.h"
#include "avtp/acf/Gpc.h"
#include <zephyr/kernel.h>
#include <string.h>

int avtp_build_frame(uint8_t *pdu, uint8_t seq_num, const struct can_frame *cf)
{
    uint16_t pdu_len = 0;

    Avtp_Ntscf_t *ntscf = (Avtp_Ntscf_t *)pdu;
    memset(ntscf, 0, AVTP_NTSCF_HEADER_LEN);
    Avtp_Ntscf_Init(ntscf);
    Avtp_Ntscf_SetSequenceNum(ntscf, seq_num);
    Avtp_Ntscf_SetStreamId(ntscf, STREAM_ID);
    pdu_len += AVTP_NTSCF_HEADER_LEN;

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
