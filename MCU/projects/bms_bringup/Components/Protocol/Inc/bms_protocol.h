#ifndef BMS_PROTOCOL_H
#define BMS_PROTOCOL_H

#include "bms_types.h"
#include "bms_status.h"

#define BMS_CAN_COMMAND_ID     0x201u
#define BMS_CAN_STATUS_ID      0x301u
#define BMS_CAN_MEASUREMENT_ID 0x302u

typedef enum
{
    BMS_ACTION_IGNORED = 0,
    BMS_ACTION_INVALID,
    BMS_ACTION_START,
    BMS_ACTION_STOP,
    BMS_ACTION_CLEAR,
    BMS_ACTION_QUERY,
    BMS_ACTION_PAUSE,
    BMS_ACTION_RESUME,
    BMS_ACTION_DELAY_SAMPLE,
    BMS_ACTION_DELAY_COMM
} BmsProtocolAction;

typedef struct
{
    uint32_t voltage_max_age_ms;
    uint32_t temperature_max_age_ms;
    uint32_t current_max_age_ms;
} BmsProtocolLimits;

/* 编码/解码只操作值，不访问HAL、队列、状态机或GPIO。 */
BmsProtocolAction Bms_ProtocolDecodeCan(
    uint32_t id, uint8_t extended, uint8_t remote,
    uint8_t length, const uint8_t *data);
BmsProtocolAction Bms_ProtocolDecodeUart(uint8_t byte);

uint8_t Bms_ProtocolEncodeStatus(
    const BmsStatus *status, uint8_t sequence, uint8_t data[8]);
uint8_t Bms_ProtocolEncodeMeasurements(
    const BmsSample *sample, const BmsProtocolLimits *limits,
    uint32_t now_ms, uint8_t data[8]);

#endif
