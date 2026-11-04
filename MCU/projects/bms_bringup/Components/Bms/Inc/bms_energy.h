#ifndef BMS_ENERGY_H
#define BMS_ENERGY_H

#include <stdint.h>

/* 保留整数原始累计量，避免每次更新时丢掉小数。 */
typedef struct
{
    uint64_t discharge_mams; /* 放出电量：mA×ms。 */
    uint64_t charge_mams;    /* 充入电量：mA×ms。 */
    uint64_t discharge_nj;   /* 放出能量：nJ。 */
    uint64_t charge_nj;      /* 充入能量：nJ。 */
    uint64_t covered_ms;     /* 有效积分时间，包含有效零电流期间。 */
    uint32_t gaps;           /* 被切断的连续采样区间数。 */
    uint8_t overflow;        /* 溢出后停止累计，防止数值回绕。 */
} BmsEnergyTotals;

typedef struct
{
    BmsEnergyTotals totals;
    int32_t previous_ma;
    uint32_t previous_mv;
    uint32_t previous_ms;
    uint32_t max_gap_ms;
    uint8_t have_previous;
} BmsEnergyCounter;

void Bms_EnergyInit(BmsEnergyCounter *counter, uint32_t max_gap_ms);

void Bms_EnergyBreak(BmsEnergyCounter *counter);

/* 只在取得新的有效电流、电压时调用。 */
void Bms_EnergyUpdate(BmsEnergyCounter *counter, int32_t current_ma,
                      uint32_t bus_mv, uint32_t timestamp_ms);

#endif
