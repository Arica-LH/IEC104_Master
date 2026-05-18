#include "iec104.h"

#include "logging.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

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

static uint16_t read_le16(const uint8_t *buffer)
{
    return (uint16_t)(buffer[0] | ((uint16_t)buffer[1] << 8));
}

static uint32_t read_le24(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2] << 16);
}

static int16_t read_i16(const uint8_t *buffer)
{
    return (int16_t)read_le16(buffer);
}

static int32_t read_i32(const uint8_t *buffer)
{
    uint32_t value = (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2] << 16) |
                     ((uint32_t)buffer[3] << 24);
    return (int32_t)value;
}

static float read_float32(const uint8_t *buffer)
{
    uint32_t raw = (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2] << 16) |
                   ((uint32_t)buffer[3] << 24);
    float value;

    memcpy(&value, &raw, sizeof(value));
    return value;
}

static long elapsed_ms(const struct timeval *start, const struct timeval *end)
{
    return (long)((end->tv_sec - start->tv_sec) * 1000L + (end->tv_usec - start->tv_usec) / 1000L);
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int set_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

static int connect_with_timeout(const char *host, uint16_t port, int timeout_sec)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
    char service[16];
    int fd = -1;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    snprintf(service, sizeof(service), "%u", port);

    rc = getaddrinfo(host, service, &hints, &result);
    if (rc != 0) {
        log_write(LOG_LEVEL_ERROR, "resolve slave %s:%u failed: %s", host, port, gai_strerror(rc));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        int opt = 1;
        struct pollfd pfd;
        int error = 0;
        socklen_t error_len = sizeof(error);

        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }

        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

        if (set_nonblocking(fd) != 0) {
            close(fd);
            fd = -1;
            continue;
        }

        rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0) {
            set_blocking(fd);
            break;
        }

        if (errno != EINPROGRESS) {
            close(fd);
            fd = -1;
            continue;
        }

        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        rc = poll(&pfd, 1, timeout_sec * 1000);
        if (rc > 0 && (pfd.revents & POLLOUT) != 0 &&
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_len) == 0 && error == 0) {
            set_blocking(fd);
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);

    if (fd >= 0) {
        struct timeval timeout;

        timeout.tv_sec = timeout_sec;
        timeout.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    }

    return fd;
}

static int write_all(int fd, const uint8_t *buffer, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        ssize_t rc = send(fd, buffer + sent, length - sent, MSG_NOSIGNAL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            return -1;
        }
        sent += (size_t)rc;
    }

    return 0;
}

static int send_u_frame(iec104_session_t *session, uint8_t control)
{
    uint8_t frame[6] = {IEC104_START, 4, control, 0, 0, 0};

    if (write_all(session->fd, frame, sizeof(frame)) != 0) {
        return -1;
    }

    session->last_tx = time(NULL);
    log_write(LOG_LEVEL_DEBUG, "send U frame control=0x%02x", control);
    return 0;
}

static int send_s_frame(iec104_session_t *session)
{
    uint8_t frame[6];
    uint16_t ack = (uint16_t)(session->recv_seq << 1);

    frame[0] = IEC104_START;
    frame[1] = 4;
    frame[2] = 0x01;
    frame[3] = 0x00;
    frame[4] = (uint8_t)(ack & 0xff);
    frame[5] = (uint8_t)(ack >> 8);

    if (write_all(session->fd, frame, sizeof(frame)) != 0) {
        return -1;
    }

    session->last_ack_sent = session->recv_seq;
    session->unacked_received = 0;
    session->last_tx = time(NULL);
    log_write(LOG_LEVEL_DEBUG, "send S frame ack=%u", session->recv_seq);
    return 0;
}

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

    if (write_all(session->fd, frame, 6 + asdu_len) != 0) {
        return -1;
    }

    log_write(LOG_LEVEL_DEBUG, "send I frame send_seq=%u recv_seq=%u asdu_len=%zu", session->send_seq, session->recv_seq, asdu_len);
    session->send_seq++;
    session->last_tx = time(NULL);
    return 0;
}

