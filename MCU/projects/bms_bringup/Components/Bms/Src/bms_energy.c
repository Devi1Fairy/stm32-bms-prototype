#include "bms_energy.h"
#include <stddef.h>

void Bms_EnergyInit(BmsEnergyCounter *counter, uint32_t max_gap_ms)
{
    if (counter != NULL)
    {
        BmsEnergyCounter empty = {0};
        *counter = empty;
        counter->max_gap_ms = max_gap_ms;
    }
}

void Bms_EnergyBreak(BmsEnergyCounter *counter)
{
    if ((counter != NULL) && (counter->have_previous != 0u))
    {
        if (counter->totals.gaps != UINT32_MAX)
        {
            counter->totals.gaps++;
        }

        counter->have_previous = 0u;
    }
}

static void Bms_EnergyRemember(BmsEnergyCounter *counter, int32_t current_ma,
                               uint32_t bus_mv, uint32_t timestamp_ms)
{
    counter->previous_ma = current_ma;
    counter->previous_mv = bus_mv;
    counter->previous_ms = timestamp_ms;
    counter->have_previous = 1u;
}

void Bms_EnergyUpdate(BmsEnergyCounter *counter, int32_t current_ma,
                      uint32_t bus_mv, uint32_t timestamp_ms)
{
    uint32_t dt;
    uint64_t magnitude;
    uint64_t charge;
    uint64_t energy;
    uint64_t *charge_total;
    uint64_t *energy_total;

    if ((counter == NULL) || (counter->totals.overflow != 0u))
    {
        return;
    }

    /* 当前输入来自INA226，检查它的总线测量范围。 */
    if (bus_mv > 36000u)
    {
        Bms_EnergyBreak(counter);
        return;
    }

    if (counter->have_previous == 0u)
    {
        /* 第一份数据只建立起点，没有前一段时间可供累计。 */
        Bms_EnergyRemember(counter, current_ma, bus_mv, timestamp_ms);
        return;
    }

    dt = (uint32_t)(timestamp_ms - counter->previous_ms);

    if (dt == 0u)
    {
        return; /* 同一份旧采样不能重复累计。 */
    }

    if (dt >= 0x80000000u)
    {
        /* 视作时间倒退，丢弃本次端点。 */
        Bms_EnergyBreak(counter);
        return;
    }

    if (dt > counter->max_gap_ms)
    {
        Bms_EnergyBreak(counter);
        Bms_EnergyRemember(counter, current_ma, bus_mv, timestamp_ms);
        return; /* 缺口不补算，只建立新起点。 */
    }

    /*
     * 前值矩形法：
     * 用上一份有效电流、电压近似表示本区间的测量值。
     */
    magnitude = counter->previous_ma < 0
        ? (uint64_t)(-(int64_t)counter->previous_ma)
        : (uint64_t)counter->previous_ma;

    charge = magnitude * (uint64_t)dt;

    if (counter->previous_ma < 0)
    {
        charge_total = &counter->totals.charge_mams;
        energy_total = &counter->totals.charge_nj;
    }
    else
    {
        charge_total = &counter->totals.discharge_mams;
        energy_total = &counter->totals.discharge_nj;
    }

    /* 先检查乘法，再计算能量。 */
    if ((counter->previous_mv != 0u)
        && (charge > UINT64_MAX / counter->previous_mv))
    {
        counter->totals.overflow = 1u;
        Bms_EnergyBreak(counter);
        return;
    }

    energy = charge * counter->previous_mv; /* mA×mV×ms = nJ。 */

    /* 所有累计值都能放下时，才一起提交。 */
    if ((charge > UINT64_MAX - *charge_total)
        || (energy > UINT64_MAX - *energy_total)
        || ((uint64_t)dt > UINT64_MAX - counter->totals.covered_ms))
    {
        counter->totals.overflow = 1u;
        Bms_EnergyBreak(counter);
        return;
    }

    *charge_total += charge;
    *energy_total += energy;
    counter->totals.covered_ms += dt;

    Bms_EnergyRemember(counter, current_ma, bus_mv, timestamp_ms);
}
