#include "iec104/iec104.h"

#include "iec104/iec104_frame.h"
#include "iec104/iec104_io.h"
#include "iec104/iec104_private.h"
#include "logging/logging.h"

#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef struct gateway_runner {
    const app_config_t *config;
    const gateway_config_t *gateway;
    point_store_t *store;
    volatile sig_atomic_t *running;
    int result;
} gateway_runner_t;

/* 单次连接会话主循环：连接网关、启动数据传输、总召、保活并接收数据。 */
static int session_loop(iec104_session_t *session,
                        const app_config_t *config,
                        const gateway_config_t *gateway,
                        point_store_t *store,
                        volatile sig_atomic_t *running)
{
    int sent_initial_gi = 0;

    memset(session, 0, sizeof(*session));
    session->fd = -1;
    session->gateway = gateway;

    if ((session->fd = iec104_connect_with_timeout(gateway, config->connect_timeout_sec)) < 0) {
        return -1;
    }

    session->last_rx = time(NULL);
    session->last_tx = time(NULL);
    session->last_testfr = session->last_tx;
    gettimeofday(&session->last_data_ack, NULL);
    log_write(LOG_LEVEL_INFO,
              "[%s] connected to IEC104 gateway %s:%u ca=%u",
              gateway->name,
              gateway->slave_host,
              gateway->slave_port,
              gateway->common_address);

    if (iec104_send_u_frame(session, IEC104_U_STARTDT_ACT) != 0) {
        close(session->fd);
        return -1;
    }

    while (*running) {
        uint8_t frame[IEC104_MAX_APDU];
        size_t frame_len = 0;
        int rc;
        time_t now = time(NULL);
        struct timeval tv_now;

        rc = iec104_wait_and_read_frame(gateway, session->fd, frame, &frame_len, config->receive_timeout_sec, running);
        if (rc < 0) {
            log_write(LOG_LEVEL_WARN, "[%s] connection lost while receiving", gateway->name);
            close(session->fd);
            return -1;
        }
        if (rc == 0 && iec104_handle_frame(session, frame, frame_len, config, store) != 0) {
            close(session->fd);
            return -1;
        }

        now = time(NULL);
        gettimeofday(&tv_now, NULL);

        if (session->startdt_confirmed && !sent_initial_gi) {
            if (iec104_send_general_interrogation(session, gateway->common_address) != 0) {
                close(session->fd);
                return -1;
            }
            sent_initial_gi = 1;
        }

        if (session->startdt_confirmed && config->general_interrogation_interval_sec > 0 &&
            now - session->last_gi >= config->general_interrogation_interval_sec) {
            if (iec104_send_general_interrogation(session, gateway->common_address) != 0) {
                close(session->fd);
                return -1;
            }
        }

        if (session->unacked_received > 0 &&
            iec104_elapsed_ms(&session->last_data_ack, &tv_now) >= config->ack_timeout_ms) {
            if (iec104_send_s_frame(session) != 0) {
                close(session->fd);
                return -1;
            }
            gettimeofday(&session->last_data_ack, NULL);
        } else if (session->unacked_received == 0) {
            gettimeofday(&session->last_data_ack, NULL);
        }

        if (now - session->last_testfr >= config->test_frame_interval_sec) {
            if (session->testfr_pending) {
                log_write(LOG_LEVEL_WARN, "[%s] TESTFR timeout, reconnecting", gateway->name);
                close(session->fd);
                return -1;
            }

            if (iec104_send_u_frame(session, IEC104_U_TESTFR_ACT) != 0) {
                close(session->fd);
                return -1;
            }
            session->testfr_pending = 1;
            session->last_testfr = now;
        }

        if (!session->startdt_confirmed && now - session->last_tx >= config->connect_timeout_sec) {
            log_write(LOG_LEVEL_WARN, "[%s] STARTDT confirm timeout", gateway->name);
            close(session->fd);
            return -1;
        }
    }

    iec104_send_u_frame(session, IEC104_U_STOPDT_ACT);
    close(session->fd);
    return 0;
}

/* 单个网关外层运行循环，负责断线后的指数退避重连。 */
static int iec104_gateway_run(const app_config_t *config,
                              const gateway_config_t *gateway,
                              point_store_t *store,
                              volatile sig_atomic_t *running)
{
    int reconnect_delay = config->reconnect_initial_sec;

    while (*running) {
        iec104_session_t session;
        int rc = session_loop(&session, config, gateway, store, running);

        if (!*running) {
            break;
        }

        if (rc == 0) {
            reconnect_delay = config->reconnect_initial_sec;
            continue;
        }

        log_write(LOG_LEVEL_WARN, "[%s] reconnect after %d seconds", gateway->name, reconnect_delay);
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

/* pthread 入口函数，运行单个 IEC104 网关采集循环。 */
static void *gateway_thread_main(void *arg)
{
    gateway_runner_t *runner = arg;

    runner->result = iec104_gateway_run(runner->config, runner->gateway, runner->store, runner->running);
    return NULL;
}

/* IEC104 主站外层运行循环，为每个启用网关启动一个采集线程。 */
int iec104_master_run(const app_config_t *config, point_store_t *store, volatile sig_atomic_t *running)
{
    pthread_t threads[IEC104_GATEWAY_MAX];
    gateway_runner_t runners[IEC104_GATEWAY_MAX];
    size_t started_count = 0;
    int result = 0;

    if (config->gateway_count == 0) {
        log_write(LOG_LEVEL_ERROR, "no enabled gateway configured");
        return -1;
    }

    for (size_t i = 0; i < config->gateway_count; ++i) {
        memset(&runners[i], 0, sizeof(runners[i]));
        runners[i].config = config;
        runners[i].gateway = &config->gateways[i];
        runners[i].store = store;
        runners[i].running = running;

        if (pthread_create(&threads[i], NULL, gateway_thread_main, &runners[i]) != 0) {
            log_write(LOG_LEVEL_ERROR, "failed to start gateway thread name=%s", config->gateways[i].name);
            *running = 0;
            result = -1;
            break;
        }

        ++started_count;
        log_write(LOG_LEVEL_INFO,
                  "[%s] gateway thread started target=%s:%u ca=%u",
                  config->gateways[i].name,
                  config->gateways[i].slave_host,
                  config->gateways[i].slave_port,
                  config->gateways[i].common_address);
    }

    for (size_t i = 0; i < started_count; ++i) {
        if (pthread_join(threads[i], NULL) != 0) {
            log_write(LOG_LEVEL_ERROR, "failed to join gateway thread name=%s", config->gateways[i].name);
            result = -1;
        } else if (runners[i].result != 0) {
            result = -1;
        }
    }

    return result;
}