/* 发送总召命令，主站只采集数据，不实现遥控和遥调。 */
static int send_general_interrogation(iec104_session_t *session, uint16_t common_address)
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
    log_write(LOG_LEVEL_INFO, "general interrogation sent ca=%u", common_address);
    return 0;
}

static int receive_exact(int fd, uint8_t *buffer, size_t length)
{
    size_t received = 0;

    while (received < length) {
        ssize_t rc = recv(fd, buffer + received, length - received, 0);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 1;
            }
            return -1;
        }
        if (rc == 0) {
            return -1;
        }
        received += (size_t)rc;
    }

    return 0;
}

static int wait_and_read_frame(int fd, uint8_t *frame, size_t *frame_len, int timeout_sec, volatile sig_atomic_t *running)
{
    fd_set readfds;
    struct timeval timeout;
    int rc;
    uint8_t header[2];

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;

    rc = select(fd + 1, &readfds, NULL, NULL, &timeout);
    if (rc < 0) {
        if (errno == EINTR) {
            return *running ? 1 : -1;
        }
        return -1;
    }
    if (rc == 0) {
        return 1;
    }

    rc = receive_exact(fd, header, sizeof(header));
    if (rc != 0) {
        return rc;
    }

    if (header[0] != IEC104_START || header[1] < 4 || header[1] > IEC104_MAX_APDU - 2) {
        log_write(LOG_LEVEL_WARN, "invalid frame header start=0x%02x len=%u", header[0], header[1]);
        return -1;
    }

    frame[0] = header[0];
    frame[1] = header[1];
    rc = receive_exact(fd, frame + 2, header[1]);
    if (rc != 0) {
        log_write(LOG_LEVEL_WARN, "incomplete IEC104 frame body len=%u", header[1]);
        return -1;
    }

    *frame_len = (size_t)header[1] + 2;
    return 0;
}

static int parse_cp56time2a(const uint8_t *data, char *buffer, size_t buffer_size)
{
    unsigned int milliseconds = (unsigned int)data[0] | ((unsigned int)data[1] << 8);
    unsigned int minute = data[2] & 0x3f;
    unsigned int hour = data[3] & 0x1f;
    unsigned int day = data[4] & 0x1f;
    unsigned int month = data[5] & 0x0f;
    unsigned int year = (data[6] & 0x7f) + 2000;

    if (milliseconds > 59999 || minute > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12) {
        return -1;
    }

    snprintf(buffer,
             buffer_size,
             "%04u-%02u-%02u %02u:%02u:%02u.%03u",
             year,
             month,
             day,
             hour,
             minute,
             milliseconds / 1000,
             milliseconds % 1000);
    return 0;
}

static size_t type_time_size(uint8_t type_id)
{
    switch (type_id) {
    case TYPE_M_SP_TB_1:
    case TYPE_M_DP_TB_1:
    case TYPE_M_ME_TD_1:
    case TYPE_M_ME_TE_1:
    case TYPE_M_ME_TF_1:
    case TYPE_M_IT_TB_1:
        return 7;
    default:
        return 0;
    }
}

static size_t information_object_size(uint8_t type_id)
{
    switch (type_id) {
    case TYPE_M_SP_NA_1:
    case TYPE_M_SP_TB_1:
        return 1 + type_time_size(type_id);
    case TYPE_M_DP_NA_1:
    case TYPE_M_DP_TB_1:
        return 1 + type_time_size(type_id);
    case TYPE_M_ME_NA_1:
    case TYPE_M_ME_TD_1:
        return 3 + type_time_size(type_id);
    case TYPE_M_ME_NB_1:
    case TYPE_M_ME_TE_1:
        return 3 + type_time_size(type_id);
    case TYPE_M_ME_NC_1:
    case TYPE_M_ME_TF_1:
        return 5 + type_time_size(type_id);
    case TYPE_M_IT_NA_1:
    case TYPE_M_IT_TB_1:
        return 5 + type_time_size(type_id);
    default:
        return 0;
    }
}

