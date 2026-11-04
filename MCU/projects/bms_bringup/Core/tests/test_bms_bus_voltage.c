#include "bms_controller.h"
#include "bms_bus_voltage.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static void MakeSample(BmsSample *sample, uint32_t now_ms, int32_t bus_mv)
{
    uint32_t i;
    BmsSample empty = {0};
    *sample = empty;

    for (i = 0u; i < BMS_CELL_COUNT; ++i)
        sample->cell_mv[i] =
            (BmsMeasurement){3800, now_ms, 1u, BMS_SOURCE_SIMULATED};

    sample->temperature_dC =
        (BmsMeasurement){250, now_ms, 1u, BMS_SOURCE_ADC};

    sample->current_ma =
        (BmsMeasurement){0, now_ms, 1u, BMS_SOURCE_EXTERNAL};

    sample->bus_mv =
        (BmsMeasurement){bus_mv, now_ms, 1u, BMS_SOURCE_EXTERNAL};
}

static void Step(BmsController *controller, const BmsControllerConfig *config,
                 uint32_t now_ms, int32_t bus_mv, BmsCommand command)
{
    BmsSample sample;

    MakeSample(&sample, now_ms, bus_mv);
    Bms_ControllerStep(controller, &sample, config, command, 1u, now_ms);
}

