#include "ntc.h"
#include <stddef.h>
#include <math.h>

uint8_t Ntc_EstimateTemperature(
    uint32_t raw, uint32_t adc_max_code,
    const NtcModel *model, int32_t *temperature_dC)
{
    float resistance_ohm;
    float inverse_kelvin;
    float temperature_c;
    float scaled;

    if ((model == NULL) || (temperature_dC == NULL)
        || !(model->pullup_ohm > 0.0f)
        || !(model->r25_ohm > 0.0f)
        || !(model->beta_k > 0.0f)
        || !(model->min_temperature_c <= model->max_temperature_c)
        || (adc_max_code <= model->rail_margin))
    {
        return 0u;
    }

    /* 两轨附近不换算，避免除零、log(0)和明显异常输入。 */
    if ((raw <= model->rail_margin)
        || (raw >= adc_max_code - model->rail_margin))
    {
        return 0u;
    }

    resistance_ohm =
        model->pullup_ohm * (float)raw / (float)(adc_max_code - raw);
    inverse_kelvin =
        1.0f / 298.15f + logf(resistance_ohm / model->r25_ohm) / model->beta_k;
    if (!(inverse_kelvin > 0.0f))
    {
        return 0u;
    }

    temperature_c = 1.0f / inverse_kelvin - 273.15f;
    if (!(temperature_c >= model->min_temperature_c
        && temperature_c <= model->max_temperature_c))
    {
        return 0u;
    }

    scaled = temperature_c * 10.0f;
    *temperature_dC =
        (int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
    return 1u;
}