static void log_object_time(uint8_t type_id, const uint8_t *time_data)
{
    char time_buf[64];

    if (type_time_size(type_id) == 0) {
        return;
    }

    if (parse_cp56time2a(time_data, time_buf, sizeof(time_buf)) == 0) {
        log_write(LOG_LEVEL_DEBUG, "object time=%s", time_buf);
    }
}

static void parse_information_object(uint8_t type_id,
                                     uint16_t common_address,
                                     uint32_t ioa,
                                     const uint8_t *data,
                                     const app_config_t *config,
                                     point_store_t *store)
{
    switch (type_id) {
    case TYPE_M_SP_NA_1:
    case TYPE_M_SP_TB_1: {
        uint8_t siq = data[0];
        point_store_update_yx(store,
                              common_address,
                              ioa,
                              siq & 0x01,
                              (uint8_t)(siq & 0xf0),
                              config->log_unchanged_yx);
        log_object_time(type_id, data + 1);
        break;
    }
    case TYPE_M_DP_NA_1:
    case TYPE_M_DP_TB_1: {
        uint8_t diq = data[0];
        point_store_update_yx(store,
                              common_address,
                              ioa,
                              diq & 0x03,
                              (uint8_t)(diq & 0xf0),
                              config->log_unchanged_yx);
        log_object_time(type_id, data + 1);
        break;
    }
    case TYPE_M_ME_NA_1:
    case TYPE_M_ME_TD_1: {
        int16_t normalized = read_i16(data);
        double value = normalized / 32768.0;
        point_store_update_yc(store,
                              common_address,
                              ioa,
                              value,
                              data[2],
                              config->log_all_yc);
        log_object_time(type_id, data + 3);
        break;
    }
    case TYPE_M_ME_NB_1:
    case TYPE_M_ME_TE_1: {
        double value = (double)read_i16(data);
        point_store_update_yc(store,
                              common_address,
                              ioa,
                              value,
                              data[2],
                              config->log_all_yc);
        log_object_time(type_id, data + 3);
        break;
    }
    case TYPE_M_ME_NC_1:
    case TYPE_M_ME_TF_1: {
        double value = (double)read_float32(data);
        point_store_update_yc(store,
                              common_address,
                              ioa,
                              value,
                              data[4],
                              config->log_all_yc);
        log_object_time(type_id, data + 5);
        break;
    }
    case TYPE_M_IT_NA_1:
    case TYPE_M_IT_TB_1: {
        int32_t value = read_i32(data);
        point_store_update_energy(store,
                                  common_address,
                                  ioa,
                                  (double)value,
                                  data[4],
                                  config->energy_spike_abs_threshold,
                                  config->energy_spike_rate_threshold);
        log_object_time(type_id, data + 5);
        break;
    }
    default:
        break;
    }
}

