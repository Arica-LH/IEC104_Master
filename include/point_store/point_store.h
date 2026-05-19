#ifndef IEC104_MASTER_POINT_STORE_H
#define IEC104_MASTER_POINT_STORE_H

#include <stdint.h>

typedef struct point_store point_store_t;

/* 创建点表状态缓存。 */
point_store_t *point_store_create(void);

/* 销毁点表状态缓存。 */
void point_store_destroy(point_store_t *store);

/* 更新遥信点值。 */
void point_store_update_yx(point_store_t *store,
                           const char *gateway_name,
                           uint16_t common_address,
                           uint32_t ioa,
                           int value,
                           uint8_t quality,
                           int log_unchanged);

/* 更新遥测点值。 */
void point_store_update_yc(point_store_t *store,
                           const char *gateway_name,
                           uint16_t common_address,
                           uint32_t ioa,
                           double value,
                           uint8_t quality,
                           int log_all);

/* 更新电能累计量并识别突发值。 */
void point_store_update_energy(point_store_t *store,
                               const char *gateway_name,
                               uint16_t common_address,
                               uint32_t ioa,
                               double value,
                               uint8_t flags,
                               double abs_threshold,
                               double rate_threshold);

#endif
