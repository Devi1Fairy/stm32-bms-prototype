#include "bms_sampling.h"
#include "bms_config.h"
#include "bsp_analog.h"
#include "sample_filter.h"
#include "ina226.h"
#include "i2c.h"
#include <stddef.h>

/* 这些诊断量归采集模块私有，可在Keil中展开观察。 */
static volatile uint32_t bms_adc_raw = 0u;
static volatile uint32_t bms_adc_input_mv = 0u;
static volatile int32_t bms_adc_cell0_mv = 0;
static volatile uint8_t bms_adc_read_ok = 0u;
static volatile uint32_t bms_adc_error_count = 0u;
static volatile uint32_t bms_adc_filter_count = 0u;
static volatile uint32_t bms_adc_filtered_raw = 0u;
static volatile uint32_t bms_adc_filtered_input_mv = 0u;
static volatile uint32_t bms_ntc_raw = 0u;
static volatile uint32_t bms_ntc_input_mv = 0u;
static volatile uint8_t bms_ntc_read_ok = 0u;
static volatile uint32_t bms_ntc_error_count = 0u;
static volatile int32_t bms_ntc_temp_est_dC = 0;
static SampleFilter bms_adc_filter;

static Ina226Sample bms_ina226_sample = {0};
static Ina226Diagnostics bms_ina226_diag = {0};
static Ina226Result bms_ina226_result = INA226_ERROR;

static uint32_t Bms_FilterAdcRaw(uint32_t raw, uint32_t now_ms)
{
    uint32_t average = raw;
    (void)SampleFilter_Update(
        &bms_adc_filter, raw, now_ms, bms_config.voltage_max_age_ms, &average);
    bms_adc_filter_count = bms_adc_filter.count;
    return average;
}

static uint8_t Bms_NtcRawToEstimatedTemperature(
    uint32_t raw, int32_t *temperature_dC)
{
    return Ntc_EstimateTemperature(
        raw, BSP_ADC_MAX_CODE, &bms_ntc_model, temperature_dC);
}


static uint8_t Bms_ReadPotentiometer(void)
{
    uint32_t raw;

    if (Bsp_AnalogRead(BSP_ANALOG_POT, &raw) == 0u)
    {
        return 0u;
    }

    bms_adc_raw = raw;
    bms_adc_input_mv =
        (raw * BSP_ADC_REFERENCE_MV + BSP_ADC_MAX_CODE / 2u) / BSP_ADC_MAX_CODE;
    bms_adc_cell0_mv = (int32_t)(
        BMS_DEMO_CELL0_MIN_MV
        + (raw * BMS_DEMO_CELL0_SPAN_MV + BSP_ADC_MAX_CODE / 2u) / BSP_ADC_MAX_CODE);
    return 1u;
}

static uint8_t Bms_ReadNtcVoltage(void)
{
    uint32_t raw;

    if (Bsp_AnalogRead(BSP_ANALOG_NTC, &raw) == 0u)
    {
        return 0u;
    }

    bms_ntc_raw = raw;
    bms_ntc_input_mv =
        (raw * BSP_ADC_REFERENCE_MV + BSP_ADC_MAX_CODE / 2u) / BSP_ADC_MAX_CODE;

    /* 电源轨附近暂按输入异常处理；不能据此确诊某一种断线。 */
    if ((raw <= bms_ntc_model.rail_margin) || (raw >= BSP_ADC_MAX_CODE - bms_ntc_model.rail_margin))
    {
        return 0u;
    }

    return 1u;
}

