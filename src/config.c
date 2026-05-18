#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* 去除字符串首尾空白字符，用于解析 key=value 配置行。 */
static char *trim(char *value)
{
    char *end;

    while (isspace((unsigned char)*value)) {
        ++value;
    }

    if (*value == '\0') {
        return value;
    }

    end = value + strlen(value) - 1;
    while (end > value && isspace((unsigned char)*end)) {
        *end = '\0';
        --end;
    }

    return value;
}

/* 解析布尔配置项，支持 true/false、yes/no、on/off 和 1/0。 */
static int parse_bool(const char *value, int *out)
{
    if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcmp(value, "1") == 0 ||
        strcasecmp(value, "on") == 0) {
        *out = 1;
        return 0;
    }
    if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 || strcmp(value, "0") == 0 ||
        strcasecmp(value, "off") == 0) {
        *out = 0;
        return 0;
    }

    return -1;
}

/* 解析指定范围内的整数配置项，越界或格式错误时返回失败。 */
static int parse_int_range(const char *value, int min_value, int max_value, int *out)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *trim(end) != '\0' || parsed < min_value || parsed > max_value) {
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

/* 解析不小于指定最小值的浮点配置项。 */
static int parse_double_min(const char *value, double min_value, double *out)
{
    char *end = NULL;
    double parsed;

    errno = 0;
    parsed = strtod(value, &end);
    if (errno != 0 || end == value || *trim(end) != '\0' || parsed < min_value) {
        return -1;
    }

    *out = parsed;
    return 0;
}

/* 解析三级调试打印等级：off、info、detail。 */
static int parse_debug_level(const char *value, app_debug_level_t *out)
{
    if (strcasecmp(value, "off") == 0 || strcasecmp(value, "none") == 0 ||
        strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *out = APP_DEBUG_OFF;
        return 0;
    }

    if (strcasecmp(value, "info") == 0 || strcasecmp(value, "brief") == 0 ||
        strcasecmp(value, "normal") == 0 || strcmp(value, "1") == 0) {
        *out = APP_DEBUG_INFO;
        return 0;
    }

    if (strcasecmp(value, "detail") == 0 || strcasecmp(value, "debug") == 0 ||
        strcasecmp(value, "verbose") == 0 || strcmp(value, "2") == 0) {
        *out = APP_DEBUG_DETAIL;
        return 0;
    }

    return -1;
}

/* 安全设置字符串配置项，避免目标缓冲区溢出。 */
static void set_string(char *dest, size_t dest_size, const char *value)
{
    snprintf(dest, dest_size, "%s", value);
}

/* 设置程序默认配置，配置文件中未出现的字段使用这些默认值。 */
void config_set_defaults(app_config_t *config)
{
    memset(config, 0, sizeof(*config));
    set_string(config->slave_host, sizeof(config->slave_host), "127.0.0.1");
    config->slave_port = 2404;
    config->common_address = 1;
    config->daemonize = 0;
    config->debug_level = APP_DEBUG_INFO;
    set_string(config->pid_file, sizeof(config->pid_file), "/run/iec104-master/iec104-master.pid");
    set_string(config->log_file, sizeof(config->log_file), "/var/log/iec104-master/iec104-master.log");
    set_string(config->debug_log_file,
               sizeof(config->debug_log_file),
               "/var/log/iec104-master/debug/iec104-master-debug.log");
    config->connect_timeout_sec = 5;
    config->receive_timeout_sec = 2;
    config->reconnect_initial_sec = 2;
    config->reconnect_max_sec = 60;
    config->general_interrogation_interval_sec = 300;
    config->test_frame_interval_sec = 20;
    config->ack_timeout_ms = 15000;
    config->ack_window = 8;
    config->energy_spike_abs_threshold = 1000.0;
    config->energy_spike_rate_threshold = 5.0;
    config->log_all_yc = 0;
    config->log_unchanged_yx = 0;
}

/* 加载并解析主站配置文件，将 key=value 配置写入 app_config_t。 */
int config_load(app_config_t *config, const char *path)
{
    FILE *fp;
    char line[1024];
    unsigned int line_no = 0;

    fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "failed to open config %s: %s\n", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *key;
        char *value;
        char *separator;

        ++line_no;
        key = trim(line);
        if (*key == '\0' || *key == '#') {
            continue;
        }

        separator = strchr(key, '=');
        if (separator == NULL) {
            fprintf(stderr, "%s:%u invalid line, expected key=value\n", path, line_no);
            fclose(fp);
            return -1;
        }

        *separator = '\0';
        value = trim(separator + 1);
        key = trim(key);

        if (strcmp(key, "slave_host") == 0) {
            set_string(config->slave_host, sizeof(config->slave_host), value);
        } else if (strcmp(key, "slave_port") == 0) {
            int port;
            if (parse_int_range(value, 1, 65535, &port) != 0) {
                goto invalid_value;
            }
            config->slave_port = (uint16_t)port;
        } else if (strcmp(key, "common_address") == 0) {
            int address;
            if (parse_int_range(value, 0, 65535, &address) != 0) {
                goto invalid_value;
            }
            config->common_address = (uint16_t)address;
        } else if (strcmp(key, "daemonize") == 0) {
            if (parse_bool(value, &config->daemonize) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "debug_level") == 0 || strcmp(key, "debug") == 0) {
            if (parse_debug_level(value, &config->debug_level) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "verbose") == 0) {
            int verbose;
            if (parse_bool(value, &verbose) != 0) {
                goto invalid_value;
            }
            config->debug_level = verbose ? APP_DEBUG_DETAIL : APP_DEBUG_INFO;
        } else if (strcmp(key, "pid_file") == 0) {
            set_string(config->pid_file, sizeof(config->pid_file), value);
        } else if (strcmp(key, "log_file") == 0) {
            set_string(config->log_file, sizeof(config->log_file), value);
        } else if (strcmp(key, "debug_log_file") == 0) {
            set_string(config->debug_log_file, sizeof(config->debug_log_file), value);
        } else if (strcmp(key, "connect_timeout_sec") == 0) {
            if (parse_int_range(value, 1, 300, &config->connect_timeout_sec) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "receive_timeout_sec") == 0) {
            if (parse_int_range(value, 1, 300, &config->receive_timeout_sec) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "reconnect_initial_sec") == 0) {
            if (parse_int_range(value, 1, 3600, &config->reconnect_initial_sec) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "reconnect_max_sec") == 0) {
            if (parse_int_range(value, 1, 86400, &config->reconnect_max_sec) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "general_interrogation_interval_sec") == 0) {
            if (parse_int_range(value, 0, 86400, &config->general_interrogation_interval_sec) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "test_frame_interval_sec") == 0) {
            if (parse_int_range(value, 5, 3600, &config->test_frame_interval_sec) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "ack_timeout_ms") == 0) {
            if (parse_int_range(value, 1000, 60000, &config->ack_timeout_ms) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "ack_window") == 0) {
            if (parse_int_range(value, 1, 32767, &config->ack_window) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "energy_spike_abs_threshold") == 0) {
            if (parse_double_min(value, 0.0, &config->energy_spike_abs_threshold) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "energy_spike_rate_threshold") == 0) {
            if (parse_double_min(value, 1.0, &config->energy_spike_rate_threshold) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "log_all_yc") == 0) {
            if (parse_bool(value, &config->log_all_yc) != 0) {
                goto invalid_value;
            }
        } else if (strcmp(key, "log_unchanged_yx") == 0) {
            if (parse_bool(value, &config->log_unchanged_yx) != 0) {
                goto invalid_value;
            }
        } else {
            fprintf(stderr, "%s:%u unknown key: %s\n", path, line_no, key);
            fclose(fp);
            return -1;
        }

        continue;

    invalid_value:
        fprintf(stderr, "%s:%u invalid value for %s: %s\n", path, line_no, key, value);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    if (config->reconnect_max_sec < config->reconnect_initial_sec) {
        config->reconnect_max_sec = config->reconnect_initial_sec;
    }

    return 0;
}

/* 打印命令行使用说明，供参数错误或 -h 时调用。 */
void config_print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [-c config] [-d] [-f] [-v]\n"
            "  -c config  configuration file path\n"
            "  -d         run as daemon\n"
            "  -f         run in foreground\n"
            "  -v         override debug_level to detail\n",
            program);
}
