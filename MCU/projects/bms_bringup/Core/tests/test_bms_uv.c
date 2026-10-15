#include <assert.h>
#include <stdio.h>
#include "bms_protection.h"

int main(void)
{
    BmsMeasurement cell = {0};

    /* 3000 mV 仅用于软件测试，实物电池的保护阈值尚未确定。 */
    const int32_t threshold_mv = 3000;

    cell.valid = 1u;
    cell.source = BMS_SOURCE_SIMULATED;

    /* 1：正常电压，不触发欠压。 */
    cell.value = 3700;
    assert(Bms_CheckCellUnderVoltage(&cell, threshold_mv)
           == BMS_UV_NOT_TRIGGERED);

    /* 2：高于阈值 1 mV，不触发。 */
    cell.value = 3001;
    assert(Bms_CheckCellUnderVoltage(&cell, threshold_mv)
           == BMS_UV_NOT_TRIGGERED);

    /* 3：刚好达到阈值，触发欠压。 */
    cell.value = 3000;
    assert(Bms_CheckCellUnderVoltage(&cell, threshold_mv)
           == BMS_UV_TRIGGERED);

    /* 4：低于阈值，也触发欠压。 */
    cell.value = 2900;
    assert(Bms_CheckCellUnderVoltage(&cell, threshold_mv)
           == BMS_UV_TRIGGERED);

    /* 5：标志有效的零电压，应触发欠压。 */
    cell.value = 0;
    assert(Bms_CheckCellUnderVoltage(&cell, threshold_mv)
           == BMS_UV_TRIGGERED);

    /* 6：同样是零电压，标志无效时应返回数据无效。 */
    cell.valid = 0u;
    assert(Bms_CheckCellUnderVoltage(&cell, threshold_mv)
           == BMS_UV_DATA_INVALID);
    cell.valid = 1u;
    cell.value = 3700;

    /* 7：负电压不是本项目认可的有效单体电压。 */
    cell.value = -1;
    assert(Bms_CheckCellUnderVoltage(&cell, threshold_mv)
           == BMS_UV_DATA_INVALID);
    cell.value = 3700;

    /* 8：没有来源，不能当作有效测量。 */
    cell.source = BMS_SOURCE_NONE;
    assert(Bms_CheckCellUnderVoltage(&cell, threshold_mv)
           == BMS_UV_DATA_INVALID);
    cell.source = BMS_SOURCE_SIMULATED;

    /* 9：零阈值属于参数错误。 */
    assert(Bms_CheckCellUnderVoltage(&cell, 0)
           == BMS_UV_DATA_INVALID);

    /* 10：负阈值同样属于参数错误。 */
    assert(Bms_CheckCellUnderVoltage(&cell, -1)
           == BMS_UV_DATA_INVALID);

    /* 11：空指针应返回无效，不能访问它的成员。 */
    assert(Bms_CheckCellUnderVoltage(NULL, threshold_mv)
           == BMS_UV_DATA_INVALID);

    puts("11 undervoltage tests passed.");
    return 0;
}