static void Bms_UpdateDemoSample(BmsSample *sample, uint32_t now_ms)
{
    uint32_t i;
    int32_t temperature_est_dC = 0;

    /* 从下标1开始，只生成第2、3节的固定模拟电压。 */
    for (i = 1u; i < BMS_CELL_COUNT; ++i)
    {
        sample->cell_mv[i].value = 3800;
        sample->cell_mv[i].timestamp_ms = now_ms;
        sample->cell_mv[i].valid = 1u;
        sample->cell_mv[i].source = BMS_SOURCE_SIMULATED;
    }

    sample->temperature_dC.source = BMS_SOURCE_ADC;
    bms_ntc_read_ok = Bms_ReadNtcVoltage();
    if ((bms_ntc_read_ok == 1u)
        && (Bms_NtcRawToEstimatedTemperature(
                bms_ntc_raw, &temperature_est_dC) == 1u))
    {
        bms_ntc_temp_est_dC = temperature_est_dC;
        sample->temperature_dC.value = temperature_est_dC;
        sample->temperature_dC.timestamp_ms = now_ms;
        sample->temperature_dC.valid = 1u;
    }
    else
    {
        /* 失败不刷新旧值和时间戳，保护模块会看到valid=0。 */
        bms_ntc_read_ok = 0u;
        sample->temperature_dC.valid = 0u;
        bms_ntc_error_count++;
    }

    sample->cell_mv[0].source = BMS_SOURCE_ADC;
    bms_adc_read_ok = Bms_ReadPotentiometer();

    if (bms_adc_read_ok == 1u)
    {
        bms_adc_filtered_raw = Bms_FilterAdcRaw(bms_adc_raw, now_ms);
        bms_adc_filtered_input_mv =
            (bms_adc_filtered_raw * BSP_ADC_REFERENCE_MV + BSP_ADC_MAX_CODE / 2u)
            / BSP_ADC_MAX_CODE;

        /* 保护仍使用本次原始读数映射的电压，避免显示滤波延后触发。 */
        sample->cell_mv[0].value = bms_adc_cell0_mv;
        sample->cell_mv[0].timestamp_ms = now_ms;
        sample->cell_mv[0].valid = 1u;
    }
    else
    {
        /* 失败时标记无效，保留旧值和旧时间，交给保护逻辑处理。 */
        sample->cell_mv[0].valid = 0u;
        bms_adc_error_count++;

        /* 丢弃旧窗口，下次成功采样时从新读数开始计算。 */
        bms_adc_filter.count = 0u;
        bms_adc_filter_count = 0u;
    }
}

static void Bms_UpdateCurrentMeasurement(BmsSample *sample)
{
    sample->current_ma.source = BMS_SOURCE_EXTERNAL;
    bms_ina226_result = Ina226_Read(&bms_ina226_sample);
    Ina226_GetDiagnostics(&bms_ina226_diag);

    if (bms_ina226_result == INA226_NEW_DATA)
    {
        sample->current_ma.value = bms_ina226_sample.current_ma;
        sample->current_ma.timestamp_ms = bms_ina226_sample.timestamp_ms;
        sample->current_ma.valid = 1u;
    }
    else if (bms_ina226_result == INA226_ERROR)
    {
        sample->current_ma.valid = 0u;
    }

    /* NO_DATA保留旧值、旧时间和原valid，由保护检查是否过期。 */
}

uint8_t Bms_SamplingInit(void)
{
    SampleFilter_Init(&bms_adc_filter);
    if (Bsp_AnalogInit() == 0u)
    {
        return 0u;
    }
    /* 初始化失败由后续的无效记录交给保护，保留通信诊断。 */
    (void)Ina226_Init(&hi2c1, BMS_INA226_SHUNT_MOHM);
    Ina226_GetDiagnostics(&bms_ina226_diag);
    return 1u;
}

void Bms_SamplingRead(BmsAcquisition *acquisition, uint32_t now_ms)
{
    if (acquisition == NULL)
    {
        return;
    }

    Bms_UpdateDemoSample(&acquisition->sample, now_ms);
    Bms_UpdateCurrentMeasurement(&acquisition->sample);
    acquisition->ina_result = bms_ina226_result;
    acquisition->bus_mv = bms_ina226_sample.bus_mv;
    /* 读取失败也发布快照，让保护看到无效状态。 */
    acquisition->sample_count++;
}
