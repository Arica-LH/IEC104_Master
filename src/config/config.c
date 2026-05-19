#include "config/config.h"

#include "config/config_gateway.h"
#include "config/config_parser.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* 设置程序默认配置，配置文件中未出现的字段使用这些默认值。 */
void config_set_defaults(app_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config_gateway_set_defaults(&config->gateways[0], 0);
    config->gateways[0].configured = 1;
    config->gateway_count = 1;
    config->daemonize = 0;
    config->debug_level = APP_DEBUG_INFO;
    config_set_string(config->pid_file, sizeof(config->pid_file), "/run/iec104-master/iec104-master.pid");
    config_set_string(config->log_file, sizeof(config->log_file), "/var/log/iec104-master/iec104-master.log");
    config_set_string(config->debug_log_file,
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

/* 解析全局配置项，例如日志路径、重连参数、总召周期和调试等级。 */
static int parse_global_key(app_config_t *config, const char *key, const char *value)
{
    if (strcmp(key, "daemonize") == 0) {
        return config_parse_bool(value, &config->daemonize);
    }
    if (strcmp(key, "debug_level") == 0 || strcmp(key, "debug") == 0) {
        return config_parse_debug_level(value, &config->debug_level);
    }
    if (strcmp(key, "verbose") == 0) {
        int verbose;
        if (config_parse_bool(value, &verbose) != 0) {
            return -1;
        }
        config->debug_level = verbose ? APP_DEBUG_DETAIL : APP_DEBUG_INFO;
        return 0;
    }
    if (strcmp(key, "pid_file") == 0) {
        config_set_string(config->pid_file, sizeof(config->pid_file), value);
        return 0;
    }
    if (strcmp(key, "log_file") == 0) {
        config_set_string(config->log_file, sizeof(config->log_file), value);
        return 0;
    }
    if (strcmp(key, "debug_log_file") == 0) {
        config_set_string(config->debug_log_file, sizeof(config->debug_log_file), value);
        return 0;
    }
    if (strcmp(key, "connect_timeout_sec") == 0) {
        return config_parse_int_range(value, 1, 300, &config->connect_timeout_sec);
    }
    if (strcmp(key, "receive_timeout_sec") == 0) {
        return config_parse_int_range(value, 1, 300, &config->receive_timeout_sec);
    }
    if (strcmp(key, "reconnect_initial_sec") == 0) {
        return config_parse_int_range(value, 1, 3600, &config->reconnect_initial_sec);
    }
    if (strcmp(key, "reconnect_max_sec") == 0) {
        return config_parse_int_range(value, 1, 86400, &config->reconnect_max_sec);
    }
    if (strcmp(key, "general_interrogation_interval_sec") == 0) {
        return config_parse_int_range(value, 0, 86400, &config->general_interrogation_interval_sec);
    }
    if (strcmp(key, "test_frame_interval_sec") == 0) {
        return config_parse_int_range(value, 5, 3600, &config->test_frame_interval_sec);
    }
    if (strcmp(key, "ack_timeout_ms") == 0) {
        return config_parse_int_range(value, 1000, 60000, &config->ack_timeout_ms);
    }
    if (strcmp(key, "ack_window") == 0) {
        return config_parse_int_range(value, 1, 32767, &config->ack_window);
    }
    if (strcmp(key, "energy_spike_abs_threshold") == 0) {
        return config_parse_double_min(value, 0.0, &config->energy_spike_abs_threshold);
    }
    if (strcmp(key, "energy_spike_rate_threshold") == 0) {
        return config_parse_double_min(value, 1.0, &config->energy_spike_rate_threshold);
    }
    if (strcmp(key, "log_all_yc") == 0) {
        return config_parse_bool(value, &config->log_all_yc);
    }
    if (strcmp(key, "log_unchanged_yx") == 0) {
        return config_parse_bool(value, &config->log_unchanged_yx);
    }

    return 1;
}

/* 解析旧版 slave_host/slave_port/common_address 单网关配置，保持向后兼容。 */
static int parse_legacy_gateway_key(app_config_t *config, const char *key, const char *value)
{
    if (strcmp(key, "slave_host") == 0) {
        if (value[0] == '\0') {
            return -1;
        }
        config_set_string(config->gateways[0].slave_host, sizeof(config->gateways[0].slave_host), value);
        return 0;
    }
    if (strcmp(key, "slave_port") == 0) {
        int port;
        if (config_parse_int_range(value, 1, 65535, &port) != 0) {
            return -1;
        }
        config->gateways[0].slave_port = (uint16_t)port;
        return 0;
    }
    if (strcmp(key, "common_address") == 0) {
        int address;
        if (config_parse_int_range(value, 0, 65535, &address) != 0) {
            return -1;
        }
        config->gateways[0].common_address = (uint16_t)address;
        return 0;
    }

    return 1;
}

/* 加载并解析主站配置文件，将 key=value 配置写入 app_config_t。 */
int config_load(app_config_t *config, const char *path)
{
    FILE *fp;
    char line[1024];
    unsigned int line_no = 0;
    int saw_gateway_keys = 0;
    int saw_legacy_gateway_keys = 0;

    fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "failed to open config %s: %s\n", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *key;
        char *value;
        char *separator;
        int rc;

        ++line_no;
        key = config_trim(line);
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
        value = config_trim(separator + 1);
        key = config_trim(key);

        {
            size_t gateway_index;
            const char *gateway_field = NULL;

            if (config_gateway_parse_key(key, &gateway_index, &gateway_field) == 0) {
                gateway_config_t *gateway;
                int field_rc;

                if (saw_legacy_gateway_keys) {
                    fprintf(stderr, "%s:%u legacy slave_* keys cannot be mixed with gateway.N.* keys\n", path, line_no);
                    fclose(fp);
                    return -1;
                }
                if (!saw_gateway_keys) {
                    config_gateway_clear_all(config);
                    saw_gateway_keys = 1;
                }

                gateway = config_gateway_ensure(config, gateway_index);
                if (gateway == NULL) {
                    goto invalid_value;
                }

                field_rc = config_gateway_parse_field(gateway, gateway_field, value);
                if (field_rc < 0) {
                    goto invalid_value;
                }
                if (field_rc > 0) {
                    fprintf(stderr, "%s:%u unknown gateway field: %s\n", path, line_no, gateway_field);
                    fclose(fp);
                    return -1;
                }

                continue;
            }
        }

        rc = parse_legacy_gateway_key(config, key, value);
        if (rc == 0) {
            if (saw_gateway_keys) {
                fprintf(stderr, "%s:%u legacy key %s cannot be mixed with gateway.N.* keys\n", path, line_no, key);
                fclose(fp);
                return -1;
            }
            saw_legacy_gateway_keys = 1;
            continue;
        }
        if (rc < 0) {
            goto invalid_value;
        }

        rc = parse_global_key(config, key, value);
        if (rc == 0) {
            continue;
        }
        if (rc < 0) {
            goto invalid_value;
        }

        fprintf(stderr, "%s:%u unknown key: %s\n", path, line_no, key);
        fclose(fp);
        return -1;

    invalid_value:
        fprintf(stderr, "%s:%u invalid value for %s: %s\n", path, line_no, key, value);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    if (config->reconnect_max_sec < config->reconnect_initial_sec) {
        config->reconnect_max_sec = config->reconnect_initial_sec;
    }

    if (config_gateway_finalize(config, path) != 0) {
        return -1;
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
