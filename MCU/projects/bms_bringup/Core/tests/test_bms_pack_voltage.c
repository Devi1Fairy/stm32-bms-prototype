#include <assert.h>
#include <stdio.h>
#include "bms_protection.h"

/*
 * 构造一份正常的模拟数据。
 * 每个测试场景重新构造，避免受到上一场景的影响。
 */
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

/*
 * 核对一份汇总结果的全部字段。
 * 即使本次只测试过压，也要确认没有意外报告无效或超时。
 */
static void AssertReport(
    BmsVoltageReport report,
    uint8_t expected_ov,
    uint8_t expected_invalid,
    uint8_t expected_timeout,
    uint8_t expected_input_error)
{
    assert(report.over_voltage_mask == expected_ov);
    assert(report.invalid_mask == expected_invalid);
    assert(report.timeout_mask == expected_timeout);
    assert(report.input_error == expected_input_error);
}

int main(void)
{
    BmsSample sample;

    /* 模拟上电后尚未取得采样的快照。 */
    BmsSample empty_sample = {0};

    uint32_t i;

    /* 以下仅为软件测试参数，实物参数尚未确定。 */
    const int32_t ov_threshold_mv = 4200;
    const uint32_t now_ms = 1499u;
    const uint32_t max_age_ms = 500u;

    /* 1：三节数据均正常，四个结果字段都应为0。 */
    sample = MakeNormalSample();
    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0u, 0u);

    /* 2：第1节刚好达到过压阈值，对应bit0。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].value = 4200;
    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, now_ms, max_age_ms),
        0x01u, 0u, 0u, 0u);

    /* 3：第2节过压，对应bit1。 */
    sample = MakeNormalSample();
    sample.cell_mv[1].value = 4300;
    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, now_ms, max_age_ms),
        0x02u, 0u, 0u, 0u);

    /* 4：第3节过压，对应bit2。 */
    sample = MakeNormalSample();
    sample.cell_mv[2].value = 4300;
    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, now_ms, max_age_ms),
        0x04u, 0u, 0u, 0u);

    /* 5：第1、3节同时过压，位图应为101，即0x05。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].value = 4300;
    sample.cell_mv[2].value = 4300;
    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, now_ms, max_age_ms),
        0x05u, 0u, 0u, 0u);

    /* 6：三节分别出现无效标志、无来源、负电压。 */
    sample = MakeNormalSample();
    sample.cell_mv[0].valid = 0u;
    sample.cell_mv[1].source = BMS_SOURCE_NONE;
    sample.cell_mv[2].value = -1;
    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0x07u, 0u, 0u);

    /*
     * 7：不同单体同时出现三类问题。
     * 第1节过压，第2节无效，第3节刚好超时。
     */
    sample = MakeNormalSample();
    sample.cell_mv[0].value = 4300;
    sample.cell_mv[1].valid = 0u;
    sample.cell_mv[2].timestamp_ms = 999u;
    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, now_ms, max_age_ms),
        0x01u, 0x02u, 0x04u, 0u);

    /* 8：第2节旧值虽然高于阈值，本次只报告它超时。 */
    sample = MakeNormalSample();
    sample.cell_mv[1].value = 4300;
    sample.cell_mv[1].timestamp_ms = 999u;
    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0x02u, 0u);

    /* 9：没有传入整组采样对象，应报告输入错误。 */
    AssertReport(Bms_CheckPackVoltages(
        NULL, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0u, 0u, 1u);

    /* 10：过压阈值为0，应报告输入错误。 */
    sample = MakeNormalSample();
    AssertReport(Bms_CheckPackVoltages(
        &sample, 0, now_ms, max_age_ms),
        0u, 0u, 0u, 1u);

    /* 11：允许的数据年龄为0，应报告输入错误。 */
    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, now_ms, 0u),
        0u, 0u, 0u, 1u);

    /*
     * 12：毫秒计数回绕。
     * 从UINT32_MAX - 99到回绕后的400，共经过500 ms。
     * 三节都刚好超时，因此超时位图应为111，即0x07。
     */
    sample = MakeNormalSample();
    for (i = 0u; i < BMS_CELL_COUNT; i++)
    {
        sample.cell_mv[i].timestamp_ms = UINT32_MAX - 99u;
    }

    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, 400u, max_age_ms),
        0u, 0u, 0x07u, 0u);

    /* 13：全零快照代表尚未采集，三节数据都应报告无效。 */
    sample = empty_sample;
    AssertReport(Bms_CheckPackVoltages(
        &sample, ov_threshold_mv, now_ms, max_age_ms),
        0u, 0x07u, 0u, 0u);

    puts("13 pack voltage scenarios passed.");
    return 0;
}