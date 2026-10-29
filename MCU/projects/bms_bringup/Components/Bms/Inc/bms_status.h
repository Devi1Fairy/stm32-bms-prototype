#ifndef BMS_STATUS_H
#define BMS_STATUS_H

#include "bms_state_machine.h"

/* 对外报告只包含结果，不暴露控制器内部的恢复计时等状态。 */
typedef struct
{
    BmsState state;
    uint32_t active_faults;
    uint32_t latched_faults;
    uint8_t load_enabled;
    uint8_t clear_allowed;
} BmsStatus;

#endif
