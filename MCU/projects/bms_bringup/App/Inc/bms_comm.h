#ifndef BMS_COMM_H
#define BMS_COMM_H

#include "bms_app_types.h"

uint8_t Bms_CommInit(void);
void Bms_CommRecoverRx(void);
/* 返回0只表示日志格式等内部错误；普通收发失败按原策略计数。 */
uint8_t Bms_CommPoll(const BmsTelemetry *report, uint32_t now_ms);

#endif
