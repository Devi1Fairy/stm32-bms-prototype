#ifndef NTC_H
#define NTC_H

#include <stdint.h>

typedef struct
{
    float pullup_ohm;
    float r25_ohm;
    float beta_k;
    float min_temperature_c;
    float max_temperature_c;
    uint32_t rail_margin;
} NtcModel;

/* 输入为分压ADC原始码，输出为0.1℃；失败不修改输出。 */
uint8_t Ntc_EstimateTemperature(
    uint32_t raw, uint32_t adc_max_code,
    const NtcModel *model, int32_t *temperature_dC);

#endif
