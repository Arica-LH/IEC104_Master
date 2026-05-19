#ifndef IEC104_MASTER_IEC104_ASDU_H
#define IEC104_MASTER_IEC104_ASDU_H

#include "iec104/iec104_private.h"

/* 解析网关上送的遥信、遥测、电能累计量，忽略遥控和遥调方向报文。 */
void iec104_parse_asdu(const uint8_t *asdu,
                       size_t len,
                       const app_config_t *config,
                       const gateway_config_t *gateway,
                       point_store_t *store);

#endif
