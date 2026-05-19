#include "iec104/iec104_asdu.h"

#include "logging/logging.h"

#include <stdio.h>

/* 解析 CP56Time2a 七字节时标，转换为可读时间字符串。 */
static int parse_cp56time2a(const uint8_t *data, char *buffer, size_t buffer_size)
{
    unsigned int milliseconds = (unsigned int)data[0] | ((unsigned int)data[1] << 8);
    unsigned int minute = data[2] & 0x3f;
    unsigned int hour = data[3] & 0x1f;
    unsigned int day = data[4] & 0x1f;
    unsigned int month = data[5] & 0x0f;
    unsigned int year = (data[6] & 0x7f) + 2000;

    if (milliseconds > 59999 || minute > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12) {
        return -1;
    }

    snprintf(buffer,
             buffer_size,
             "%04u-%02u-%02u %02u:%02u:%02u.%03u",
             year,
             month,
             day,
             hour,
             minute,
             milliseconds / 1000,
             milliseconds % 1000);
    return 0;
}

/* 判断类型标识是否带 CP56Time2a 时标，并返回时标长度。 */
static size_t type_time_size(uint8_t type_id)
{
    switch (type_id) {
    case TYPE_M_SP_TB_1:
    case TYPE_M_DP_TB_1:
    case TYPE_M_ME_TD_1:
    case TYPE_M_ME_TE_1:
    case TYPE_M_ME_TF_1:
    case TYPE_M_IT_TB_1:
        return 7;
    default:
        return 0;
    }
}

/* 根据 ASDU 类型标识返回单个信息体数据区长度，不包含 3 字节 IOA。 */
static size_t information_object_size(uint8_t type_id)
{
    switch (type_id) {
    case TYPE_M_SP_NA_1:
    case TYPE_M_SP_TB_1:
        return 1 + type_time_size(type_id);
    case TYPE_M_DP_NA_1:
    case TYPE_M_DP_TB_1:
        return 1 + type_time_size(type_id);
    case TYPE_M_ME_NA_1:
    case TYPE_M_ME_TD_1:
        return 3 + type_time_size(type_id);
    case TYPE_M_ME_NB_1:
    case TYPE_M_ME_TE_1:
        return 3 + type_time_size(type_id);
    case TYPE_M_ME_NC_1:
    case TYPE_M_ME_TF_1:
        return 5 + type_time_size(type_id);
    case TYPE_M_IT_NA_1:
    case TYPE_M_IT_TB_1:
        return 5 + type_time_size(type_id);
    default:
        return 0;
    }
}

/* 如果信息体包含时标，则解析并输出详细调试日志。 */
static void log_object_time(const char *gateway_name, uint8_t type_id, const uint8_t *time_data)
{
    char time_buf[64];

    if (type_time_size(type_id) == 0) {
        return;
    }

    if (parse_cp56time2a(time_data, time_buf, sizeof(time_buf)) == 0) {
        log_write(LOG_LEVEL_DEBUG, "[%s] object time=%s", gateway_name, time_buf);
    }
}

