#ifndef BSP_OUTPUT_H
#define BSP_OUTPUT_H

#include <stdint.h>

/* 输入是业务结果，板级函数负责PB10高有效、LED低有效的细节。 */
void Bsp_OutputApply(uint8_t load_enabled, uint8_t fault_locked);
void Bsp_OutputToggleHeartbeat(void);
void Bsp_OutputForceOff(void);

#endif
