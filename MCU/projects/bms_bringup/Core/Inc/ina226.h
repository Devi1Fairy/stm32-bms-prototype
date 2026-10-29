#ifndef INA226_H
#define INA226_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 三种结果必须区分，无新数据不代表读取失败。 */
typedef enum
{
    INA226_ERROR = -1,
    INA226_NO_DATA = 0,
    INA226_NEW_DATA = 1
} Ina226Result;

/* 只有完整读到新转换时，才更新这三个字段。 */
typedef struct
{
    int32_t current_ma;
    uint32_t bus_mv;
    uint32_t timestamp_ms;
} Ina226Sample;

/* 对外复制的诊断信息，不作为可修改的设备配置。 */
typedef struct
{
    uint8_t found;
    uint8_t address7;
    uint8_t ack_count;
    uint8_t configured;
    uint16_t manufacturer;
    uint16_t die_id;
    int32_t shunt_raw;
    uint32_t read_count;
    uint32_t error_count;
} Ina226Diagnostics;

/* 单器件驱动；在采集任务启动时调用一次。分流电阻单位为毫欧。 */
uint8_t Ina226_Init(I2C_HandleTypeDef *i2c, uint16_t shunt_mohm);

/* 只能由采集任务调用；NO_DATA和ERROR都不改动sample里的旧值/时间。 */
Ina226Result Ina226_Read(Ina226Sample *sample);

/* 在同一采集任务里复制诊断，其他任务继续通过原有快照获取数据。 */
void Ina226_GetDiagnostics(Ina226Diagnostics *diagnostics);

#ifdef __cplusplus
}
#endif

#endif /* INA226_H */
