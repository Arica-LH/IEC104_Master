#ifndef IEC104_MASTER_CONFIG_GATEWAY_H
#define IEC104_MASTER_CONFIG_GATEWAY_H

#include "config/config.h"

#include <stddef.h>

/* 设置单个网关的默认参数，未配置字段会继承这些值。 */
void config_gateway_set_defaults(gateway_config_t *gateway, size_t index);

/* 清空所有网关配置，用于切换到 gateway.N.* 新格式。 */
void config_gateway_clear_all(app_config_t *config);

/* 获取并初始化指定序号的网关配置。配置文件中的序号从 1 开始。 */
gateway_config_t *config_gateway_ensure(app_config_t *config, size_t index);

/* 解析 gateway.N.field 形式的网关配置项。 */
int config_gateway_parse_key(const char *key, size_t *index, const char **field);

/* 解析单个网关下的字段。 */
int config_gateway_parse_field(gateway_config_t *gateway, const char *field, const char *value);

/* 压缩、校验启用的网关列表，便于运行层直接遍历。 */
int config_gateway_finalize(app_config_t *config, const char *path);

#endif
