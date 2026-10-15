#ifndef BMS_PROTECTION_H
#define BMS_PROTECTION_H

#include "bms_types.h"

/* 这里只描述过压检查结果，不代表电池整体是否正常。 */
typedef enum
{
    BMS_OV_DATA_INVALID = 0, /* 数据无效，当前无法判断 */
    BMS_OV_NOT_TRIGGERED,   /* 没有触发本项过压 */
    BMS_OV_TRIGGERED        /* 已触发过压 */
} BmsOvResult;

/*
 * 检查一节电池是否过压。
 * cell：该节电池的测量数据。
 * threshold_mv：过压阈值，单位为 mV。
 * const 表示函数只读取测量数据，不修改它。
 */
BmsOvResult Bms_CheckCellOverVoltage(
    const BmsMeasurement *cell,
    int32_t threshold_mv);

/* 一路测量数据的时效检查结果；未超时不代表电压、电流等数值正常。 */
typedef enum
{
    BMS_SAMPLE_INVALID = 0, /* 数据标志、来源或超时参数不符合要求 */
    BMS_SAMPLE_FRESH,       /* 数据尚未达到超时时间 */
    BMS_SAMPLE_TIMEOUT      /* 数据已经达到或超过超时时间 */
} BmsSampleStatus;

/*
 * 检查一路测量数据是否长时间没有更新。
 * measurement：待检查的测量数据，只读取，不修改。
 * now_ms：当前毫秒计数，必须与 timestamp_ms 使用同一个计时来源。
 * max_age_ms：允许的最大数据年龄，必须大于 0；达到该值就算超时。
 */
BmsSampleStatus Bms_CheckSampleFreshness(
    const BmsMeasurement *measurement,
    uint32_t now_ms,
    uint32_t max_age_ms);

#endif
