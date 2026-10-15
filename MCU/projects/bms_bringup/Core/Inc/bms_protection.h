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

/*
 * 四个位图分别记录过压、欠压、无效和超时位置。
 * bit0、bit1、bit2 对应第1、2、3节；uint8_t 最多表示8节。
 */
typedef struct
{
    uint8_t over_voltage_mask;  /* 哪些单体达到过压阈值 */
    uint8_t under_voltage_mask; /* 哪些单体达到欠压阈值 */
    uint8_t invalid_mask;       /* 哪些单体的数据无效 */
    uint8_t timeout_mask;       /* 哪些单体的数据超时 */
    uint8_t input_error;        /* 1：整组指针或参数错误 */
} BmsVoltageReport;

/*
 * 检查各节电池的电压上下限和数据时效。
 * 所有电压参数单位为 mV，时间参数单位为 ms。
 * 参数必须满足：0 < uv_threshold_mv < ov_threshold_mv。
 * 返回本次检查结果，不记录历史故障，也不直接控制负载。
 */
BmsVoltageReport Bms_CheckPackVoltages(
    const BmsSample *sample,
    int32_t uv_threshold_mv,
    int32_t ov_threshold_mv,
    uint32_t now_ms,
    uint32_t max_age_ms);

/* 单节电池的欠压检查结果，不代表整组电池的完整状态。 */
typedef enum
{
    BMS_UV_DATA_INVALID = 0, /* 数据或阈值无效，当前无法判断 */
    BMS_UV_NOT_TRIGGERED,   /* 没有触发本项欠压 */
    BMS_UV_TRIGGERED        /* 已触发欠压 */
} BmsUvResult;

/*
 * 检查一节电池是否欠压。
 * cell：待检查的单体测量数据，只读取，不修改。
 * threshold_mv：欠压阈值，单位 mV，必须大于 0。
 * 本函数检查数值和基本有效性；采样超时仍由时效函数检查。
 */
BmsUvResult Bms_CheckCellUnderVoltage(
    const BmsMeasurement *cell,
    int32_t threshold_mv);

#endif