/* 解析从站上送的遥信、遥测、电能累计量，忽略遥控和遥调方向报文。 */
static void parse_asdu(const uint8_t *asdu, size_t len, const app_config_t *config, point_store_t *store)
{
    uint8_t type_id;
    uint8_t vsq;
    uint8_t count;
    int sequence;
    uint16_t cot;
    uint16_t common_address;
    const uint8_t *payload;
    size_t payload_len;
    size_t object_size;

    if (len < 6) {
        log_write(LOG_LEVEL_WARN, "ASDU too short len=%zu", len);
        return;
    }

    type_id = asdu[0];
    vsq = asdu[1];
    count = vsq & 0x7f;
    sequence = (vsq & 0x80) != 0;
    cot = read_le16(asdu + 2) & 0x3fff;
    common_address = read_le16(asdu + 4);
    payload = asdu + 6;
    payload_len = len - 6;
    object_size = information_object_size(type_id);

    if (type_id == TYPE_C_IC_NA_1) {
        log_write(LOG_LEVEL_INFO, "general interrogation response ca=%u cot=%u", common_address, cot);
        return;
    }

    if (object_size == 0) {
        log_write(LOG_LEVEL_DEBUG, "unsupported ASDU type=%u count=%u cot=%u ca=%u len=%zu",
                  type_id,
                  count,
                  cot,
                  common_address,
                  len);
        return;
    }

    if (count == 0) {
        return;
    }

    if (sequence) {
        uint32_t base_ioa;

        if (payload_len < 3 + (size_t)count * object_size) {
            log_write(LOG_LEVEL_WARN,
                      "invalid sequential ASDU type=%u count=%u ca=%u payload_len=%zu",
                      type_id,
                      count,
                      common_address,
                      payload_len);
            return;
        }

        base_ioa = read_le24(payload);
        payload += 3;
        for (uint8_t i = 0; i < count; ++i) {
            parse_information_object(type_id, common_address, base_ioa + i, payload + (size_t)i * object_size, config, store);
        }
    } else {
        size_t required = (size_t)count * (3 + object_size);

        if (payload_len < required) {
            log_write(LOG_LEVEL_WARN,
                      "invalid ASDU type=%u count=%u ca=%u payload_len=%zu required=%zu",
                      type_id,
                      count,
                      common_address,
                      payload_len,
                      required);
            return;
        }

        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t *object = payload + (size_t)i * (3 + object_size);
            uint32_t ioa = read_le24(object);
            parse_information_object(type_id, common_address, ioa, object + 3, config, store);
        }
    }
}

static int handle_frame(iec104_session_t *session,
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
        uint16_t send_seq = (uint16_t)(read_le16(control) >> 1);
        uint16_t recv_seq = (uint16_t)(read_le16(control + 2) >> 1);
        const uint8_t *asdu = frame + IEC104_APCI_LEN;
        size_t asdu_len = apdu_len - 4;

        log_write(LOG_LEVEL_DEBUG,
                  "recv I frame send_seq=%u recv_seq=%u asdu_len=%zu",
                  send_seq,
                  recv_seq,
                  asdu_len);

        if (send_seq != session->recv_seq) {
            log_write(LOG_LEVEL_WARN, "unexpected send sequence from slave expected=%u got=%u", session->recv_seq, send_seq);
            return -1;
        }

        session->recv_seq++;
        session->unacked_received++;
        parse_asdu(asdu, asdu_len, config, store);

        if (session->unacked_received >= config->ack_window) {
            return send_s_frame(session);
        }

        return 0;
    }

    if ((control[0] & 0x03) == 0x01) {
        uint16_t ack_seq = (uint16_t)(read_le16(control + 2) >> 1);
        log_write(LOG_LEVEL_DEBUG, "recv S frame ack=%u", ack_seq);
        return 0;
    }

    if ((control[0] & 0x03) == 0x03) {
        switch (control[0]) {
        case IEC104_U_STARTDT_CON:
            session->startdt_confirmed = 1;
            log_write(LOG_LEVEL_INFO, "STARTDT confirmed");
            return 0;
        case IEC104_U_STOPDT_CON:
            log_write(LOG_LEVEL_WARN, "STOPDT confirmed by slave");
            return -1;
        case IEC104_U_TESTFR_ACT:
            log_write(LOG_LEVEL_DEBUG, "recv TESTFR act");
            return send_u_frame(session, IEC104_U_TESTFR_CON);
        case IEC104_U_TESTFR_CON:
            session->testfr_pending = 0;
            log_write(LOG_LEVEL_DEBUG, "TESTFR confirmed");
            return 0;
        default:
            log_write(LOG_LEVEL_WARN, "unknown U frame control=0x%02x", control[0]);
            return 0;
        }
    }

    log_write(LOG_LEVEL_WARN, "unknown control field 0x%02x", control[0]);
    return 0;
}

