#include "config/config_gateway.h"

#include "config/config_parser.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_gateway_set_defaults(gateway_config_t *gateway, size_t index)
{
    memset(gateway, 0, sizeof(*gateway));
    snprintf(gateway->name, sizeof(gateway->name), "gateway%zu", index + 1);
    config_set_string(gateway->slave_host, sizeof(gateway->slave_host), "127.0.0.1");
    gateway->slave_port = 2404;
    gateway->common_address = 1;
    gateway->enabled = 1;
}

void config_gateway_clear_all(app_config_t *config)
{
    memset(config->gateways, 0, sizeof(config->gateways));
    config->gateway_count = 0;
}

gateway_config_t *config_gateway_ensure(app_config_t *config, size_t index)
{
    gateway_config_t *gateway;

    if (index >= IEC104_GATEWAY_MAX) {
        return NULL;
    }

    gateway = &config->gateways[index];
    if (!gateway->configured) {
        config_gateway_set_defaults(gateway, index);
        gateway->configured = 1;
    }

    if (config->gateway_count <= index) {
        config->gateway_count = index + 1;
    }

    return gateway;
}

int config_gateway_parse_key(const char *key, size_t *index, const char **field)
{
    const char prefix[] = "gateway.";
    const char *cursor;
    char *end = NULL;
    long parsed;

    if (strncmp(key, prefix, sizeof(prefix) - 1) != 0) {
        return -1;
    }

    cursor = key + sizeof(prefix) - 1;
    errno = 0;
    parsed = strtol(cursor, &end, 10);
    if (errno != 0 || end == cursor || parsed < 1 || parsed > IEC104_GATEWAY_MAX || *end != '.' || end[1] == '\0') {
        return -1;
    }

    *index = (size_t)(parsed - 1);
    *field = end + 1;
    return 0;
}

int config_gateway_parse_field(gateway_config_t *gateway, const char *field, const char *value)
{
    if (strcmp(field, "name") == 0) {
        if (value[0] == '\0') {
            return -1;
        }
        config_set_string(gateway->name, sizeof(gateway->name), value);
    } else if (strcmp(field, "slave_host") == 0 || strcmp(field, "host") == 0) {
        if (value[0] == '\0') {
            return -1;
        }
        config_set_string(gateway->slave_host, sizeof(gateway->slave_host), value);
    } else if (strcmp(field, "slave_port") == 0 || strcmp(field, "port") == 0) {
        int port;
        if (config_parse_int_range(value, 1, 65535, &port) != 0) {
            return -1;
        }
        gateway->slave_port = (uint16_t)port;
    } else if (strcmp(field, "common_address") == 0 || strcmp(field, "ca") == 0) {
        int address;
        if (config_parse_int_range(value, 0, 65535, &address) != 0) {
            return -1;
        }
        gateway->common_address = (uint16_t)address;
    } else if (strcmp(field, "enabled") == 0) {
        if (config_parse_bool(value, &gateway->enabled) != 0) {
            return -1;
        }
    } else {
        return 1;
    }

    return 0;
}

int config_gateway_finalize(app_config_t *config, const char *path)
{
    size_t write_index = 0;

    for (size_t read_index = 0; read_index < IEC104_GATEWAY_MAX; ++read_index) {
        gateway_config_t *gateway = &config->gateways[read_index];

        if (!gateway->configured || !gateway->enabled) {
            continue;
        }

        if (gateway->name[0] == '\0' || gateway->slave_host[0] == '\0') {
            fprintf(stderr, "%s invalid gateway.%zu configuration\n", path, read_index + 1);
            return -1;
        }

        if (write_index != read_index) {
            config->gateways[write_index] = *gateway;
        }
        ++write_index;
    }

    if (write_index == 0) {
        fprintf(stderr, "%s no enabled gateway configured\n", path);
        return -1;
    }

    config->gateway_count = write_index;
    for (size_t i = write_index; i < IEC104_GATEWAY_MAX; ++i) {
        memset(&config->gateways[i], 0, sizeof(config->gateways[i]));
    }

    for (size_t i = 0; i < config->gateway_count; ++i) {
        for (size_t j = i + 1; j < config->gateway_count; ++j) {
            if (strcmp(config->gateways[i].name, config->gateways[j].name) == 0) {
                fprintf(stderr, "%s duplicate gateway name: %s\n", path, config->gateways[i].name);
                return -1;
            }
        }
    }

    return 0;
}
