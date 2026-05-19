#ifndef IEC104_MASTER_IEC104_PRIVATE_H
#define IEC104_MASTER_IEC104_PRIVATE_H

#include "config/config.h"
#include "point_store/point_store.h"

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

#define IEC104_START 0x68
#define IEC104_MAX_APDU 255
#define IEC104_APCI_LEN 6
#define IEC104_ASDU_MAX (IEC104_MAX_APDU - IEC104_APCI_LEN)

#define IEC104_U_STARTDT_ACT 0x07
#define IEC104_U_STARTDT_CON 0x0b
#define IEC104_U_STOPDT_ACT 0x13
#define IEC104_U_STOPDT_CON 0x23
#define IEC104_U_TESTFR_ACT 0x43
#define IEC104_U_TESTFR_CON 0x83

#define TYPE_M_SP_NA_1 1
#define TYPE_M_DP_NA_1 3
#define TYPE_M_ME_NA_1 9
#define TYPE_M_ME_NB_1 11
#define TYPE_M_ME_NC_1 13
#define TYPE_M_IT_NA_1 15
#define TYPE_M_SP_TB_1 30
#define TYPE_M_DP_TB_1 31
#define TYPE_M_ME_TD_1 34
#define TYPE_M_ME_TE_1 35
#define TYPE_M_ME_TF_1 36
#define TYPE_M_IT_TB_1 37
#define TYPE_C_IC_NA_1 100

#define COT_ACTIVATION 6

typedef struct iec104_session {
    int fd;
    const gateway_config_t *gateway;
    uint16_t send_seq;
    uint16_t recv_seq;
    uint16_t last_ack_sent;
    int unacked_received;
    int startdt_confirmed;
    int testfr_pending;
    time_t last_rx;
    time_t last_tx;
    time_t last_gi;
    time_t last_testfr;
    struct timeval last_data_ack;
} iec104_session_t;

uint16_t iec104_read_le16(const uint8_t *buffer);
uint32_t iec104_read_le24(const uint8_t *buffer);
int16_t iec104_read_i16(const uint8_t *buffer);
int32_t iec104_read_i32(const uint8_t *buffer);
float iec104_read_float32(const uint8_t *buffer);
long iec104_elapsed_ms(const struct timeval *start, const struct timeval *end);

#endif
