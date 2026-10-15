#include <assert.h>
#include <stdio.h>
#include "bms_protection.h"

/* 每个场景重新构造正常快照，避免混入上一场景的数据。 */
static BmsSample MakeNormalSample(void)
{
    BmsSample sample = {0};
    uint32_t i;

    for (i = 0u; i < BMS_CELL_COUNT; i++)
    {
        sample.cell_mv[i].value = 3700;
        sample.cell_mv[i].timestamp_ms = 1000u;
        sample.cell_mv[i].valid = 1u;
        sample.cell_mv[i].source = BMS_SOURCE_SIMULATED;
    }

    return sample;
}

/* 预期参数顺序：过压、欠压、无效、超时、输入错误。 */
static void AssertReport(
    BmsVoltageReport report,
    uint8_t expected_ov,
    uint8_t expected_uv,
    uint8_t expected_invalid,
    uint8_t expected_timeout,
    uint8_t expected_input_error)
{
    assert(report.over_voltage_mask == expected_ov);
    assert(report.under_voltage_mask == expected_uv);
    assert(report.invalid_mask == expected_invalid);
    assert(report.timeout_mask == expected_timeout);
    assert(report.input_error == expected_input_error);
}

int main(void)
{
    BmsSample sample;
    BmsSample empty_sample = {0};
    uint32_t i;

    /* 测试用参数，尚不是实物电池的保护配置。 */
    const int32_t uv_threshold_mv = 3000;
    const int32_t ov_threshold_mv = 4200;
    const uint32_t now_ms = 1499u;
    const uint32_t max_age_ms = 500u;

    /* 1：三节数据均正常，五个字段都应为0。 */
    sample = MakeNormalSample();
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0u, 0u, 0u);

    /* 2：第1节刚好达到过压阈值。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].value = 4200;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0x01u, 0u, 0u, 0u, 0u);

    /* 3：第2节过压。 */
    sample = MakeNormalSample();
    sample.cell_mv[1].value = 4300;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0x02u, 0u, 0u, 0u, 0u);

    /* 4：第3节过压。 */
    sample = MakeNormalSample();
    sample.cell_mv[2].value = 4300;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0x04u, 0u, 0u, 0u, 0u);

    /* 5：第1、3节同时过压。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].value = 4300;
    sample.cell_mv[2].value = 4300;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0x05u, 0u, 0u, 0u, 0u);

    /* 6：三节分别出现无效标志、无来源和负电压。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].valid = 0u;
    sample.cell_mv[1].source = BMS_SOURCE_NONE;
    sample.cell_mv[2].value = -1;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0x07u, 0u, 0u);

    /* 7：第1节过压、第2节无效、第3节刚好超时。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].value = 4300;
    sample.cell_mv[1].valid = 0u;
    sample.cell_mv[2].timestamp_ms = 999u;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0x01u, 0u, 0x02u, 0x04u, 0u);

    /* 8：第2节旧值偏高，只报告超时。 */
    sample = MakeNormalSample();
    sample.cell_mv[1].value = 4300;
    sample.cell_mv[1].timestamp_ms = 999u;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0u, 0x02u, 0u);

    /* 9：空指针必须报告输入错误。 */
    sample = MakeNormalSample();
    AssertReport(Bms_CheckPackVoltages(
        NULL, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0u, 0u, 1u);

    /* 10：过压阈值为0，属于参数错误。 */
    sample = MakeNormalSample();
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, 0, now_ms, max_age_ms),
        0u, 0u, 0u, 0u, 1u);

    /* 11：超时参数为0，属于参数错误。 */
    sample = MakeNormalSample();
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, 0u),
        0u, 0u, 0u, 0u, 1u);

    /* 12：计数回绕后，三节数据年龄都刚好为500 ms。 */
    sample = MakeNormalSample();
    for (i = 0u; i < BMS_CELL_COUNT; i++)
    {
        sample.cell_mv[i].timestamp_ms = UINT32_MAX - 99u;
    }
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, 400u, max_age_ms),
        0u, 0u, 0u, 0x07u, 0u);

    /* 13：全零快照尚未采集，三节都无效。 */
    sample = empty_sample;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0x07u, 0u, 0u);

    /* 14：第1节刚好达到欠压阈值。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].value = 3000;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0x01u, 0u, 0u, 0u);

    /* 15：第2节欠压。 */
    sample = MakeNormalSample();
    sample.cell_mv[1].value = 2900;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0x02u, 0u, 0u, 0u);

    /* 16：第3节欠压。 */
    sample = MakeNormalSample();
    sample.cell_mv[2].value = 2900;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0x04u, 0u, 0u, 0u);

    /* 17：第1、3节同时欠压。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].value = 2900;
    sample.cell_mv[2].value = 2900;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0x05u, 0u, 0u, 0u);

    /* 18：第1节欠压、第2节过压，可以同时记录。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].value = 2900;
    sample.cell_mv[1].value = 4300;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0x02u, 0x01u, 0u, 0u, 0u);

    /* 19：第2节旧值偏低，只报告超时，不报告欠压。 */
    sample = MakeNormalSample();
    sample.cell_mv[1].value = 2900;
    sample.cell_mv[1].timestamp_ms = 999u;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0u, 0x02u, 0u);

    /* 20：比下限高1 mV、比上限低1 mV，都不触发。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].value = 3001;
    sample.cell_mv[1].value = 4199;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0u, 0u, 0u);

    /* 21：有效零电压触发欠压。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].value = 0;
    AssertReport(Bms_CheckPackVoltages(
        &sample, uv_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0x01u, 0u, 0u, 0u);

    /* 22：欠压阈值为0，属于参数错误。 */
    sample = MakeNormalSample();
    AssertReport(Bms_CheckPackVoltages(
        &sample, 0, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0u, 0u, 1u);

    /* 23：欠压阈值为负数，属于参数错误。 */
    sample = MakeNormalSample();
    AssertReport(Bms_CheckPackVoltages(
        &sample, -1, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0u, 0u, 1u);

    /* 24：上下限相等，没有合理的正常区间。 */
    sample = MakeNormalSample();
    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0u, 0u, 1u);

    /* 25：下限高于上限，属于参数错误。 */
    sample = MakeNormalSample();
    AssertReport(Bms_CheckPackVoltages(
        &sample, 4300, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0u, 0u, 1u);

    puts("25 pack voltage scenarios passed.");
    return 0;
}