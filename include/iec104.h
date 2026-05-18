#ifndef IEC104_MASTER_IEC104_H
#define IEC104_MASTER_IEC104_H

#include <signal.h>

#include "config.h"
#include "point_store.h"

/* 启动 IEC104 主站采集循环，持续连接从站并接收遥信、遥测和电能数据。 */
int iec104_master_run(const app_config_t *config,
                      point_store_t *store,
                      volatile sig_atomic_t *running);

#endif
