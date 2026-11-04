#include "bms_watchdog.h"
#include "bms_health.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    BmsWatchdog watchdog = {0};
    uint32_t now_ms;

    /* 没初始化不能喂狗。 */
    assert(Bms_WatchdogStep(&watchdog, 0u, 0u) == 0u);
    assert(watchdog.tripped && watchdog.fault_mask == 0x0Fu);

    /* 一直没有全部就绪，宽限不能永久续期。 */
    Bms_WatchdogInit(&watchdog, 100u, 3000u);

    assert(Bms_WatchdogStep(&watchdog, 0x0Fu, 100u) == 1u);
    assert(Bms_WatchdogStep(&watchdog, 0x02u, 3099u) == 1u);
    assert(!watchdog.tasks_ready && !watchdog.tripped);

    assert(Bms_WatchdogStep(&watchdog, 0x02u, 3100u) == 0u);
    assert(watchdog.tripped && watchdog.fault_mask == 0x02u);

    /* 恢复报到不能取消已经触发的复位决定。 */
    assert(Bms_WatchdogStep(&watchdog, 0u, 3101u) == 0u);
    assert(watchdog.fault_mask == 0x02u);

    /* 曾经全部就绪后，不再享有启动宽限。 */
    Bms_WatchdogInit(&watchdog, 0u, 3000u);

    assert(Bms_WatchdogStep(&watchdog, 0u, 100u) == 1u);
    assert(watchdog.tasks_ready);

    assert(Bms_WatchdogStep(&watchdog, 0x04u, 500u) == 0u);
    assert(watchdog.tripped && watchdog.fault_mask == 0x04u);

    assert(Bms_WatchdogStep(&watchdog, 0u, 600u) == 0u);
    assert(Bms_WatchdogStep(&watchdog, 0x02u, 700u) == 0u);
    assert(watchdog.fault_mask == 0x04u);

    /* 长时间健康运行，持续允许喂狗。 */
    Bms_WatchdogInit(&watchdog, 0u, 3000u);

    for (now_ms = 0u; now_ms <= 600000u; now_ms += 500u)
        assert(Bms_WatchdogStep(&watchdog, 0u, now_ms) == 1u);

    assert(!watchdog.tripped);

    /* 启动宽限支持32位毫秒计数回绕。 */
    Bms_WatchdogInit(&watchdog, UINT32_MAX - 999u, 3000u);

    assert(Bms_WatchdogStep(&watchdog, 0x01u, 1999u) == 1u);
    assert(Bms_WatchdogStep(&watchdog, 0x01u, 2000u) == 0u);

    /* 零宽限、非法位图和空指针。 */
    Bms_WatchdogInit(&watchdog, 0u, 0u);
    assert(Bms_WatchdogStep(&watchdog, 0x0Fu, 0u) == 0u);

    Bms_WatchdogInit(&watchdog, 0u, 3000u);
    assert(Bms_WatchdogStep(&watchdog, 0x10u, 0u) == 0u);
    assert(watchdog.tripped && watchdog.fault_mask == 0x0Fu);

    Bms_WatchdogInit(NULL, 0u, 3000u);
    assert(Bms_WatchdogStep(NULL, 0u, 0u) == 0u);

    puts("Watchdog checks passed.");
    return 0;
}