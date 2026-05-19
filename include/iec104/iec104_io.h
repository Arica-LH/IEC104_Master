#ifndef IEC104_MASTER_IEC104_IO_H
#define IEC104_MASTER_IEC104_IO_H

#include "iec104/iec104_private.h"

/* 在指定超时时间内连接 IEC104 网关，并设置 TCP keepalive 和收发超时。 */
int iec104_connect_with_timeout(const gateway_config_t *gateway, int timeout_sec);

/* 循环发送缓冲区全部内容，避免短写导致 IEC104 帧不完整。 */
int iec104_write_all(int fd, const uint8_t *buffer, size_t length);

/* 等待并读取一帧完整 IEC104 APDU，校验启动字符和长度字段。 */
int iec104_wait_and_read_frame(const gateway_config_t *gateway,
                               int fd,
                               uint8_t *frame,
                               size_t *frame_len,
                               int timeout_sec,
                               volatile sig_atomic_t *running);

#endif
