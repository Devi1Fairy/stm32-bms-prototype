#include <assert.h>
#include <stdio.h>
#include "bms_protection.h"

int main(void)
{
    BmsMeasurement current = {0};

    /* 仅为软件测试参数，实物限流值尚未确定。 */
    const int32_t discharge_limit_ma = 1000;
    const int32_t charge_limit_ma = 500;
    const uint32_t now_ms = 1499u;
    const uint32_t max_age_ms = 500u;

    current.timestamp_ms = 1000u;
    current.valid = 1u;
    current.source = BMS_SOURCE_SIMULATED;

    /* 1：有效零电流，正常。 */
    current.value = 0;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_NORMAL);

    /* 2：放电电流比限值小1 mA。 */
    current.value = 999;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_NORMAL);

    /* 3：刚好达到放电限值。 */
    current.value = 1000;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_DISCHARGE_OVER_LIMIT);

    /* 4：超过放电限值。 */
    current.value = 1001;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_DISCHARGE_OVER_LIMIT);

    /* 5：负电流可以有效，充电大小尚未达到500 mA。 */
    current.value = -499;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_NORMAL);

    /* 6：刚好达到充电限值，对应负边界-500。 */
    current.value = -500;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_CHARGE_OVER_LIMIT);

    /* 7：充电电流大小超过限值。 */
    current.value = -501;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_CHARGE_OVER_LIMIT);

    /* 8：数值看起来正常，但标志无效。 */
    current.value = 100;
    current.valid = 0u;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_DATA_INVALID);
    current.valid = 1u;

    /* 9：没有数据来源。 */
    current.source = BMS_SOURCE_NONE;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_DATA_INVALID);
    current.source = BMS_SOURCE_SIMULATED;

    /* 10：年龄刚好500 ms，报告超时。 */
    current.timestamp_ms = 999u;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_TIMEOUT);

    /* 11：旧值偏大，只报告超时。 */
    current.value = 1001;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_TIMEOUT);

    /* 12：旧负值的大小偏大，也只报告超时。 */
    current.value = -501;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_TIMEOUT);
    current.timestamp_ms = 1000u;
    current.value = 0;

    /* 13：空指针属于输入错误。 */
    assert(Bms_CheckCurrent(
        NULL, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_INPUT_ERROR);

    /* 14：放电限值为0，不接受。 */
    assert(Bms_CheckCurrent(
        &current, 0, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_INPUT_ERROR);

    /* 15：放电限值为负数，不接受。 */
    assert(Bms_CheckCurrent(
        &current, -1, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_INPUT_ERROR);

    /* 16：充电限值参数也必须填写正数。 */
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, 0, now_ms, max_age_ms)
        == BMS_CURRENT_INPUT_ERROR);

    /* 17：传入负数充电限值属于输入错误。 */
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, -1, now_ms, max_age_ms)
        == BMS_CURRENT_INPUT_ERROR);

    /* 18：超时参数为0，属于输入错误。 */
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, 0u)
        == BMS_CURRENT_INPUT_ERROR);

    /* 19：最大正整数，仍能正确比较。 */
    current.value = INT32_MAX;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_DISCHARGE_OVER_LIMIT);

    /* 20：最小负整数，不取绝对值也能正确比较。 */
    current.value = INT32_MIN;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, now_ms, max_age_ms)
        == BMS_CURRENT_CHARGE_OVER_LIMIT);

    /* 21：计数回绕后年龄499 ms，仍正常。 */
    current.value = 0;
    current.timestamp_ms = UINT32_MAX - 99u;
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, 399u, max_age_ms)
        == BMS_CURRENT_NORMAL);

    /* 22：计数回绕后年龄500 ms，刚好超时。 */
    assert(Bms_CheckCurrent(
        &current, discharge_limit_ma, charge_limit_ma, 400u, max_age_ms)
        == BMS_CURRENT_TIMEOUT);

    puts("22 current tests passed.");
    return 0;
}