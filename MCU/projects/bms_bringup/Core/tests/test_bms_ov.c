#include <assert.h>
#include <stdio.h>
#include "bms_protection.h"

int main(void)
{
    BmsMeasurement cell = {0};

    /* 4200 mV 仅作为本次软件测试阈值，实物阈值尚未确定。 */
    const int32_t threshold_mv = 4200;

    /* 明确标记：这些数值由软件构造，没有连接真实电池。 */
    cell.valid = 1u;
    cell.source = BMS_SOURCE_SIMULATED;

    /* 普通电压不应触发过压。 */
    cell.value = 3700;
    assert(Bms_CheckCellOverVoltage(&cell, threshold_mv)
           == BMS_OV_NOT_TRIGGERED);

    /* 比阈值小 1 mV，仍不触发。 */
    cell.value = 4199;
    assert(Bms_CheckCellOverVoltage(&cell, threshold_mv)
           == BMS_OV_NOT_TRIGGERED);

    /* 刚好达到阈值，应触发。 */
    cell.value = 4200;
    assert(Bms_CheckCellOverVoltage(&cell, threshold_mv)
           == BMS_OV_TRIGGERED);

    /* 超过阈值，也应触发。 */
    cell.value = 4300;
    assert(Bms_CheckCellOverVoltage(&cell, threshold_mv)
           == BMS_OV_TRIGGERED);

    /* 即使数值看起来正常，标记无效后也不能正常使用。 */
    cell.value = 3700;
    cell.valid = 0u;
    assert(Bms_CheckCellOverVoltage(&cell, threshold_mv)
           == BMS_OV_DATA_INVALID);
    cell.valid = 1u;

    /* 负电压属于无效输入。 */
    cell.value = -1;
    assert(Bms_CheckCellOverVoltage(&cell, threshold_mv)
           == BMS_OV_DATA_INVALID);
    cell.value = 3700;

    /* 没有采样来源时，不能认为已经取得有效数据。 */
    cell.source = BMS_SOURCE_NONE;
    assert(Bms_CheckCellOverVoltage(&cell, threshold_mv)
           == BMS_OV_DATA_INVALID);
    cell.source = BMS_SOURCE_SIMULATED;

    /* 不合理的阈值和空指针，都应返回数据无效。 */
    assert(Bms_CheckCellOverVoltage(&cell, 0)
           == BMS_OV_DATA_INVALID);
    assert(Bms_CheckCellOverVoltage(NULL, threshold_mv)
           == BMS_OV_DATA_INVALID);

    puts("9 software tests passed.");
    return 0;
}