int main(void)
{
    BmsBusVoltageConfig bus = {1u, 4500, 5500, 4700, 5300, 500u};
    BmsBusVoltageConfig disabled = {0};
    BmsBusVoltageConfig bad;
    BmsMeasurement value = {5000, 100u, 1u, BMS_SOURCE_EXTERNAL};
    BmsController controller;
    BmsSample sample;

    BmsControllerConfig config = {
        3000, 4200, 600, 80, 80, 500u, 500u, 500u,
        {3200, 4100, 550, 60, 60, 500u, 500u, 500u, 2000u},
        {1u, 4500, 5500, 4700, 5300, 500u}
    };

    /* 时效、触发边界和有效性。 */
    assert(Bms_BusConfigIsValid(&bus));
    assert(Bms_CheckBusVoltage(&value, &bus, 599u) == 0u);
    assert(Bms_CheckBusVoltage(&value, &bus, 600u)
           == BMS_FAULT_SAMPLE_TIMEOUT);

    value.value = 5500;
    assert(Bms_CheckBusVoltage(&value, &bus, 100u) == BMS_FAULT_BUS_OV);
    value.value = 5499;
    assert(Bms_CheckBusVoltage(&value, &bus, 100u) == 0u);
    value.value = 4500;
    assert(Bms_CheckBusVoltage(&value, &bus, 100u) == BMS_FAULT_BUS_UV);
    value.value = 4501;
    assert(Bms_CheckBusVoltage(&value, &bus, 100u) == 0u);

    value.value = 0;
    assert(Bms_CheckBusVoltage(&value, &bus, 100u) == BMS_FAULT_BUS_UV);
    value.value = -1;
    assert(Bms_CheckBusVoltage(&value, &bus, 100u) == BMS_FAULT_DATA_INVALID);

    value.value = 5000;
    value.valid = 0u;
    assert(Bms_CheckBusVoltage(&value, &bus, 100u) == BMS_FAULT_DATA_INVALID);
    assert(!Bms_BusRecoveryReady(&value, &bus, 100u));

    value.valid = 1u;
    value.source = BMS_SOURCE_NONE;
    assert(Bms_CheckBusVoltage(&value, &bus, 100u) == BMS_FAULT_DATA_INVALID);
    value.source = BMS_SOURCE_EXTERNAL;

    /* 恢复带包含边界。 */
    value.value = 4700;
    assert(Bms_BusRecoveryReady(&value, &bus, 100u));
    value.value = 5300;
    assert(Bms_BusRecoveryReady(&value, &bus, 100u));
    value.value = 4699;
    assert(!Bms_BusRecoveryReady(&value, &bus, 100u));
    value.value = 5301;
    assert(!Bms_BusRecoveryReady(&value, &bus, 100u));

    /* 时间回绕和错误参数。 */
    value.value = 5000;
    value.timestamp_ms = UINT32_MAX - 99u;
    assert(Bms_CheckBusVoltage(&value, &bus, 100u) == 0u);
    assert(Bms_CheckBusVoltage(&value, &bus, 400u)
           == BMS_FAULT_SAMPLE_TIMEOUT);

    assert(Bms_CheckBusVoltage(NULL, &bus, 100u) == BMS_FAULT_INPUT_ERROR);
    assert(Bms_CheckBusVoltage(&value, NULL, 100u) == BMS_FAULT_INPUT_ERROR);
    assert(!Bms_BusRecoveryReady(NULL, &bus, 100u));
    assert(Bms_CheckBusVoltage(NULL, &disabled, 100u) == 0u);
    assert(Bms_BusRecoveryReady(NULL, &disabled, 100u));

    bad = bus; bad.enabled = 2u;
    assert(!Bms_BusConfigIsValid(&bad));
    bad = bus; bad.recover_min_mv = bad.under_mv;
    assert(!Bms_BusConfigIsValid(&bad));
    bad = bus; bad.recover_max_mv = bad.over_mv;
    assert(!Bms_BusConfigIsValid(&bad));
    bad = bus; bad.recover_min_mv = bad.recover_max_mv;
    assert(!Bms_BusConfigIsValid(&bad));
    bad = bus; bad.max_age_ms = 0u;
    assert(Bms_CheckBusVoltage(&value, &bad, 100u) == BMS_FAULT_INPUT_ERROR);

    /* 控制器集成：故障锁定、恢复回差、稳定计时和人工清除。 */
    Bms_ControllerInit(&controller);
    Step(&controller, &config, 0u, 5000, BMS_CMD_NONE);
    Step(&controller, &config, 100u, 5000, BMS_CMD_START);
    assert(controller.machine.state == BMS_STATE_RUN
           && controller.machine.load_enabled);

    Step(&controller, &config, 200u, 5500, BMS_CMD_START);
    assert(controller.active_faults == BMS_FAULT_BUS_OV);
    assert(controller.machine.state == BMS_STATE_FAULT_LOCK
           && !controller.machine.load_enabled);

    Step(&controller, &config, 300u, 5400, BMS_CMD_CLEAR);
    assert(!controller.active_faults
           && !controller.clear_allowed && !controller.recovery.timing);

    Step(&controller, &config, 400u, 5000, BMS_CMD_NONE);
    Step(&controller, &config, 2399u, 5000, BMS_CMD_CLEAR);
    assert(!controller.clear_allowed
           && controller.machine.state == BMS_STATE_FAULT_LOCK);

    Step(&controller, &config, 2400u, 5000, BMS_CMD_NONE);
    assert(controller.clear_allowed);

    Step(&controller, &config, 2500u, 5400, BMS_CMD_CLEAR);
    assert(!controller.clear_allowed && !controller.recovery.timing);

    Step(&controller, &config, 2600u, 5000, BMS_CMD_NONE);
    Step(&controller, &config, 4600u, 5000, BMS_CMD_CLEAR);
    assert(controller.machine.state == BMS_STATE_STANDBY
           && !controller.machine.load_enabled);

    Step(&controller, &config, 4700u, 5000, BMS_CMD_START);
    Step(&controller, &config, 4800u, 4500, BMS_CMD_NONE);
    assert(controller.active_faults == BMS_FAULT_BUS_UV
           && !controller.machine.load_enabled);

    Step(&controller, &config, 4900u, 5000, BMS_CMD_NONE);
    Step(&controller, &config, 6900u, 5000, BMS_CMD_CLEAR);
    assert(controller.machine.state == BMS_STATE_STANDBY);

    /* 旧高压数据只能报超时，不能作为当前过压。 */
    MakeSample(&sample, 7000u, 6000);
    sample.bus_mv.timestamp_ms = 6500u;
    Bms_ControllerStep(&controller, &sample, &config,
                       BMS_CMD_START, 1u, 7000u);
    assert(controller.active_faults == BMS_FAULT_SAMPLE_TIMEOUT
           && !controller.machine.load_enabled);

    config.bus.enabled = 2u;
    Bms_ControllerStep(&controller, &sample, &config,
                       BMS_CMD_CLEAR, 1u, 7000u);
    assert(controller.active_faults == BMS_FAULT_INPUT_ERROR);

    puts("Bus voltage protection checks passed.");
    return 0;
}