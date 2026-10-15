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
