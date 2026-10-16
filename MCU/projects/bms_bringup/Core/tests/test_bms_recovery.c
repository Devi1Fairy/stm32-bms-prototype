#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "bms_recovery.h"
#include "bms_state_machine.h"

static unsigned int recovery_checks = 0u;
static unsigned int control_checks = 0u;

/* 模拟本轮取得了一组新的正常采样；仅供测试使用。 */
static void MakeHealthySample(BmsSample *sample, uint32_t now_ms)
{
    unsigned int i;

    for (i = 0u; i < BMS_CELL_COUNT; ++i)
    {
        sample->cell_mv[i].value = 3800;
        sample->cell_mv[i].timestamp_ms = now_ms;
        sample->cell_mv[i].valid = 1u;
        sample->cell_mv[i].source = BMS_SOURCE_SIMULATED;
    }

    sample->temperature_dC.value = 250;
    sample->temperature_dC.timestamp_ms = now_ms;
    sample->temperature_dC.valid = 1u;
    sample->temperature_dC.source = BMS_SOURCE_SIMULATED;

    sample->current_ma.value = 0;
    sample->current_ma.timestamp_ms = now_ms;
    sample->current_ma.valid = 1u;
    sample->current_ma.source = BMS_SOURCE_SIMULATED;
}

/* 为异常测试构造“已经稳定足够久”的计时记录。 */
static void PrepareCase(BmsRecovery *recovery, BmsSample *sample)
{
    MakeHealthySample(sample, 10000u);
    recovery->timing = 1u;
    recovery->stable_since_ms = 8000u;
}

static void CheckRecovery(
    BmsRecovery *recovery,
    const BmsSample *sample,
    const BmsRecoveryConfig *config,
    uint32_t active_faults,
    uint32_t now_ms,
    uint8_t expected_allowed,
    uint8_t expected_timing)
{
    assert(Bms_RecoveryUpdate(
        recovery, sample, config, active_faults, now_ms)
        == expected_allowed);

    if (recovery != NULL)
    {
        assert(recovery->timing == expected_timing);
    }

    ++recovery_checks;
}

static void CheckControl(
    const BmsStateMachine *machine,
    BmsState state,
    uint32_t faults,
    uint8_t enabled)
{
    assert(machine->state == state);
    assert(machine->latched_faults == faults);
    assert(machine->load_enabled == enabled);
    ++control_checks;
}

