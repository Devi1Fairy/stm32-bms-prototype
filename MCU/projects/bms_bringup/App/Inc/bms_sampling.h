#ifndef BMS_SAMPLING_H
#define BMS_SAMPLING_H

#include "bms_app_types.h"

/* 两个接口都只由采集任务调用，外设不需要跨任务加锁。 */
uint8_t Bms_SamplingInit(void);
void Bms_SamplingRead(BmsAcquisition *acquisition, uint32_t now_ms);

#endif
