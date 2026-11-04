#ifndef BMS_WATCHDOG_H
#define BMS_WATCHDOG_H

#include <stdint.h>

typedef struct
{
    uint32_t start_ms;
    uint32_t startup_grace_ms;

    /* 首次决定停止喂狗时，缺失了哪些任务。 */
    uint32_t fault_mask;

    uint8_t initialized;

    /* 曾经观察到四个任务全部及时报到。 */
    uint8_t tasks_ready;

    /* 锁定停止喂狗，保持到系统复位。 */
    uint8_t tripped;
} BmsWatchdog;

/* 只在启动调度器前初始化，运行中不能用它清除锁定。 */
void Bms_WatchdogInit(BmsWatchdog *watchdog, uint32_t now_ms,
                      uint32_t startup_grace_ms);

/* 返回1允许本次喂狗，返回0必须停止喂狗。 */
uint8_t Bms_WatchdogStep(BmsWatchdog *watchdog,
                        uint32_t missing_mask, uint32_t now_ms);

#endif
