#include <assert.h>
#include <stdio.h>
#include "bms_protection.h"

int main(void)
{
    BmsMeasurement cell = {0};

    /* 500 ms 只是本次测试参数；实际值以后按采样周期确定。 */
    const uint32_t max_age_ms = 500u;

    cell.value = 3700;
    cell.timestamp_ms = 1000u;
    cell.valid = 1u;
    cell.source = BMS_SOURCE_SIMULATED;

    /* 1：刚取得的数据，年龄为 0 ms。 */
    assert(Bms_CheckSampleFreshness(&cell, 1000u, max_age_ms)
           == BMS_SAMPLE_FRESH);

    /* 2：年龄 499 ms，比超时阈值小 1 ms。 */
    assert(Bms_CheckSampleFreshness(&cell, 1499u, max_age_ms)
           == BMS_SAMPLE_FRESH);

    /* 3：年龄刚好 500 ms，应判为超时。 */
    assert(Bms_CheckSampleFreshness(&cell, 1500u, max_age_ms)
           == BMS_SAMPLE_TIMEOUT);

    /* 4：年龄超过 500 ms，也应判为超时。 */
    assert(Bms_CheckSampleFreshness(&cell, 1501u, max_age_ms)
           == BMS_SAMPLE_TIMEOUT);

    /* 5：启动时刻的计数可以是 0；不能仅凭时间为 0 就判无效。 */
    cell.timestamp_ms = 0u;
    assert(Bms_CheckSampleFreshness(&cell, 0u, max_age_ms)
           == BMS_SAMPLE_FRESH);

    /* 6：没有传入测量对象。 */
    assert(Bms_CheckSampleFreshness(NULL, 1000u, max_age_ms)
           == BMS_SAMPLE_INVALID);

    /* 7：即使数值正常，标记无效的数据也不能通过检查。 */
    cell.valid = 0u;
    assert(Bms_CheckSampleFreshness(&cell, 1000u, max_age_ms)
           == BMS_SAMPLE_INVALID);
    cell.valid = 1u;

    /* 8：尚未取得数据来源。 */
    cell.source = BMS_SOURCE_NONE;
    assert(Bms_CheckSampleFreshness(&cell, 1000u, max_age_ms)
           == BMS_SAMPLE_INVALID);
    cell.source = BMS_SOURCE_SIMULATED;

    /* 9：不接受为 0 的超时参数。 */
    assert(Bms_CheckSampleFreshness(&cell, 1000u, 0u)
           == BMS_SAMPLE_INVALID);

    /*
     * 10、11：测试计数回绕。
     * UINT32_MAX 是 32 位无符号整数的最大值。
     * 从 UINT32_MAX - 99 到回绕后的 0，经过 100 ms。
     */
    cell.timestamp_ms = UINT32_MAX - 99u;

    /* 再走到 399，总共经过 499 ms。 */
    assert(Bms_CheckSampleFreshness(&cell, 399u, max_age_ms)
           == BMS_SAMPLE_FRESH);

    /* 再走到 400，总共经过 500 ms。 */
    assert(Bms_CheckSampleFreshness(&cell, 400u, max_age_ms)
           == BMS_SAMPLE_TIMEOUT);

    puts("11 sample timeout tests passed.");
    return 0;
}