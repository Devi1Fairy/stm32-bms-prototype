#include "bsp_analog.h"
#include "adc.h"
#include <stddef.h>

uint8_t Bsp_AnalogInit(void)
{
    return HAL_ADCEx_Calibration_Start(&hadc1) == HAL_OK;
}

uint8_t Bsp_AnalogRead(BspAnalogInput input, uint32_t *raw_out)
{
    ADC_ChannelConfTypeDef config = {0};
    uint32_t raw;

    if (raw_out == NULL)
    {
        return 0u;
    }

    switch (input)
    {
        case BSP_ANALOG_POT:
            config.Channel = ADC_CHANNEL_10;
            break;
        case BSP_ANALOG_NTC:
            config.Channel = ADC_CHANNEL_11;
            break;
        default:
            return 0u;
    }

    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK)
    {
        return 0u;
    }
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        (void)HAL_ADC_Stop(&hadc1);
        return 0u;
    }

    /* 沿用2ms等待上限，失败也尝试停止ADC。 */
    if (HAL_ADC_PollForConversion(&hadc1, 2u) != HAL_OK)
    {
        (void)HAL_ADC_Stop(&hadc1);
        return 0u;
    }
    raw = HAL_ADC_GetValue(&hadc1);
    if ((HAL_ADC_Stop(&hadc1) != HAL_OK) || (raw > BSP_ADC_MAX_CODE))
    {
        return 0u;
    }

    *raw_out = raw;
    return 1u;
}
