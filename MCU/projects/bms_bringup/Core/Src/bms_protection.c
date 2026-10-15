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
    int32_t uv_threshold_mv,
    int32_t ov_threshold_mv,
    uint32_t now_ms,
    uint32_t max_age_ms)
{
    BmsVoltageReport report = {0};
    uint32_t i;
    uint8_t cell_bit;
    const BmsMeasurement *cell;
    BmsSampleStatus sample_status;
    BmsOvResult ov_result;
    BmsUvResult uv_result;

    /*
     * 下限必须大于0，上限必须高于下限。
     * 上下限相等或顺序颠倒时，不能继续做正常保护判断。
     */
    if (sample == NULL ||
        uv_threshold_mv <= 0 ||
        ov_threshold_mv <= uv_threshold_mv ||
        max_age_ms == 0u)
    {
        report.input_error = 1u;
        return report;
    }

    for (i = 0u; i < BMS_CELL_COUNT; i++)
    {
        cell = &sample->cell_mv[i];
        cell_bit = (uint8_t)(1u << i);

        /* 先检查标志、来源和时间，旧数据不用于报告当前过压或欠压。 */
        sample_status = Bms_CheckSampleFreshness(
            cell, now_ms, max_age_ms);

        if (sample_status == BMS_SAMPLE_INVALID)
        {
            report.invalid_mask |= cell_bit;
            continue;
        }

        if (sample_status == BMS_SAMPLE_TIMEOUT)
        {
            report.timeout_mask |= cell_bit;
            continue;
        }

        /* 复用两个单节函数，不在汇总函数中重复实现阈值比较。 */
        ov_result = Bms_CheckCellOverVoltage(cell, ov_threshold_mv);
        uv_result = Bms_CheckCellUnderVoltage(cell, uv_threshold_mv);

        if (ov_result == BMS_OV_DATA_INVALID ||
            uv_result == BMS_UV_DATA_INVALID)
        {
            report.invalid_mask |= cell_bit;
        }
        else if (ov_result == BMS_OV_TRIGGERED)
        {
            report.over_voltage_mask |= cell_bit;
        }
        else if (uv_result == BMS_UV_TRIGGERED)
        {
            report.under_voltage_mask |= cell_bit;
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

BmsTemperatureResult Bms_CheckTemperature(
    const BmsMeasurement *temperature,
    int32_t over_temp_dC,
    uint32_t now_ms,
    uint32_t max_age_ms)
{
    BmsSampleStatus sample_status;

    /* 先区分调用参数错误与测量数据本身无效。 */
    if (temperature == NULL || max_age_ms == 0u)
    {
        return BMS_TEMP_INPUT_ERROR;
    }

    /* 复用时效检查，避免每种保护都重写时间差计算。 */
    sample_status = Bms_CheckSampleFreshness(
        temperature, now_ms, max_age_ms);

    if (sample_status == BMS_SAMPLE_INVALID)
    {
        return BMS_TEMP_DATA_INVALID;
    }

    if (sample_status == BMS_SAMPLE_TIMEOUT)
    {
        /* 旧温度值不能用于报告当前过温。 */
        return BMS_TEMP_TIMEOUT;
    }

    /*
     * 温度与阈值都使用 0.1℃，可以直接比较整数。
     * 达到阈值就触发，包含刚好相等的情况。
     */
    if (temperature->value >= over_temp_dC)
    {
        return BMS_TEMP_OVER_LIMIT;
    }

    return BMS_TEMP_NORMAL;
}

BmsCurrentResult Bms_CheckCurrent(
    const BmsMeasurement *current,
    int32_t discharge_limit_ma,
    int32_t charge_limit_ma,
    uint32_t now_ms,
    uint32_t max_age_ms)
{
    BmsSampleStatus sample_status;

    /* 两个方向的限值都必须是正数，方向由电流值的正负表示。 */
    if (current == NULL ||
        discharge_limit_ma <= 0 ||
        charge_limit_ma <= 0 ||
        max_age_ms == 0u)
    {
        return BMS_CURRENT_INPUT_ERROR;
    }

    sample_status = Bms_CheckSampleFreshness(
        current, now_ms, max_age_ms);

    if (sample_status == BMS_SAMPLE_INVALID)
    {
        return BMS_CURRENT_DATA_INVALID;
    }

    if (sample_status == BMS_SAMPLE_TIMEOUT)
    {
        /* 旧电流不用于报告当前是否过流。 */
        return BMS_CURRENT_TIMEOUT;
    }

    /* 正方向达到限值，触发放电过流。 */
    if (current->value >= discharge_limit_ma)
    {
        return BMS_CURRENT_DISCHARGE_OVER_LIMIT;
    }

    /*
     * 将正数的充电限值变为负边界，再直接比较。
     * 不对测量值取绝对值，避免 INT32_MIN 取负时溢出。
     * charge_limit_ma 已确认是正数，因此对它取负可以安全表示。
     */
    if (current->value <= -charge_limit_ma)
    {
        return BMS_CURRENT_CHARGE_OVER_LIMIT;
    }

    return BMS_CURRENT_NORMAL;
}
