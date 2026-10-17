#ifndef BMS_CONTROLLER_H
#define BMS_CONTROLLER_H

#include "bms_state_machine.h"
#include "bms_recovery.h"

/* 正常保护阈值与恢复配置放在一起，便于统一校验。 */
typedef struct
{
    int32_t uv_threshold_mv;       /* 单体欠压触发值，单位mV */
    int32_t ov_threshold_mv;       /* 单体过压触发值，单位mV */
    int32_t over_temp_dC;          /* 过温触发值，单位0.1℃ */
    int32_t discharge_limit_ma;    /* 放电限值，填写正数，单位mA */
    int32_t charge_limit_ma;       /* 充电限值大小，填写正数，单位mA */
    uint32_t voltage_max_age_ms;
    uint32_t temperature_max_age_ms;
    uint32_t current_max_age_ms;
    BmsRecoveryConfig recovery;
} BmsControllerConfig;

/* 需要跨控制周期保留的记录。 */
typedef struct
{
    BmsStateMachine machine;
    BmsRecovery recovery;
    uint32_t active_faults;  /* 本轮保护检查结果 */
    uint8_t clear_allowed;  /* 仅在故障锁定期间可能为1 */
} BmsController;

/* 上电调用一次，不要每轮重新初始化。 */
void Bms_ControllerInit(BmsController *controller);

/*
 * 处理一轮完整采样和一条新指令。
 * 无新指令时传入 NONE；本函数不保存指令，也不刷新采样时间戳。
 * 配置在运行期间保持不变；修改前须重新开始恢复计时。
 */
void Bms_ControllerStep(
    BmsController *controller,
    const BmsSample *sample,
    const BmsControllerConfig *config,
    BmsCommand command,
    uint8_t self_test_passed,
    uint32_t now_ms);

#endif
