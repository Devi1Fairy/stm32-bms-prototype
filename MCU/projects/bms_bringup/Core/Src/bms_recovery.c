#include <stddef.h>
#include "bms_recovery.h"

void Bms_RecoveryInit(BmsRecovery *recovery)
{
    if (recovery == NULL)
    {
        return;
    }

    recovery->stable_since_ms = 0u;
    recovery->timing = 0u;
}

uint8_t Bms_RecoveryUpdate(
    BmsRecovery *recovery,
    const BmsSample *sample,
    const BmsRecoveryConfig *config,
    uint32_t active_faults,
    uint32_t now_ms)
{
    BmsVoltageReport voltage;
    BmsTemperatureResult temperature;
    BmsCurrentResult current;

    if (recovery == NULL)
    {
        return 0u;
    }

    if (sample == NULL || config == NULL ||
        active_faults != BMS_FAULT_NONE)
    {
        Bms_RecoveryInit(recovery);
        return 0u;
    }

    if (config->stable_time_ms == 0u)
    {
        Bms_RecoveryInit(recovery);
        return 0u;
    }

    /*
     * 复用已有检查函数，但传入更严格的恢复边界。
     * 它们也会检查参数、有效标志、来源和数据时效。
     */
    voltage = Bms_CheckPackVoltages(
        sample, config->cell_min_mv, config->cell_max_mv,
        now_ms, config->voltage_max_age_ms);

    temperature = Bms_CheckTemperature(
        &sample->temperature_dC, config->max_temperature_dC,
        now_ms, config->temperature_max_age_ms);

    current = Bms_CheckCurrent(
        &sample->current_ma,
        config->discharge_limit_ma, config->charge_limit_ma,
        now_ms, config->current_max_age_ms);

    /*
     * 此处汇总的是“是否满足恢复边界”，不用于替换活动故障。
     * 任一项不通过，就丢弃之前累计的稳定时间。
     */
    if (Bms_CombineFaults(&voltage, temperature, current)
        != BMS_FAULT_NONE)
    {
        Bms_RecoveryInit(recovery);
        return 0u;
    }

    if (recovery->timing != 1u)
    {
        recovery->stable_since_ms = now_ms;
        recovery->timing = 1u;
        return 0u;
    }

    /*
     * 使用无符号时间差处理毫秒计数回绕。
     * 必须使用同一时钟，并在一个完整计数周期内持续调用。
     */
    if ((uint32_t)(now_ms - recovery->stable_since_ms)
        >= config->stable_time_ms)
    {
        return 1u;
    }

    return 0u;
}
