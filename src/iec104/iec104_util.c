#include "iec104/iec104_private.h"

#include <string.h>

/* 从 IEC104 小端字节序读取 16 位无符号整数。 */
uint16_t iec104_read_le16(const uint8_t *buffer)
{
    return (uint16_t)(buffer[0] | ((uint16_t)buffer[1] << 8));
}

/* 从 IEC104 三字节小端格式读取信息体地址 IOA。 */
uint32_t iec104_read_le24(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2] << 16);
}

/* 从 IEC104 小端字节序读取 16 位有符号整数。 */
int16_t iec104_read_i16(const uint8_t *buffer)
{
    return (int16_t)iec104_read_le16(buffer);
}

/* 从 IEC104 小端字节序读取 32 位有符号整数，用于电能累计量。 */
int32_t iec104_read_i32(const uint8_t *buffer)
{
    uint32_t value = (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2] << 16) |
                     ((uint32_t)buffer[3] << 24);
    return (int32_t)value;
}

/* 从 IEC104 小端字节序读取 IEEE754 单精度浮点数。 */
float iec104_read_float32(const uint8_t *buffer)
{
    uint32_t raw = (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2] << 16) |
                   ((uint32_t)buffer[3] << 24);
    float value;

    memcpy(&value, &raw, sizeof(value));
    return value;
}

/* 计算两个 timeval 之间的毫秒差，用于确认和保活超时判断。 */
long iec104_elapsed_ms(const struct timeval *start, const struct timeval *end)
{
    return (long)((end->tv_sec - start->tv_sec) * 1000L + (end->tv_usec - start->tv_usec) / 1000L);
}
