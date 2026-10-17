#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "bms_controller.h"

static unsigned int scenarios = 0u;

/* 测试假定取得了新采样，因此更新各通道的时间戳。 */
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

static void CheckController(
    const BmsController *controller,
    BmsState state,
    uint32_t active_faults,
    uint32_t latched_faults,
    uint8_t enabled,
    uint8_t clear_allowed)
{
    assert(controller->machine.state == state);
    assert(controller->active_faults == active_faults);
    assert(controller->machine.latched_faults == latched_faults);
    assert(controller->machine.load_enabled == enabled);
    assert(controller->clear_allowed == clear_allowed);
    ++scenarios;
}

int main(void)
{
    BmsController controller;
    BmsSample sample = {0};

    /* 仅为软件测试配置，单位与头文件中的字段说明一致。 */
    const BmsControllerConfig config = {
        3000, 4200, 600, 1000, 500,
        500u, 500u, 500u,
        {3200, 4100, 550, 800, 300, 500u, 500u, 500u, 2000u}
    };
    BmsControllerConfig bad_config;
    const uint32_t multiple_faults =
        BMS_FAULT_CELL_OV | BMS_FAULT_CELL_UV |
        BMS_FAULT_OVER_TEMP | BMS_FAULT_DISCHARGE_OC;

    /* 初始化尚未检查采样，但不会因此自动产生锁定记录。 */
    Bms_ControllerInit(&controller);
    CheckController(&controller, BMS_STATE_SELF_TEST,
        BMS_FAULT_DATA_INVALID, BMS_FAULT_NONE, 0u, 0u);

    /* 正常采样，自检未通过，继续等待。 */
    MakeHealthySample(&sample, 1000u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_NONE, 0u, 1000u);
    CheckController(&controller, BMS_STATE_SELF_TEST, 0u, 0u, 0u, 0u);

    /* 自检通过的同一轮收到 START，只进入待机。 */
    MakeHealthySample(&sample, 1100u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_START, 1u, 1100u);
    CheckController(&controller, BMS_STATE_STANDBY, 0u, 0u, 0u, 0u);

    MakeHealthySample(&sample, 1200u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_START, 1u, 1200u);
    CheckController(&controller, BMS_STATE_RUN, 0u, 0u, 1u, 0u);

    MakeHealthySample(&sample, 1300u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_STOP, 1u, 1300u);
    CheckController(&controller, BMS_STATE_STANDBY, 0u, 0u, 0u, 0u);

    MakeHealthySample(&sample, 1400u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_START, 1u, 1400u);
    CheckController(&controller, BMS_STATE_RUN, 0u, 0u, 1u, 0u);

    /* 实际构造过压测量；同一轮的 STOP 不改变故障优先级。 */
    MakeHealthySample(&sample, 1500u);
    sample.cell_mv[0].value = 4200;
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_STOP, 1u, 1500u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_CELL_OV, BMS_FAULT_CELL_OV, 0u, 0u);

    /* 4150 mV不再触发过压，但尚未进入恢复区间。 */
    MakeHealthySample(&sample, 1600u);
    sample.cell_mv[0].value = 4150;
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_CLEAR, 1u, 1600u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        0u, BMS_FAULT_CELL_OV, 0u, 0u);
    assert(controller.recovery.timing == 0u);

    /* 开始恢复计时；过早的 CLEAR 不会被保存。 */
    MakeHealthySample(&sample, 1700u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_CLEAR, 1u, 1700u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        0u, BMS_FAULT_CELL_OV, 0u, 0u);
    assert(controller.recovery.stable_since_ms == 1700u);

    MakeHealthySample(&sample, 3699u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_NONE, 1u, 3699u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        0u, BMS_FAULT_CELL_OV, 0u, 0u);

    /* 满2 s且没有新指令，仍然保持锁定。 */
    MakeHealthySample(&sample, 3700u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_NONE, 1u, 3700u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        0u, BMS_FAULT_CELL_OV, 0u, 1u);

    MakeHealthySample(&sample, 3800u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_CLEAR, 1u, 3800u);
    CheckController(&controller, BMS_STATE_STANDBY, 0u, 0u, 0u, 0u);
    assert(controller.recovery.timing == 0u);

    MakeHealthySample(&sample, 3900u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_NONE, 1u, 3900u);
    CheckController(&controller, BMS_STATE_STANDBY, 0u, 0u, 0u, 0u);

    MakeHealthySample(&sample, 4000u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_START, 1u, 4000u);
    CheckController(&controller, BMS_STATE_RUN, 0u, 0u, 1u, 0u);

    /* 多个测量故障同时出现，正常保护必须全部汇总。 */
    MakeHealthySample(&sample, 4100u);
    sample.cell_mv[0].value = 3000;
    sample.cell_mv[1].value = 4200;
    sample.temperature_dC.value = 600;
    sample.current_ma.value = 1000;
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_NONE, 1u, 4100u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        multiple_faults, multiple_faults, 0u, 0u);

    MakeHealthySample(&sample, 4200u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_START, 1u, 4200u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        0u, multiple_faults, 0u, 0u);
    assert(controller.recovery.stable_since_ms == 4200u);

    /* 计时中数据无效，撤销计时并累计新故障。 */
    MakeHealthySample(&sample, 5000u);
    sample.current_ma.valid = 0u;
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_NONE, 1u, 5000u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_DATA_INVALID,
        multiple_faults | BMS_FAULT_DATA_INVALID, 0u, 0u);
    assert(controller.recovery.timing == 0u);

    MakeHealthySample(&sample, 5100u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_NONE, 1u, 5100u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        0u, multiple_faults | BMS_FAULT_DATA_INVALID, 0u, 0u);

    /* 时间已满，但旧温度恰好超时，仍不能清除。 */
    MakeHealthySample(&sample, 7100u);
    sample.temperature_dC.timestamp_ms = 6600u;
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_CLEAR, 1u, 7100u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_SAMPLE_TIMEOUT,
        multiple_faults | BMS_FAULT_DATA_INVALID |
        BMS_FAULT_SAMPLE_TIMEOUT, 0u, 0u);

    MakeHealthySample(&sample, 7200u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_NONE, 1u, 7200u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        0u, multiple_faults | BMS_FAULT_DATA_INVALID |
        BMS_FAULT_SAMPLE_TIMEOUT, 0u, 0u);

    MakeHealthySample(&sample, 9200u);
    Bms_ControllerStep(&controller, &sample, &config,
        BMS_CMD_CLEAR, 1u, 9200u);
    CheckController(&controller, BMS_STATE_STANDBY, 0u, 0u, 0u, 0u);

    /* 配置错误时，START 也不能开启负载。 */
    MakeHealthySample(&sample, 9300u);
    bad_config = config;
    bad_config.recovery.cell_max_mv = config.ov_threshold_mv;
    Bms_ControllerStep(&controller, &sample, &bad_config,
        BMS_CMD_START, 1u, 9300u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_INPUT_ERROR, BMS_FAULT_INPUT_ERROR, 0u, 0u);

    bad_config = config;
    bad_config.recovery.cell_min_mv = config.uv_threshold_mv;
    Bms_ControllerStep(&controller, &sample, &bad_config,
        BMS_CMD_CLEAR, 1u, 9300u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_INPUT_ERROR, BMS_FAULT_INPUT_ERROR, 0u, 0u);

    bad_config = config;
    bad_config.recovery.max_temperature_dC = config.over_temp_dC;
    Bms_ControllerStep(&controller, &sample, &bad_config,
        BMS_CMD_NONE, 1u, 9300u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_INPUT_ERROR, BMS_FAULT_INPUT_ERROR, 0u, 0u);

    bad_config = config;
    bad_config.recovery.discharge_limit_ma = config.discharge_limit_ma;
    Bms_ControllerStep(&controller, &sample, &bad_config,
        BMS_CMD_NONE, 1u, 9300u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_INPUT_ERROR, BMS_FAULT_INPUT_ERROR, 0u, 0u);

    bad_config = config;
    bad_config.recovery.charge_limit_ma = config.charge_limit_ma;
    Bms_ControllerStep(&controller, &sample, &bad_config,
        BMS_CMD_NONE, 1u, 9300u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_INPUT_ERROR, BMS_FAULT_INPUT_ERROR, 0u, 0u);

    bad_config = config;
    bad_config.uv_threshold_mv = 0;
    Bms_ControllerStep(&controller, &sample, &bad_config,
        BMS_CMD_NONE, 1u, 9300u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_INPUT_ERROR, BMS_FAULT_INPUT_ERROR, 0u, 0u);

    bad_config = config;
    bad_config.recovery.stable_time_ms = 0u;
    Bms_ControllerStep(&controller, &sample, &bad_config,
        BMS_CMD_NONE, 1u, 9300u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_INPUT_ERROR, BMS_FAULT_INPUT_ERROR, 0u, 0u);

    bad_config = config;
    bad_config.current_max_age_ms = 0u;
    Bms_ControllerStep(&controller, &sample, &bad_config,
        BMS_CMD_NONE, 1u, 9300u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_INPUT_ERROR, BMS_FAULT_INPUT_ERROR, 0u, 0u);

    Bms_ControllerStep(&controller, NULL, &config,
        BMS_CMD_NONE, 1u, 9300u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_INPUT_ERROR, BMS_FAULT_INPUT_ERROR, 0u, 0u);

    Bms_ControllerStep(&controller, &sample, NULL,
        BMS_CMD_NONE, 1u, 9300u);
    CheckController(&controller, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_INPUT_ERROR, BMS_FAULT_INPUT_ERROR, 0u, 0u);

    printf("%u controller scenarios passed.\n", scenarios);
    return 0;
}