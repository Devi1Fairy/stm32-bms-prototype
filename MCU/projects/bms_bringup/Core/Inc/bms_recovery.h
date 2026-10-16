#ifndef BMS_RECOVERY_H
#define BMS_RECOVERY_H

#include "bms_protection.h"

/* 恢复边界应比故障触发边界更靠近正常区间，具体值由配置提供。 */
typedef struct
{
    int32_t cell_min_mv;         /* 每节电压必须严格大于此值 */
    int32_t cell_max_mv;         /* 每节电压必须严格小于此值 */
    int32_t max_temperature_dC;  /* 温度必须严格小于此值，单位0.1℃ */
    int32_t discharge_limit_ma;  /* 电流必须严格小于此正数 */
    int32_t charge_limit_ma;     /* 电流必须严格大于此参数的负值 */
    uint32_t voltage_max_age_ms;
    uint32_t temperature_max_age_ms;
    uint32_t current_max_age_ms;
    uint32_t stable_time_ms;     /* 连续满足恢复条件的时间，必须大于0 */
} BmsRecoveryConfig;

/* 独立标志区分“尚未计时”和“从0 ms开始计时”。 */
typedef struct
{
    uint32_t stable_since_ms;
    uint8_t timing;              /* 1：正在累计稳定时间 */
} BmsRecovery;

/* 上电初始化；退出故障锁定后也调用，丢弃上次计时。 */
void Bms_RecoveryInit(BmsRecovery *recovery);

/*
 * 在故障锁定期间周期调用。
 * active_faults 是本轮正常保护检查的结果。
 * 返回1只表示恢复许可成立，清除仍须交给状态机判断。
 */
uint8_t Bms_RecoveryUpdate(
    BmsRecovery *recovery,
    const BmsSample *sample,
    const BmsRecoveryConfig *config,
    uint32_t active_faults,
    uint32_t now_ms);

#endif
	