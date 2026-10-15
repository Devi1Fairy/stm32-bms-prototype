#include "bms_protection.h"
#include <stddef.h> /* 提供 NULL，表示没有传入有效对象 */

BmsOvResult Bms_CheckCellOverVoltage(
    const BmsMeasurement *cell,
    int32_t threshold_mv)
{
    /*
     * 没有数据、数据未通过检查，或尚未取得数据时，
     * 当前无法判断是否过压。
     * || 从左到右判断，cell 为 NULL 时不会访问后面的成员。
     */
    if (cell == NULL ||
        cell->valid != 1u ||
        cell->source == BMS_SOURCE_NONE)
    {
        return BMS_OV_DATA_INVALID;
    }

    /* 电池电压不能为负数，阈值也必须是正数。 */
    if (cell->value < 0 || threshold_mv <= 0)
    {
        return BMS_OV_DATA_INVALID;
    }

    /* 本练习约定：达到阈值就触发，包含刚好相等的情况。 */
    if (cell->value >= threshold_mv)
    {
        return BMS_OV_TRIGGERED;
    }

    return BMS_OV_NOT_TRIGGERED;
}

BmsSampleStatus Bms_CheckSampleFreshness(
    const BmsMeasurement *measurement,
    uint32_t now_ms,
    uint32_t max_age_ms)
{
    uint32_t age_ms;

    /* 没有数据或参数不合要求时，不能返回“未超时”。 */
    if (measurement == NULL ||
        measurement->valid != 1u ||
        measurement->source == BMS_SOURCE_NONE ||
        max_age_ms == 0u)
    {
        return BMS_SAMPLE_INVALID;
    }

    /*
     * 数据年龄 = 当前时间 - 取得这份数据的时间。
     * 使用无符号减法，可以处理毫秒计数跨过最大值后从 0 继续的情况。
     * 前提：时间来自同一个计数器，采样先于检查，
     *       并且实际间隔小于一个完整计数周期。
     */
    age_ms = (uint32_t)(now_ms - measurement->timestamp_ms);

    /* 本项目约定：达到超时阈值，就不再把数据视为及时更新。 */
    if (age_ms >= max_age_ms)
    {
        return BMS_SAMPLE_TIMEOUT;
    }

    return BMS_SAMPLE_FRESH;
}

BmsVoltageReport Bms_CheckPackVoltages(
    const BmsSample *sample,
    int32_t ov_threshold_mv,
    uint32_t now_ms,
    uint32_t max_age_ms)
{
    /* 每次检查都从零开始，避免混入上一次的检查结果。 */
    BmsVoltageReport report = {0};

    uint32_t i;
    uint8_t cell_bit;
    const BmsMeasurement *cell;
    BmsSampleStatus sample_status;
    BmsOvResult ov_result;

    /*
     * 整组数据不存在，或者检查参数不合理时，
     * 单独报告输入错误，不继续访问测量数据。
     */
    if (sample == NULL ||
        ov_threshold_mv <= 0 ||
        max_age_ms == 0u)
    {
        report.input_error = 1u;
        return report;
    }

    /* 依次检查每一节电池。 */
    for (i = 0u; i < BMS_CELL_COUNT; i++)
    {
        /* 取得当前单体测量对象的地址。 */
        cell = &sample->cell_mv[i];

        /*
         * 将二进制的1向左移动i位。
         * i为0、1、2时，分别得到001、010、100。
         */
        cell_bit = (uint8_t)(1u << i);

        /* 先检查数据标志、来源和更新时间。 */
        sample_status = Bms_CheckSampleFreshness(
            cell,
            now_ms,
            max_age_ms);

        if (sample_status == BMS_SAMPLE_INVALID)
        {
            /* 记录当前单体无效，同时保留其他单体的记录。 */
            report.invalid_mask |= cell_bit;

            /* 本节无法继续判断，转到下一节。 */
            continue;
        }

        if (sample_status == BMS_SAMPLE_TIMEOUT)
        {
            report.timeout_mask |= cell_bit;

            /* 旧数据不用于报告当前过压。 */
            continue;
        }

        /* 数据通过时效检查后，再检查电压数值和过压阈值。 */
        ov_result = Bms_CheckCellOverVoltage(
            cell,
            ov_threshold_mv);

        if (ov_result == BMS_OV_DATA_INVALID)
        {
            /* 例如：数据标志有效，但电压数值为负数。 */
            report.invalid_mask |= cell_bit;
        }
        else if (ov_result == BMS_OV_TRIGGERED)
        {
            report.over_voltage_mask |= cell_bit;
        }
    }

    return report;
}

BmsUvResult Bms_CheckCellUnderVoltage(
    const BmsMeasurement *cell,
    int32_t threshold_mv)
{
    /*
     * 先检查指针，再访问对象成员。
     * valid 不为 1 或没有来源的数据，不能用于正常电压判断。
     */
    if (cell == NULL ||
        cell->valid != 1u ||
        cell->source == BMS_SOURCE_NONE)
    {
        return BMS_UV_DATA_INVALID;
    }

    /* 负电压和非正数阈值不符合本函数的输入约定。 */
    if (cell->value < 0 || threshold_mv <= 0)
    {
        return BMS_UV_DATA_INVALID;
    }

    /*
     * 本项目约定：达到或低于欠压阈值，就触发欠压。
     * value 为 0 且标志有效时，也应触发，不能写成 value <= 0 判无效。
     */
    if (cell->value <= threshold_mv)
    {
        return BMS_UV_TRIGGERED;
    }

    return BMS_UV_NOT_TRIGGERED;
}
