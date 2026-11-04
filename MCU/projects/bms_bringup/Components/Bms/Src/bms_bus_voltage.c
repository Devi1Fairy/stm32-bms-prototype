#include "bms_bus_voltage.h"
#include <stddef.h>

uint8_t Bms_BusConfigIsValid(const BmsBusVoltageConfig *config)
{
    if (config == NULL)
        return 0u;

    if (config->enabled == 0u)
        return 1u;

    return config->enabled == 1u
        && config->under_mv > 0
        && config->over_mv > config->under_mv
        && config->recover_min_mv > config->under_mv
        && config->recover_max_mv < config->over_mv
        && config->recover_min_mv < config->recover_max_mv
        && config->max_age_ms != 0u;
}

uint32_t Bms_CheckBusVoltage(const BmsMeasurement *bus,
                             const BmsBusVoltageConfig *config,
                             uint32_t now_ms)
{
    BmsSampleStatus status;

    if (Bms_BusConfigIsValid(config) == 0u)
        return BMS_FAULT_INPUT_ERROR;

    if (config->enabled == 0u)
        return BMS_FAULT_NONE;

    if (bus == NULL)
        return BMS_FAULT_INPUT_ERROR;

    /* 先看有效性和时间，旧电压不能用于判断当前过压/欠压。 */
    status = Bms_CheckSampleFreshness(bus, now_ms, config->max_age_ms);

    if (status == BMS_SAMPLE_INVALID)
        return BMS_FAULT_DATA_INVALID;

    if (status == BMS_SAMPLE_TIMEOUT)
        return BMS_FAULT_SAMPLE_TIMEOUT;

    if (bus->value < 0)
        return BMS_FAULT_DATA_INVALID;

    if (bus->value <= config->under_mv)
        return BMS_FAULT_BUS_UV;

    if (bus->value >= config->over_mv)
        return BMS_FAULT_BUS_OV;

    return BMS_FAULT_NONE;
}

uint8_t Bms_BusRecoveryReady(const BmsMeasurement *bus,
                             const BmsBusVoltageConfig *config,
                             uint32_t now_ms)
{
    if (Bms_BusConfigIsValid(config) == 0u)
        return 0u;

    if (config->enabled == 0u)
        return 1u;

    /* 恢复区间包含边界，但必须使用有效且未过期的数据。 */
    return bus != NULL
        && Bms_CheckSampleFreshness(bus, now_ms, config->max_age_ms)
            == BMS_SAMPLE_FRESH
        && bus->value >= config->recover_min_mv
        && bus->value <= config->recover_max_mv;
}
