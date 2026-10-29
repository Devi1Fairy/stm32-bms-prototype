#include <stddef.h>
#include "bms_controller.h"

/* static表示这个辅助函数只在本文件内部使用。 */
static uint8_t Bms_ControllerConfigIsValid(
    const BmsControllerConfig *config)
{
    const BmsRecoveryConfig *recovery;

    if (config == NULL)
    {
        return 0u;
    }

    recovery = &config->recovery;

    /* 先检查正常保护参数是否成立。 */
    if (config->uv_threshold_mv <= 0 ||
        config->ov_threshold_mv <= config->uv_threshold_mv ||
        config->discharge_limit_ma <= 0 ||
        config->charge_limit_ma <= 0 ||
        config->voltage_max_age_ms == 0u ||
        config->temperature_max_age_ms == 0u ||
        config->current_max_age_ms == 0u)
    {
        return 0u;
    }

    /* 恢复边界必须位于正常区间内，保留明确的回差。 */
    if (recovery->cell_min_mv <= config->uv_threshold_mv ||
        recovery->cell_max_mv >= config->ov_threshold_mv ||
        recovery->cell_min_mv >= recovery->cell_max_mv ||
        recovery->max_temperature_dC >= config->over_temp_dC ||
        recovery->discharge_limit_ma <= 0 ||
        recovery->discharge_limit_ma >= config->discharge_limit_ma ||
        recovery->charge_limit_ma <= 0 ||
        recovery->charge_limit_ma >= config->charge_limit_ma ||
        recovery->voltage_max_age_ms == 0u ||
        recovery->temperature_max_age_ms == 0u ||
        recovery->current_max_age_ms == 0u ||
        recovery->stable_time_ms == 0u)
    {
        return 0u;
    }

    return 1u;
}

void Bms_ControllerInit(BmsController *controller)
{
    if (controller == NULL)
    {
        return;
    }

    Bms_StateMachineInit(&controller->machine);
    Bms_RecoveryInit(&controller->recovery);

    /* 首次检查尚未执行，不把未知数据报告为正常。 */
    controller->active_faults = BMS_FAULT_DATA_INVALID;
    controller->clear_allowed = 0u;
}

void Bms_ControllerStep(
    BmsController *controller,
    const BmsSample *sample,
    const BmsControllerConfig *config,
    BmsCommand command,
    uint8_t self_test_passed,
    uint32_t now_ms)
{
    BmsVoltageReport voltage;
    BmsTemperatureResult temperature;
    BmsCurrentResult current;
    BmsControlInput input = {0};

    if (controller == NULL)
    {
        return;
    }

    input.command = command;
    input.self_test_passed = self_test_passed;

    /* 缺少采样对象或配置错误，立即交给状态机锁定。 */
    if (sample == NULL || Bms_ControllerConfigIsValid(config) == 0u)
    {
        controller->active_faults = BMS_FAULT_INPUT_ERROR;
        controller->clear_allowed = 0u;
        Bms_RecoveryInit(&controller->recovery);

        input.active_faults = controller->active_faults;
        Bms_StateMachineStep(&controller->machine, &input);
        return;
    }

    /* 1：使用正常保护阈值，检查同一轮采样和同一个当前时间。 */
    voltage = Bms_CheckPackVoltages(
        sample, config->uv_threshold_mv, config->ov_threshold_mv,
        now_ms, config->voltage_max_age_ms);

    temperature = Bms_CheckTemperature(
        &sample->temperature_dC, config->over_temp_dC,
        now_ms, config->temperature_max_age_ms);

    current = Bms_CheckCurrent(
        &sample->current_ma,
        config->discharge_limit_ma, config->charge_limit_ma,
        now_ms, config->current_max_age_ms);

    controller->active_faults = Bms_CombineFaults(
        &voltage, temperature, current);

    /* 2：只有已经处于故障锁定时，才累计恢复时间。 */
    controller->clear_allowed = 0u;

    if (controller->machine.state == BMS_STATE_FAULT_LOCK)
    {
        controller->clear_allowed = Bms_RecoveryUpdate(
            &controller->recovery, sample, &config->recovery,
            controller->active_faults, now_ms);
    }
    else
    {
        Bms_RecoveryInit(&controller->recovery);
    }

    /* 3：统一交给状态机决定下一状态和软件输出许可。 */
    input.active_faults = controller->active_faults;
    input.clear_allowed = controller->clear_allowed;
    Bms_StateMachineStep(&controller->machine, &input);

    /* 清除成功后立即丢弃计时，避免下次故障复用旧记录。 */
    if (controller->machine.state != BMS_STATE_FAULT_LOCK)
    {
        Bms_RecoveryInit(&controller->recovery);
        controller->clear_allowed = 0u;
    }
}
