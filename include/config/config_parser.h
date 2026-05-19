#ifndef IEC104_MASTER_CONFIG_PARSER_H
#define IEC104_MASTER_CONFIG_PARSER_H

#include "config/config.h"

#include <stddef.h>

/* 去除字符串首尾空白字符，用于解析 key=value 配置行。 */
char *config_trim(char *value);

/* 解析布尔配置项，支持 true/false、yes/no、on/off 和 1/0。 */
int config_parse_bool(const char *value, int *out);

/* 解析指定范围内的整数配置项，越界或格式错误时返回失败。 */
int config_parse_int_range(const char *value, int min_value, int max_value, int *out);

/* 解析不小于指定最小值的浮点配置项。 */
int config_parse_double_min(const char *value, double min_value, double *out);

/* 解析三级调试打印等级：off、info、detail。 */
int config_parse_debug_level(const char *value, app_debug_level_t *out);

/* 安全设置字符串配置项，避免目标缓冲区溢出。 */
void config_set_string(char *dest, size_t dest_size, const char *value);

#endif
