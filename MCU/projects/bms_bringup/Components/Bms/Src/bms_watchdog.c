#include "bms_watchdog.h"
#include "bms_health.h"
#include <stddef.h>

void Bms_WatchdogInit(BmsWatchdog *watchdog, uint32_t now_ms,
                      uint32_t startup_grace_ms)
{
    if (watchdog != NULL)
    {
        BmsWatchdog empty = {0};

        *watchdog = empty;
        watchdog->start_ms = now_ms;
        watchdog->startup_grace_ms = startup_grace_ms;
        watchdog->initialized = 1u;
    }
}

uint8_t Bms_WatchdogStep(BmsWatchdog *watchdog,
                        uint32_t missing_mask, uint32_t now_ms)
{
    if ((watchdog == NULL) || (watchdog->tripped != 0u))
        return 0u;

    if ((watchdog->initialized == 0u)
        || ((missing_mask & ~BMS_HEALTH_ALL_MASK) != 0u))
    {
        watchdog->tripped = 1u;
        watchdog->fault_mask = BMS_HEALTH_ALL_MASK;
        return 0u;
    }

    if (missing_mask == 0u)
    {
        watchdog->tasks_ready = 1u;
        return 1u;
    }

    /* 启动时任务尚未全部完成首轮工作，给予有限的宽限。 */
    if ((watchdog->tasks_ready == 0u)
        && ((uint32_t)(now_ms - watchdog->start_ms)
            < watchdog->startup_grace_ms))
        return 1u;

    /* 正常运行后任务超时，或启动宽限耗尽，都锁定停止喂狗。 */
    watchdog->fault_mask = missing_mask;
    watchdog->tripped = 1u;
    return 0u;
}
