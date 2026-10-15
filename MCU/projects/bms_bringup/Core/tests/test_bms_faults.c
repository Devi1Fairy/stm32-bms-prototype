#include <assert.h>
#include <stdio.h>
#include "bms_protection.h"

int main(void)
{
    BmsVoltageReport voltage = {0};
    const BmsVoltageReport empty_report = {0};

    /* 1：三类检查都没有发现故障。 */
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_NORMAL)
        == BMS_FAULT_NONE);

    /* 2：第1节过压，汇总成“存在单体过压”。 */
    voltage.over_voltage_mask = 0x01u;
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_NORMAL)
        == BMS_FAULT_CELL_OV);
    voltage = empty_report;

    /* 3：第2节欠压。 */
    voltage.under_voltage_mask = 0x02u;
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_NORMAL)
        == BMS_FAULT_CELL_UV);
    voltage = empty_report;

    /* 4：单独过温。 */
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_OVER_LIMIT, BMS_CURRENT_NORMAL)
        == BMS_FAULT_OVER_TEMP);

    /* 5：单独放电过流。 */
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_DISCHARGE_OVER_LIMIT)
        == BMS_FAULT_DISCHARGE_OC);

    /* 6：单独充电过流。 */
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_CHARGE_OVER_LIMIT)
        == BMS_FAULT_CHARGE_OC);

    /* 7：第3节电压数据无效。 */
    voltage.invalid_mask = 0x04u;
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_NORMAL)
        == BMS_FAULT_DATA_INVALID);
    voltage = empty_report;

    /* 8：温度数据无效。 */
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_DATA_INVALID, BMS_CURRENT_NORMAL)
        == BMS_FAULT_DATA_INVALID);

    /* 9：电流数据无效。 */
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_DATA_INVALID)
        == BMS_FAULT_DATA_INVALID);

    /* 10：第1节电压数据超时。 */
    voltage.timeout_mask = 0x01u;
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_NORMAL)
        == BMS_FAULT_SAMPLE_TIMEOUT);
    voltage = empty_report;

    /* 11：温度超时。 */
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_TIMEOUT, BMS_CURRENT_NORMAL)
        == BMS_FAULT_SAMPLE_TIMEOUT);

    /* 12：电流超时。 */
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_TIMEOUT)
        == BMS_FAULT_SAMPLE_TIMEOUT);

    /* 13：电压检查报告参数错误。 */
    voltage.input_error = 1u;
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_NORMAL)
        == BMS_FAULT_INPUT_ERROR);
    voltage = empty_report;

    /* 14：温度检查报告参数错误。 */
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_INPUT_ERROR, BMS_CURRENT_NORMAL)
        == BMS_FAULT_INPUT_ERROR);

    /* 15：电流检查报告参数错误。 */
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_INPUT_ERROR)
        == BMS_FAULT_INPUT_ERROR);

    /* 16：没有电压报告，不能认为全部正常。 */
    assert(Bms_CombineFaults(
        NULL, BMS_TEMP_NORMAL, BMS_CURRENT_NORMAL)
        == BMS_FAULT_INPUT_ERROR);

    /*
     * 17：不同通道同时故障，所有对应位都要保留。
     * 第1节过压、第2节欠压、第3节无效；
     * 温度超时，同时放电过流。
     */
    voltage.over_voltage_mask = 0x01u;
    voltage.under_voltage_mask = 0x02u;
    voltage.invalid_mask = 0x04u;
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_TIMEOUT, BMS_CURRENT_DISCHARGE_OVER_LIMIT)
        == (BMS_FAULT_CELL_OV |
            BMS_FAULT_CELL_UV |
            BMS_FAULT_DATA_INVALID |
            BMS_FAULT_SAMPLE_TIMEOUT |
            BMS_FAULT_DISCHARGE_OC));

    /* 18：电压报告为空时，仍保留过温和充电过流。 */
    assert(Bms_CombineFaults(
        NULL, BMS_TEMP_OVER_LIMIT, BMS_CURRENT_CHARGE_OVER_LIMIT)
        == (BMS_FAULT_INPUT_ERROR |
            BMS_FAULT_OVER_TEMP |
            BMS_FAULT_CHARGE_OC));

    /* 19：新的检查全部正常，汇总函数不保留历史故障。 */
    voltage = empty_report;
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, BMS_CURRENT_NORMAL)
        == BMS_FAULT_NONE);

    /* 20：传入未定义的温度结果，报告输入错误。 */
    assert(Bms_CombineFaults(
        &voltage, (BmsTemperatureResult)99, BMS_CURRENT_NORMAL)
        == BMS_FAULT_INPUT_ERROR);

    /* 21：传入未定义的电流结果，报告输入错误。 */
    assert(Bms_CombineFaults(
        &voltage, BMS_TEMP_NORMAL, (BmsCurrentResult)99)
        == BMS_FAULT_INPUT_ERROR);

    puts("21 fault aggregation tests passed.");
    return 0;
}