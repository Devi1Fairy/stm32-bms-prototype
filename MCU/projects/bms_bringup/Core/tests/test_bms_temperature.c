#include <assert.h>
#include <stdio.h>
#include "bms_protection.h"

int main(void)
{
    BmsMeasurement temperature = {0};

    /* 60.0℃ 和 500 ms 仅为软件测试参数，实物配置尚未确定。 */
    const int32_t over_temp_dC = 600;
    const uint32_t now_ms = 1499u;
    const uint32_t max_age_ms = 500u;

    temperature.timestamp_ms = 1000u;
    temperature.valid = 1u;
    temperature.source = BMS_SOURCE_SIMULATED;

    /* 1：25.0℃，正常。 */
    temperature.value = 250;
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, now_ms, max_age_ms)
        == BMS_TEMP_NORMAL);

    /* 2：59.9℃，比阈值低0.1℃。 */
    temperature.value = 599;
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, now_ms, max_age_ms)
        == BMS_TEMP_NORMAL);

    /* 3：60.0℃，刚好达到阈值。 */
    temperature.value = 600;
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, now_ms, max_age_ms)
        == BMS_TEMP_OVER_LIMIT);

    /* 4：65.0℃，超过阈值。 */
    temperature.value = 650;
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, now_ms, max_age_ms)
        == BMS_TEMP_OVER_LIMIT);

    /* 5：-5.0℃可以是有效温度，不触发本项过温。 */
    temperature.value = -50;
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, now_ms, max_age_ms)
        == BMS_TEMP_NORMAL);

    /* 6：有效的0℃也不应被误判为无效。 */
    temperature.value = 0;
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, now_ms, max_age_ms)
        == BMS_TEMP_NORMAL);

    /* 7：数值看起来正常，但标志无效。 */
    temperature.value = 250;
    temperature.valid = 0u;
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, now_ms, max_age_ms)
        == BMS_TEMP_DATA_INVALID);
    temperature.valid = 1u;

    /* 8：没有数据来源。 */
    temperature.source = BMS_SOURCE_NONE;
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, now_ms, max_age_ms)
        == BMS_TEMP_DATA_INVALID);
    temperature.source = BMS_SOURCE_SIMULATED;

    /* 9：年龄达到500 ms，温度数值正常也应报告超时。 */
    temperature.timestamp_ms = 999u;
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, now_ms, max_age_ms)
        == BMS_TEMP_TIMEOUT);

    /* 10：旧温度偏高，也只报告超时。 */
    temperature.value = 650;
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, now_ms, max_age_ms)
        == BMS_TEMP_TIMEOUT);
    temperature.timestamp_ms = 1000u;
    temperature.value = 250;

    /* 11：空指针属于调用参数错误。 */
    assert(Bms_CheckTemperature(
        NULL, over_temp_dC, now_ms, max_age_ms)
        == BMS_TEMP_INPUT_ERROR);

    /* 12：超时参数为0，属于调用参数错误。 */
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, now_ms, 0u)
        == BMS_TEMP_INPUT_ERROR);

    /* 13：阈值可以是0℃；-0.1℃尚未达到它。 */
    temperature.value = -1;
    assert(Bms_CheckTemperature(
        &temperature, 0, now_ms, max_age_ms)
        == BMS_TEMP_NORMAL);

    /* 14：阈值为0℃时，0℃刚好触发。 */
    temperature.value = 0;
    assert(Bms_CheckTemperature(
        &temperature, 0, now_ms, max_age_ms)
        == BMS_TEMP_OVER_LIMIT);

    /* 15：阈值可以为负；-6.0℃低于-5.0℃。 */
    temperature.value = -60;
    assert(Bms_CheckTemperature(
        &temperature, -50, now_ms, max_age_ms)
        == BMS_TEMP_NORMAL);

    /* 16：负阈值也包含相等边界。 */
    temperature.value = -50;
    assert(Bms_CheckTemperature(
        &temperature, -50, now_ms, max_age_ms)
        == BMS_TEMP_OVER_LIMIT);

    /* 17：计数回绕后年龄为499 ms，仍未超时。 */
    temperature.value = 250;
    temperature.timestamp_ms = UINT32_MAX - 99u;
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, 399u, max_age_ms)
        == BMS_TEMP_NORMAL);

    /* 18：计数回绕后年龄为500 ms，刚好超时。 */
    assert(Bms_CheckTemperature(
        &temperature, over_temp_dC, 400u, max_age_ms)
        == BMS_TEMP_TIMEOUT);

    puts("18 temperature tests passed.");
    return 0;
}