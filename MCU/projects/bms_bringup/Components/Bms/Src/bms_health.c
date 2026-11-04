#include "bms_health.h"
#include <stddef.h>

void Bms_HealthMark(BmsTaskHealth *health, BmsHealthTask task,
                    uint32_t now_ms)
{
    uint32_t index = (uint32_t)task;

    if ((health == NULL) || (index >= BMS_HEALTH_TASK_COUNT))
        return;

    health->last_ms[index] = now_ms;
    health->seen_mask |= (1u << index);
}

uint32_t Bms_HealthMissing(const BmsTaskHealth *health,
                           uint32_t now_ms, uint32_t timeout_ms)
{
    uint32_t missing = 0u;
    uint32_t i;

    if ((health == NULL) || (timeout_ms == 0u))
        return BMS_HEALTH_ALL_MASK;

    for (i = 0u; i < BMS_HEALTH_TASK_COUNT; ++i)
    {
        uint32_t bit = 1u << i;

        /* 未报到不能把初始时间0误认为刚刚更新。 */
        if (((health->seen_mask & bit) == 0u)
            || ((uint32_t)(now_ms - health->last_ms[i]) >= timeout_ms))
        {
            missing |= bit;
        }
    }

    return missing;
}