static int session_loop(iec104_session_t *session,
                        const app_config_t *config,
                        point_store_t *store,
                        volatile sig_atomic_t *running)
{
    int sent_initial_gi = 0;

    memset(session, 0, sizeof(*session));
    session->fd = -1;

    if ((session->fd = connect_with_timeout(config->slave_host, config->slave_port, config->connect_timeout_sec)) < 0) {
        return -1;
    }

    session->last_rx = time(NULL);
    session->last_tx = time(NULL);
    session->last_testfr = session->last_tx;
    gettimeofday(&session->last_data_ack, NULL);
    log_write(LOG_LEVEL_INFO, "connected to IEC104 slave %s:%u", config->slave_host, config->slave_port);

    if (send_u_frame(session, IEC104_U_STARTDT_ACT) != 0) {
        close(session->fd);
        return -1;
    }

    while (*running) {
        uint8_t frame[IEC104_MAX_APDU];
        size_t frame_len = 0;
        int rc;
        time_t now = time(NULL);
        struct timeval tv_now;

        rc = wait_and_read_frame(session->fd, frame, &frame_len, config->receive_timeout_sec, running);
        if (rc < 0) {
            log_write(LOG_LEVEL_WARN, "connection lost while receiving");
            close(session->fd);
            return -1;
        }
        if (rc == 0 && handle_frame(session, frame, frame_len, config, store) != 0) {
            close(session->fd);
            return -1;
        }

        now = time(NULL);
        gettimeofday(&tv_now, NULL);

        if (session->startdt_confirmed && !sent_initial_gi) {
            if (send_general_interrogation(session, config->common_address) != 0) {
                close(session->fd);
                return -1;
            }
            sent_initial_gi = 1;
        }

        if (session->startdt_confirmed && config->general_interrogation_interval_sec > 0 &&
            now - session->last_gi >= config->general_interrogation_interval_sec) {
            if (send_general_interrogation(session, config->common_address) != 0) {
                close(session->fd);
                return -1;
            }
        }

        if (session->unacked_received > 0 &&
            elapsed_ms(&session->last_data_ack, &tv_now) >= config->ack_timeout_ms) {
            if (send_s_frame(session) != 0) {
                close(session->fd);
                return -1;
            }
            gettimeofday(&session->last_data_ack, NULL);
        } else if (session->unacked_received == 0) {
            gettimeofday(&session->last_data_ack, NULL);
        }

        if (now - session->last_testfr >= config->test_frame_interval_sec) {
            if (session->testfr_pending) {
                log_write(LOG_LEVEL_WARN, "TESTFR timeout, reconnecting");
                close(session->fd);
                return -1;
            }

            if (send_u_frame(session, IEC104_U_TESTFR_ACT) != 0) {
                close(session->fd);
                return -1;
            }
            session->testfr_pending = 1;
            session->last_testfr = now;
        }

        if (!session->startdt_confirmed && now - session->last_tx >= config->connect_timeout_sec) {
            log_write(LOG_LEVEL_WARN, "STARTDT confirm timeout");
            close(session->fd);
            return -1;
        }
    }

    send_u_frame(session, IEC104_U_STOPDT_ACT);
    close(session->fd);
    return 0;
}

int iec104_master_run(const app_config_t *config, point_store_t *store, volatile sig_atomic_t *running)
{
    int reconnect_delay = config->reconnect_initial_sec;

    while (*running) {
        iec104_session_t session;
        int rc = session_loop(&session, config, store, running);

        if (!*running) {
            break;
        }

        if (rc == 0) {
            reconnect_delay = config->reconnect_initial_sec;
            continue;
        }

        log_write(LOG_LEVEL_WARN, "reconnect after %d seconds", reconnect_delay);
        for (int i = 0; i < reconnect_delay && *running; ++i) {
            sleep(1);
        }

        if (reconnect_delay < config->reconnect_max_sec) {
            reconnect_delay *= 2;
            if (reconnect_delay > config->reconnect_max_sec) {
                reconnect_delay = config->reconnect_max_sec;
            }
        }
    }

    return 0;
}
