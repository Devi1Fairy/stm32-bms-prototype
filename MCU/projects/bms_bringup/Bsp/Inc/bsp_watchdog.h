#ifndef BSP_WATCHDOG_H
#define BSP_WATCHDOG_H

#include <stdint.h>

/* 在main入口保存复位标志，并设置调试暂停时冻结计数。 */
void Bsp_WatchdogCaptureReset(void);

/* UART初始化后、调度器启动前打印一次。 */
void Bsp_WatchdogLogReset(void);

uint8_t Bsp_WatchdogRefresh(void);

#endif
