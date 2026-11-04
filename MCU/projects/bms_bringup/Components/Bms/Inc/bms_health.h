#ifndef BMS_HEALTH_H
#define BMS_HEALTH_H

#include <stdint.h>

typedef enum
{
    BMS_HEALTH_CONTROL = 0,
    BMS_HEALTH_SAMPLE,
    BMS_HEALTH_COMM,
    BMS_HEALTH_LED
} BmsHealthTask;

#define BMS_HEALTH_TASK_COUNT 4u
#define BMS_HEALTH_ALL_MASK   0x0Fu

typedef struct
{
    uint32_t last_ms[BMS_HEALTH_TASK_COUNT];

    /* 每一位表示对应任务至少报到过一次。 */
    uint32_t seen_mask;
} BmsTaskHealth;

/* 对象先清零。多任务共享时，由调用者用临界区保护。 */
void Bms_HealthMark(BmsTaskHealth *health, BmsHealthTask task,
                    uint32_t now_ms);

/* 返回未报到或已经超时的任务位图；0表示本次检查全部及时。 */
uint32_t Bms_HealthMissing(const BmsTaskHealth *health,
                           uint32_t now_ms, uint32_t timeout_ms);

#endif
