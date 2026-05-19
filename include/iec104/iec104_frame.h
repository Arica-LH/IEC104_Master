#ifndef IEC104_MASTER_IEC104_FRAME_H
#define IEC104_MASTER_IEC104_FRAME_H

#include "iec104/iec104_private.h"

/* 发送 IEC104 U 帧，用于 STARTDT、STOPDT 和 TESTFR 控制。 */
int iec104_send_u_frame(iec104_session_t *session, uint8_t control);

/* 发送 IEC104 S 帧，确认已经接收的网关 I 帧序号。 */
int iec104_send_s_frame(iec104_session_t *session);

/* 发送总召命令，主站只采集数据，不实现遥控和遥调。 */
int iec104_send_general_interrogation(iec104_session_t *session, uint16_t common_address);

/* 处理一帧 IEC104 APDU，根据 I/S/U 帧类型维护序号、确认和链路状态。 */
int iec104_handle_frame(iec104_session_t *session,
                        const uint8_t *frame,
                        size_t frame_len,
                        const app_config_t *config,
                        point_store_t *store);

#endif
