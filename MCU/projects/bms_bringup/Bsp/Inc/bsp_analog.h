#ifndef BSP_ANALOG_H
#define BSP_ANALOG_H

#include <stdint.h>

#define BSP_ADC_MAX_CODE     4095u
#define BSP_ADC_REFERENCE_MV 3300u

typedef enum
{
    BSP_ANALOG_POT = 0,
    BSP_ANALOG_NTC
} BspAnalogInput;

/* ADC1由采集任务独占，通道与采样时间留在板级实现中。 */
uint8_t Bsp_AnalogInit(void);
uint8_t Bsp_AnalogRead(BspAnalogInput input, uint32_t *raw_out);

#endif