int main(void)
{
    BmsRecovery recovery;
    BmsSample sample = {0};
    BmsRecoveryConfig config = {
        3200, 4100, 550, 800, 300,
        500u, 500u, 500u, 2000u
    };
    BmsRecoveryConfig bad_config;
    BmsStateMachine machine;
    BmsControlInput input = {0};

    /* 初始化不能把0 ms误认为已经开始计时。 */
    Bms_RecoveryInit(&recovery);
    assert(recovery.timing == 0u);
    assert(recovery.stable_since_ms == 0u);

    /* 从1000 ms开始，2999 ms不足2 s，3000 ms刚好达到。 */
    MakeHealthySample(&sample, 1000u);
    CheckRecovery(&recovery, &sample, &config, 0u, 1000u, 0u, 1u);
    MakeHealthySample(&sample, 2999u);
    CheckRecovery(&recovery, &sample, &config, 0u, 2999u, 0u, 1u);
    MakeHealthySample(&sample, 3000u);
    CheckRecovery(&recovery, &sample, &config, 0u, 3000u, 1u, 1u);
    MakeHealthySample(&sample, 3100u);
    CheckRecovery(&recovery, &sample, &config, 0u, 3100u, 1u, 1u);

    /* 恢复边界以内，仍然允许；电流两个方向分别验证。 */
    PrepareCase(&recovery, &sample);
    sample.cell_mv[0].value = 3201;
    sample.cell_mv[1].value = 4099;
    sample.temperature_dC.value = 549;
    sample.current_ma.value = 799;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 1u, 1u);
    sample.current_ma.value = -299;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 1u, 1u);

    /* 即使测量在恢复区间，有活动故障也必须撤销许可。 */
    PrepareCase(&recovery, &sample);
    CheckRecovery(&recovery, &sample, &config,
        BMS_FAULT_INPUT_ERROR, 10000u, 0u, 0u);

    /* 达到任一恢复边界，都必须清掉计时。 */
    PrepareCase(&recovery, &sample);
    sample.cell_mv[0].value = 4100;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 0u, 0u);

    PrepareCase(&recovery, &sample);
    sample.cell_mv[1].value = 3200;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 0u, 0u);

    PrepareCase(&recovery, &sample);
    sample.temperature_dC.value = 550;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 0u, 0u);

    PrepareCase(&recovery, &sample);
    sample.current_ma.value = 800;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 0u, 0u);

    PrepareCase(&recovery, &sample);
    sample.current_ma.value = -300;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 0u, 0u);

    /* 无效数据与没有来源的数据不能用于恢复。 */
    PrepareCase(&recovery, &sample);
    sample.cell_mv[2].valid = 0u;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 0u, 0u);

    PrepareCase(&recovery, &sample);
    sample.temperature_dC.source = BMS_SOURCE_NONE;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 0u, 0u);

    /* 各类数据年龄达到500 ms，均撤销许可。 */
    PrepareCase(&recovery, &sample);
    sample.cell_mv[0].timestamp_ms = 9500u;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 0u, 0u);

    PrepareCase(&recovery, &sample);
    sample.temperature_dC.timestamp_ms = 9500u;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 0u, 0u);

    PrepareCase(&recovery, &sample);
    sample.current_ma.timestamp_ms = 9500u;
    CheckRecovery(&recovery, &sample, &config, 0u, 10000u, 0u, 0u);

    /* 错误配置不能放行，并清除已有计时。 */
    PrepareCase(&recovery, &sample);
    bad_config = config;
    bad_config.cell_max_mv = bad_config.cell_min_mv;
    CheckRecovery(&recovery, &sample, &bad_config, 0u, 10000u, 0u, 0u);

    PrepareCase(&recovery, &sample);
    bad_config = config;
    bad_config.discharge_limit_ma = 0;
    CheckRecovery(&recovery, &sample, &bad_config, 0u, 10000u, 0u, 0u);

    PrepareCase(&recovery, &sample);
    bad_config = config;
    bad_config.voltage_max_age_ms = 0u;
    CheckRecovery(&recovery, &sample, &bad_config, 0u, 10000u, 0u, 0u);

    PrepareCase(&recovery, &sample);
    bad_config = config;
    bad_config.stable_time_ms = 0u;
    CheckRecovery(&recovery, &sample, &bad_config, 0u, 10000u, 0u, 0u);

    PrepareCase(&recovery, &sample);
    CheckRecovery(&recovery, NULL, &config, 0u, 10000u, 0u, 0u);
    PrepareCase(&recovery, &sample);
    CheckRecovery(&recovery, &sample, NULL, 0u, 10000u, 0u, 0u);
    CheckRecovery(NULL, &sample, &config, 0u, 10000u, 0u, 0u);

    /* 中途异常后，必须重新累计完整的2 s。 */
    Bms_RecoveryInit(&recovery);
    MakeHealthySample(&sample, 1000u);
    CheckRecovery(&recovery, &sample, &config, 0u, 1000u, 0u, 1u);
    MakeHealthySample(&sample, 2500u);
    sample.current_ma.valid = 0u;
    CheckRecovery(&recovery, &sample, &config, 0u, 2500u, 0u, 0u);
    MakeHealthySample(&sample, 2600u);
    CheckRecovery(&recovery, &sample, &config, 0u, 2600u, 0u, 1u);
    MakeHealthySample(&sample, 4599u);
    CheckRecovery(&recovery, &sample, &config, 0u, 4599u, 0u, 1u);
    MakeHealthySample(&sample, 4600u);
    CheckRecovery(&recovery, &sample, &config, 0u, 4600u, 1u, 1u);

    /* 毫秒计数回绕后，仍能判断1999 ms和2000 ms。 */
    Bms_RecoveryInit(&recovery);
    MakeHealthySample(&sample, UINT32_MAX - 999u);
    CheckRecovery(&recovery, &sample, &config,
        0u, UINT32_MAX - 999u, 0u, 1u);
    MakeHealthySample(&sample, 999u);
    CheckRecovery(&recovery, &sample, &config, 0u, 999u, 0u, 1u);
    MakeHealthySample(&sample, 1000u);
    CheckRecovery(&recovery, &sample, &config, 0u, 1000u, 1u, 1u);

    /* 从0 ms开始计时也必须等待完整时间。 */
    Bms_RecoveryInit(&recovery);
    MakeHealthySample(&sample, 0u);
    CheckRecovery(&recovery, &sample, &config, 0u, 0u, 0u, 1u);
    MakeHealthySample(&sample, 1999u);
    CheckRecovery(&recovery, &sample, &config, 0u, 1999u, 0u, 1u);
    MakeHealthySample(&sample, 2000u);
    CheckRecovery(&recovery, &sample, &config, 0u, 2000u, 1u, 1u);

    /* 联合验证：过早的 CLEAR 不会被保存到计时完成后执行。 */
    Bms_StateMachineInit(&machine);
    Bms_RecoveryInit(&recovery);
    input.self_test_passed = 1u;
    input.active_faults = BMS_FAULT_CELL_OV;
    Bms_StateMachineStep(&machine, &input);
    CheckControl(&machine, BMS_STATE_FAULT_LOCK, BMS_FAULT_CELL_OV, 0u);

    input.active_faults = BMS_FAULT_NONE;
    input.command = BMS_CMD_CLEAR;
    MakeHealthySample(&sample, 1000u);
    input.clear_allowed = Bms_RecoveryUpdate(
        &recovery, &sample, &config, input.active_faults, 1000u);
    Bms_StateMachineStep(&machine, &input);
    CheckControl(&machine, BMS_STATE_FAULT_LOCK, BMS_FAULT_CELL_OV, 0u);

    input.command = BMS_CMD_NONE;
    MakeHealthySample(&sample, 3000u);
    input.clear_allowed = Bms_RecoveryUpdate(
        &recovery, &sample, &config, input.active_faults, 3000u);
    assert(input.clear_allowed == 1u);
    Bms_StateMachineStep(&machine, &input);
    CheckControl(&machine, BMS_STATE_FAULT_LOCK, BMS_FAULT_CELL_OV, 0u);

    /* 已有许可，再收到新的 CLEAR 才回到待机。 */
    input.command = BMS_CMD_CLEAR;
    MakeHealthySample(&sample, 3100u);
    input.clear_allowed = Bms_RecoveryUpdate(
        &recovery, &sample, &config, input.active_faults, 3100u);
    Bms_StateMachineStep(&machine, &input);
    CheckControl(&machine, BMS_STATE_STANDBY, BMS_FAULT_NONE, 0u);

    /* 退出锁定后清掉计时，不能复用到下一次故障。 */
    Bms_RecoveryInit(&recovery);
    input.clear_allowed = 0u;
    input.command = BMS_CMD_NONE;
    Bms_StateMachineStep(&machine, &input);
    CheckControl(&machine, BMS_STATE_STANDBY, BMS_FAULT_NONE, 0u);

    input.command = BMS_CMD_START;
    Bms_StateMachineStep(&machine, &input);
    CheckControl(&machine, BMS_STATE_RUN, BMS_FAULT_NONE, 1u);

    printf("%u recovery checks and %u control scenarios passed.\n",
        recovery_checks, control_checks);
    return 0;
}