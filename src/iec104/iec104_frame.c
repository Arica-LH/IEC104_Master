#include "iec104/iec104_frame.h"

#include "iec104/iec104_asdu.h"
#include "iec104/iec104_io.h"
#include "logging/logging.h"

#include <string.h>

int iec104_send_u_frame(iec104_session_t *session, uint8_t control)
{
    uint8_t frame[6] = {IEC104_START, 4, control, 0, 0, 0};

    if (iec104_write_all(session->fd, frame, sizeof(frame)) != 0) {
        return -1;
    }

    session->last_tx = time(NULL);
    log_write(LOG_LEVEL_DEBUG, "[%s] send U frame control=0x%02x", session->gateway->name, control);
    return 0;
}

int iec104_send_s_frame(iec104_session_t *session)
{
    uint8_t frame[6];
    uint16_t ack = (uint16_t)(session->recv_seq << 1);

    frame[0] = IEC104_START;
    frame[1] = 4;
    frame[2] = 0x01;
    frame[3] = 0x00;
    frame[4] = (uint8_t)(ack & 0xff);
    frame[5] = (uint8_t)(ack >> 8);

    if (iec104_write_all(session->fd, frame, sizeof(frame)) != 0) {
        return -1;
    }

    session->last_ack_sent = session->recv_seq;
    session->unacked_received = 0;
    session->last_tx = time(NULL);
    log_write(LOG_LEVEL_DEBUG, "[%s] send S frame ack=%u", session->gateway->name, session->recv_seq);
    return 0;
}

/* 发送 IEC104 I 帧，携带主站 ASDU，例如总召命令。 */
static int send_i_frame(iec104_session_t *session, const uint8_t *asdu, size_t asdu_len)
{
    uint8_t frame[IEC104_MAX_APDU];
    uint16_t send = (uint16_t)(session->send_seq << 1);
    uint16_t recv = (uint16_t)(session->recv_seq << 1);

    if (asdu_len > IEC104_ASDU_MAX) {
        return -1;
    }

    frame[0] = IEC104_START;
    frame[1] = (uint8_t)(4 + asdu_len);
    frame[2] = (uint8_t)(send & 0xff);
    frame[3] = (uint8_t)(send >> 8);
    frame[4] = (uint8_t)(recv & 0xff);
    frame[5] = (uint8_t)(recv >> 8);
    memcpy(frame + IEC104_APCI_LEN, asdu, asdu_len);

    if (iec104_write_all(session->fd, frame, 6 + asdu_len) != 0) {
        return -1;
    }

    log_write(LOG_LEVEL_DEBUG,
              "[%s] send I frame send_seq=%u recv_seq=%u asdu_len=%zu",
              session->gateway->name,
              session->send_seq,
              session->recv_seq,
              asdu_len);
    session->send_seq++;
    session->last_tx = time(NULL);
    return 0;
}

int iec104_send_general_interrogation(iec104_session_t *session, uint16_t common_address)
{
    uint8_t asdu[10];

    asdu[0] = TYPE_C_IC_NA_1;
    asdu[1] = 1;
    asdu[2] = COT_ACTIVATION;
    asdu[3] = 0;
    asdu[4] = (uint8_t)(common_address & 0xff);
    asdu[5] = (uint8_t)(common_address >> 8);
    asdu[6] = 0;
    asdu[7] = 0;
    asdu[8] = 0;
    asdu[9] = 20;

    if (send_i_frame(session, asdu, sizeof(asdu)) != 0) {
        return -1;
    }

    session->last_gi = time(NULL);
    log_write(LOG_LEVEL_INFO, "[%s] general interrogation sent ca=%u", session->gateway->name, common_address);
    return 0;
}

int iec104_handle_frame(iec104_session_t *session,
                        const uint8_t *frame,
                        size_t frame_len,
                        const app_config_t *config,
                        point_store_t *store)
{
    const uint8_t *control = frame + 2;
    uint8_t apdu_len = frame[1];

    (void)frame_len;
    session->last_rx = time(NULL);

    if ((control[0] & 0x01) == 0) {
        uint16_t send_seq = (uint16_t)(iec104_read_le16(control) >> 1);
        uint16_t recv_seq = (uint16_t)(iec104_read_le16(control + 2) >> 1);
        const uint8_t *asdu = frame + IEC104_APCI_LEN;
        size_t asdu_len = apdu_len - 4;

        log_write(LOG_LEVEL_DEBUG,
                  "[%s] recv I frame send_seq=%u recv_seq=%u asdu_len=%zu",
                  session->gateway->name,
                  send_seq,
                  recv_seq,
                  asdu_len);

        if (send_seq != session->recv_seq) {
            log_write(LOG_LEVEL_WARN,
                      "[%s] unexpected send sequence from gateway expected=%u got=%u",
                      session->gateway->name,
                      session->recv_seq,
                      send_seq);
            return -1;
        }

        session->recv_seq++;
        session->unacked_received++;
        iec104_parse_asdu(asdu, asdu_len, config, session->gateway, store);

        if (session->unacked_received >= config->ack_window) {
            return iec104_send_s_frame(session);
        }

        return 0;
    }

    if ((control[0] & 0x03) == 0x01) {
        uint16_t ack_seq = (uint16_t)(iec104_read_le16(control + 2) >> 1);
        log_write(LOG_LEVEL_DEBUG, "[%s] recv S frame ack=%u", session->gateway->name, ack_seq);
        return 0;
    }

    if ((control[0] & 0x03) == 0x03) {
        switch (control[0]) {
        case IEC104_U_STARTDT_CON:
            session->startdt_confirmed = 1;
            log_write(LOG_LEVEL_INFO, "[%s] STARTDT confirmed", session->gateway->name);
            return 0;
        case IEC104_U_STOPDT_CON:
            log_write(LOG_LEVEL_WARN, "[%s] STOPDT confirmed by gateway", session->gateway->name);
            return -1;
        case IEC104_U_TESTFR_ACT:
            log_write(LOG_LEVEL_DEBUG, "[%s] recv TESTFR act", session->gateway->name);
            return iec104_send_u_frame(session, IEC104_U_TESTFR_CON);
        case IEC104_U_TESTFR_CON:
            session->testfr_pending = 0;
            log_write(LOG_LEVEL_DEBUG, "[%s] TESTFR confirmed", session->gateway->name);
            return 0;
        default:
            log_write(LOG_LEVEL_WARN, "[%s] unknown U frame control=0x%02x", session->gateway->name, control[0]);
            return 0;
        }
    }

    log_write(LOG_LEVEL_WARN, "[%s] unknown control field 0x%02x", session->gateway->name, control[0]);
    return 0;
}
