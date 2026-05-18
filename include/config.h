#ifndef IEC104_MASTER_CONFIG_H
#define IEC104_MASTER_CONFIG_H

#include <stdint.h>

#ifndef IEC104_PATH_MAX
#define IEC104_PATH_MAX 4096
#endif

typedef enum app_debug_level {
    APP_DEBUG_OFF = 0,
    APP_DEBUG_INFO = 1,
    APP_DEBUG_DETAIL = 2
} app_debug_level_t;

typedef struct app_config {
    char slave_host[256];
    uint16_t slave_port;
    uint16_t common_address;

    int daemonize;
    app_debug_level_t debug_level;
    char pid_file[IEC104_PATH_MAX];
    char log_file[IEC104_PATH_MAX];
    char debug_log_file[IEC104_PATH_MAX];

    int connect_timeout_sec;
    int receive_timeout_sec;
    int reconnect_initial_sec;
    int reconnect_max_sec;
    int general_interrogation_interval_sec;
    int test_frame_interval_sec;
    int ack_timeout_ms;
    int ack_window;

    double energy_spike_abs_threshold;
    double energy_spike_rate_threshold;

    int log_all_yc;
    int log_unchanged_yx;
} app_config_t;

/* 设置默认配置参数。 */
void config_set_defaults(app_config_t *config);

/* 从指定配置文件加载参数。 */
int config_load(app_config_t *config, const char *path);

/* 打印命令行使用说明。 */
void config_print_usage(const char *program);

#endif
