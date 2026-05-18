#ifndef IEC104_MASTER_POINT_STORE_H
#define IEC104_MASTER_POINT_STORE_H

#include <stdint.h>

typedef struct point_store point_store_t;

point_store_t *point_store_create(void);
void point_store_destroy(point_store_t *store);

void point_store_update_yx(point_store_t *store,
                           uint16_t common_address,
                           uint32_t ioa,
                           int value,
                           uint8_t quality,
                           int log_unchanged);

void point_store_update_yc(point_store_t *store,
                           uint16_t common_address,
                           uint32_t ioa,
                           double value,
                           uint8_t quality,
                           int log_all);

void point_store_update_energy(point_store_t *store,
                               uint16_t common_address,
                               uint32_t ioa,
                               double value,
                               uint8_t flags,
                               double abs_threshold,
                               double rate_threshold);

#endif
