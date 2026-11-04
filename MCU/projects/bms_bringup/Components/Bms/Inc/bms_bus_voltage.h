#ifndef BMS_BUS_VOLTAGE_H
#define BMS_BUS_VOLTAGE_H

#include "bms_protection.h"

typedef struct
{
    uint8_t enabled;          /* 0关闭本项检查，1开启。 */
    int32_t under_mv;
    int32_t over_mv;
    int32_t recover_min_mv;
    int32_t recover_max_mv;
    uint32_t max_age_ms;
} BmsBusVoltageConfig;

uint8_t Bms_BusConfigIsValid(const BmsBusVoltageConfig *config);

uint32_t Bms_CheckBusVoltage(const BmsMeasurement *bus,
                             const BmsBusVoltageConfig *config,
                             uint32_t now_ms);

uint8_t Bms_BusRecoveryReady(const BmsMeasurement *bus,
                             const BmsBusVoltageConfig *config,
                             uint32_t now_ms);

#endif
							 