/* 解析单个信息体对象，并按类型更新遥信、遥测或电能累计量点表。 */
static void parse_information_object(uint8_t type_id,
                                     const gateway_config_t *gateway,
                                     uint16_t common_address,
                                     uint32_t ioa,
                                     const uint8_t *data,
                                     const app_config_t *config,
                                     point_store_t *store)
{
    switch (type_id) {
    case TYPE_M_SP_NA_1:
    case TYPE_M_SP_TB_1: {
        uint8_t siq = data[0];
        point_store_update_yx(store,
                              gateway->name,
                              common_address,
                              ioa,
                              siq & 0x01,
                              (uint8_t)(siq & 0xf0),
                              config->log_unchanged_yx);
        log_object_time(gateway->name, type_id, data + 1);
        break;
    }
    case TYPE_M_DP_NA_1:
    case TYPE_M_DP_TB_1: {
        uint8_t diq = data[0];
        point_store_update_yx(store,
                              gateway->name,
                              common_address,
                              ioa,
                              diq & 0x03,
                              (uint8_t)(diq & 0xf0),
                              config->log_unchanged_yx);
        log_object_time(gateway->name, type_id, data + 1);
        break;
    }
    case TYPE_M_ME_NA_1:
    case TYPE_M_ME_TD_1: {
        int16_t normalized = iec104_read_i16(data);
        double value = normalized / 32768.0;
        point_store_update_yc(store,
                              gateway->name,
                              common_address,
                              ioa,
                              value,
                              data[2],
                              config->log_all_yc);
        log_object_time(gateway->name, type_id, data + 3);
        break;
    }
    case TYPE_M_ME_NB_1:
    case TYPE_M_ME_TE_1: {
        double value = (double)iec104_read_i16(data);
        point_store_update_yc(store,
                              gateway->name,
                              common_address,
                              ioa,
                              value,
                              data[2],
                              config->log_all_yc);
        log_object_time(gateway->name, type_id, data + 3);
        break;
    }
    case TYPE_M_ME_NC_1:
    case TYPE_M_ME_TF_1: {
        double value = (double)iec104_read_float32(data);
        point_store_update_yc(store,
                              gateway->name,
                              common_address,
                              ioa,
                              value,
                              data[4],
                              config->log_all_yc);
        log_object_time(gateway->name, type_id, data + 5);
        break;
    }
    case TYPE_M_IT_NA_1:
    case TYPE_M_IT_TB_1: {
        int32_t value = iec104_read_i32(data);
        point_store_update_energy(store,
                                  gateway->name,
                                  common_address,
                                  ioa,
                                  (double)value,
                                  data[4],
                                  config->energy_spike_abs_threshold,
                                  config->energy_spike_rate_threshold);
        log_object_time(gateway->name, type_id, data + 5);
        break;
    }
    default:
        break;
    }
}

/* 解析 ASDU 头、VSQ 和信息体列表，并按类型分发到点表缓存。 */
void iec104_parse_asdu(const uint8_t *asdu,
                       size_t len,
                       const app_config_t *config,
                       const gateway_config_t *gateway,
                       point_store_t *store)
{
    uint8_t type_id;
    uint8_t vsq;
    uint8_t count;
    int sequence;
    uint16_t cot;
    uint16_t common_address;
    const uint8_t *payload;
    size_t payload_len;
    size_t object_size;

    if (len < 6) {
        log_write(LOG_LEVEL_WARN, "[%s] ASDU too short len=%zu", gateway->name, len);
        return;
    }

    type_id = asdu[0];
    vsq = asdu[1];
    count = vsq & 0x7f;
    sequence = (vsq & 0x80) != 0;
    cot = iec104_read_le16(asdu + 2) & 0x3fff;
    common_address = iec104_read_le16(asdu + 4);
    payload = asdu + 6;
    payload_len = len - 6;
    object_size = information_object_size(type_id);

    if (type_id == TYPE_C_IC_NA_1) {
        log_write(LOG_LEVEL_INFO, "[%s] general interrogation response ca=%u cot=%u", gateway->name, common_address, cot);
        return;
    }

    if (object_size == 0) {
        log_write(LOG_LEVEL_DEBUG, "[%s] unsupported ASDU type=%u count=%u cot=%u ca=%u len=%zu",
                  gateway->name,
                  type_id,
                  count,
                  cot,
                  common_address,
                  len);
        return;
    }

    if (count == 0) {
        return;
    }

    if (sequence) {
        uint32_t base_ioa;

        if (payload_len < 3 + (size_t)count * object_size) {
            log_write(LOG_LEVEL_WARN,
                      "[%s] invalid sequential ASDU type=%u count=%u ca=%u payload_len=%zu",
                      gateway->name,
                      type_id,
                      count,
                      common_address,
                      payload_len);
            return;
        }

        base_ioa = iec104_read_le24(payload);
        payload += 3;
        for (uint8_t i = 0; i < count; ++i) {
            parse_information_object(type_id,
                                     gateway,
                                     common_address,
                                     base_ioa + i,
                                     payload + (size_t)i * object_size,
                                     config,
                                     store);
        }
    } else {
        size_t required = (size_t)count * (3 + object_size);

        if (payload_len < required) {
            log_write(LOG_LEVEL_WARN,
                      "[%s] invalid ASDU type=%u count=%u ca=%u payload_len=%zu required=%zu",
                      gateway->name,
                      type_id,
                      count,
                      common_address,
                      payload_len,
                      required);
            return;
        }

        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t *object = payload + (size_t)i * (3 + object_size);
            uint32_t ioa = iec104_read_le24(object);
            parse_information_object(type_id, gateway, common_address, ioa, object + 3, config, store);
        }
    }
}
