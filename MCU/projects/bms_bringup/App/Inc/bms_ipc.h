#ifndef BMS_IPC_H
#define BMS_IPC_H

#include "bms_app_types.h"

/* 只在创建任务前调用一次；头文件不暴露FreeRTOS句柄。 */
uint8_t Bms_IpcInit(void);

/* 发布/读取快照是任务接口，不能从中断调用。 */
uint8_t Bms_IpcPublishAcquisition(const BmsAcquisition *acquisition);
uint8_t Bms_IpcPeekAcquisition(BmsAcquisition *acquisition);
uint8_t Bms_IpcPublishReport(const BmsTelemetry *report);
uint8_t Bms_IpcWaitReport(BmsTelemetry *report);
uint8_t Bms_IpcPeekReport(BmsTelemetry *report);

/* 投递会自动区分任务和ISR；普通队列满时请求STOP。 */
void Bms_IpcPostCommand(BmsCommand command);
BmsCommand Bms_IpcTakeCommand(void);

uint8_t Bms_IpcSamplingEnabled(void);
void Bms_IpcSetSamplingEnabled(uint8_t enabled);
uint32_t Bms_IpcSampleDelayMs(void);
uint32_t Bms_IpcCommDelayMs(void);
void Bms_IpcToggleSampleDelay(void);
void Bms_IpcToggleCommDelay(void);

#endif
