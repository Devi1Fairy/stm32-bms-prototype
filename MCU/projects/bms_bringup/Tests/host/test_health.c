#include "bms_health.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    BmsTaskHealth health = {0};
    BmsTaskHealth wrapped = {0};
    uint32_t i;

    /* 启动时没有任务报到，不能判为健康。 */
    assert(Bms_HealthMissing(&health, 0u, 2000u) == 0x0Fu);

    /* 只报到保护任务，另外三个任务仍缺失。 */
    Bms_HealthMark(&health, BMS_HEALTH_CONTROL, 0u);
    assert(health.seen_mask == 0x01u);
    assert(Bms_HealthMissing(&health, 1u, 2000u) == 0x0Eu);

    /* 1999ms有效，2000ms到达超时边界。 */
    for (i = 0u; i < BMS_HEALTH_TASK_COUNT; ++i)
        Bms_HealthMark(&health, (BmsHealthTask)i, 100u);

    assert(health.seen_mask == 0x0Fu);
    assert(Bms_HealthMissing(&health, 2099u, 2000u) == 0u);
    assert(Bms_HealthMissing(&health, 2100u, 2000u) == 0x0Fu);

    /* 其他任务不断报到，不能替代漏报的采集任务。 */
    Bms_HealthMark(&health, BMS_HEALTH_CONTROL, 2100u);
    Bms_HealthMark(&health, BMS_HEALTH_COMM, 2100u);
    Bms_HealthMark(&health, BMS_HEALTH_LED, 2100u);

    assert(Bms_HealthMissing(&health, 2100u, 2000u) == 0x02u);

    /* 采集恢复报到，重新健康。 */
    Bms_HealthMark(&health, BMS_HEALTH_SAMPLE, 2101u);
    assert(Bms_HealthMissing(&health, 2101u, 2000u) == 0u);

    /* 两个任务同时超时，必须保留两个对应位。 */
    Bms_HealthMark(&health, BMS_HEALTH_COMM, 4200u);
    Bms_HealthMark(&health, BMS_HEALTH_LED, 4200u);

    assert(Bms_HealthMissing(&health, 4200u, 2000u) == 0x03u);

    /* 毫秒计数正常回绕，实际只经过200ms。 */
    for (i = 0u; i < BMS_HEALTH_TASK_COUNT; ++i)
        Bms_HealthMark(&wrapped, (BmsHealthTask)i, UINT32_MAX - 99u);

    assert(Bms_HealthMissing(&wrapped, 100u, 201u) == 0u);
    assert(Bms_HealthMissing(&wrapped, 100u, 200u) == 0x0Fu);

    /* 异常参数不能越界写，也不能误报健康。 */
    Bms_HealthMark(&health, (BmsHealthTask)4, 9999u);
    Bms_HealthMark(&health, (BmsHealthTask)-1, 9999u);
    Bms_HealthMark(NULL, BMS_HEALTH_CONTROL, 0u);

    assert(health.seen_mask == 0x0Fu && health.last_ms[0] == 2100u);
    assert(health.last_ms[1] == 2101u);
    assert(Bms_HealthMissing(NULL, 0u, 2000u) == 0x0Fu);
    assert(Bms_HealthMissing(&health, 0u, 0u) == 0x0Fu);

    puts("Task health checks passed.");
    return 0;
}