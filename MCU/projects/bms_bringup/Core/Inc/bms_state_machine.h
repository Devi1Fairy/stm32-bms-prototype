#ifndef BMS_STATE_MACHINE_H
#define BMS_STATE_MACHINE_H

#include "bms_protection.h"

/* 当前控制阶段；上电初始化后先进入自检。 */
typedef enum
{
    BMS_STATE_SELF_TEST = 0, /* 等待自检通过，负载关闭 */
    BMS_STATE_STANDBY,       /* 已准备好，等待启动指令 */
    BMS_STATE_RUN,           /* 允许运行 */
    BMS_STATE_FAULT_LOCK     /* 故障锁定，等待满足条件后清除 */
} BmsState;

/* 每次处理一个指令；没有新指令时填写 NONE。 */
typedef enum
{
    BMS_CMD_NONE = 0,
    BMS_CMD_START,
    BMS_CMD_STOP,
    BMS_CMD_CLEAR
} BmsCommand;

/* 状态机保存的运行记录，需要在多次调用之间保留。 */
typedef struct
{
    BmsState state;
    uint32_t latched_faults; /* 锁定期间累计出现过的故障 */
    uint8_t load_enabled;   /* 1：软件允许负载工作；尚未操作 GPIO */
} BmsStateMachine;

/* 本轮输入；测量检查和恢复条件检查由外部提供。 */
typedef struct
{
    uint32_t active_faults;   /* 本轮 Bms_CombineFaults() 的结果 */
    BmsCommand command;      /* 本轮新收到的指令 */
    uint8_t self_test_passed; /* 1：自检已通过；0：尚未通过 */
    uint8_t clear_allowed;    /* 1：恢复条件已满足；0：禁止清除 */
} BmsControlInput;

/* 上电时调用一次，或测试中模拟复位时调用。 */
void Bms_StateMachineInit(BmsStateMachine *machine);

/*
 * 周期调用，即使没有指令也要检查故障。
 * 本函数只更新状态和输出许可，不操作真实负载。
 */
void Bms_StateMachineStep(
    BmsStateMachine *machine,
    const BmsControlInput *input);

#endif
