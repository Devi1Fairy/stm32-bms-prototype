#ifndef BMS_APP_TYPES_H
#define BMS_APP_TYPES_H

#include "bms_types.h"
#include "bms_status.h"

/* 消息只包含值，不携带任务句柄、外设句柄或控制器内部指针。 */
typedef struct
{
    BmsSample sample;
    int32_t ina_result;
    uint32_t bus_mv;
    uint32_t sample_count;
} BmsAcquisition;

typedef struct
{
    BmsSample sample;
    BmsStatus status;
    int32_t ina_result;
    uint32_t bus_mv;
    uint32_t control_count;
    uint32_t control_max_interval_ms;
    uint32_t sample_count;
} BmsTelemetry;

#endif
