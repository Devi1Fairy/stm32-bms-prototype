#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "bms_state_machine.h"

/* 每个场景同时检查状态、锁定故障和负载许可。 */
static void CheckState(
    const BmsStateMachine *machine,
    BmsState expected_state,
    uint32_t expected_faults,
    uint8_t expected_load)
{
    assert(machine->state == expected_state);
    assert(machine->latched_faults == expected_faults);
    assert(machine->load_enabled == expected_load);
}

int main(void)
{
    BmsStateMachine machine;
    BmsControlInput input = {0};

    /* 1：模拟上电，先自检，负载关闭。 */
    Bms_StateMachineInit(&machine);
    CheckState(&machine, BMS_STATE_SELF_TEST, BMS_FAULT_NONE, 0u);

    /* 2：没有故障但自检尚未通过，继续等待。 */
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_SELF_TEST, BMS_FAULT_NONE, 0u);

    /* 3：自检未通过时收到启动指令，也不能运行。 */
    input.command = BMS_CMD_START;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_SELF_TEST, BMS_FAULT_NONE, 0u);

    /* 4：自检通过；同一轮的 START 不直接启动负载。 */
    input.self_test_passed = 1u;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_STANDBY, BMS_FAULT_NONE, 0u);

    /* 5：在待机时另发一次 START，进入运行。 */
    input.command = BMS_CMD_START;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_RUN, BMS_FAULT_NONE, 1u);

    /* 6：无新指令、无故障，保持运行。 */
    input.command = BMS_CMD_NONE;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_RUN, BMS_FAULT_NONE, 1u);

    /* 7：停止后回待机，关闭负载许可。 */
    input.command = BMS_CMD_STOP;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_STANDBY, BMS_FAULT_NONE, 0u);

    /* 8：待机收到 CLEAR 不会启动。 */
    input.command = BMS_CMD_CLEAR;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_STANDBY, BMS_FAULT_NONE, 0u);

    /* 9：启动指令与过压同时出现，故障优先。 */
    input.command = BMS_CMD_START;
    input.active_faults = BMS_FAULT_CELL_OV;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_FAULT_LOCK, BMS_FAULT_CELL_OV, 0u);

    /* 10：过压消失，但 START 不能解除锁定。 */
    input.active_faults = BMS_FAULT_NONE;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_FAULT_LOCK, BMS_FAULT_CELL_OV, 0u);

    /* 11：STOP 也不能解除锁定。 */
    input.command = BMS_CMD_STOP;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_FAULT_LOCK, BMS_FAULT_CELL_OV, 0u);

    /* 12：收到 CLEAR，但恢复条件未满足，保持锁定。 */
    input.command = BMS_CMD_CLEAR;
    input.clear_allowed = 0u;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_FAULT_LOCK, BMS_FAULT_CELL_OV, 0u);

    /* 13：锁定期间又出现过温，累计记录；故障优先于 CLEAR。 */
    input.clear_allowed = 1u;
    input.active_faults = BMS_FAULT_OVER_TEMP;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_CELL_OV | BMS_FAULT_OVER_TEMP, 0u);

    /* 14：即使允许清除，自检未通过仍不能解除锁定。 */
    input.active_faults = BMS_FAULT_NONE;
    input.self_test_passed = 0u;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_CELL_OV | BMS_FAULT_OVER_TEMP, 0u);

    /* 15：满足条件并收到 CLEAR，只回待机。 */
    input.self_test_passed = 1u;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_STANDBY, BMS_FAULT_NONE, 0u);

    /* 16：清除后没有新指令，不会自动运行。 */
    input.command = BMS_CMD_NONE;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_STANDBY, BMS_FAULT_NONE, 0u);

    /* 17：重新发送 START，才允许运行。 */
    input.command = BMS_CMD_START;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_RUN, BMS_FAULT_NONE, 1u);

    /* 18：运行中数据无效和超时，故障优先于 STOP。 */
    input.command = BMS_CMD_STOP;
    input.active_faults = BMS_FAULT_DATA_INVALID | BMS_FAULT_SAMPLE_TIMEOUT;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_FAULT_LOCK,
        BMS_FAULT_DATA_INVALID | BMS_FAULT_SAMPLE_TIMEOUT, 0u);

    /* 19：数据恢复、允许清除，CLEAR 返回待机。 */
    input.active_faults = BMS_FAULT_NONE;
    input.command = BMS_CMD_CLEAR;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_STANDBY, BMS_FAULT_NONE, 0u);

    /* 20：再次启动，为测试运行中输入错误做准备。 */
    input.command = BMS_CMD_START;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_RUN, BMS_FAULT_NONE, 1u);

    /* 21：输入指针为空，必须撤销负载许可并锁定。 */
    Bms_StateMachineStep(&machine, NULL);
    CheckState(&machine, BMS_STATE_FAULT_LOCK, BMS_FAULT_INPUT_ERROR, 0u);

    /* 22：异常状态值也必须进入锁定。 */
    Bms_StateMachineInit(&machine);
    machine.state = (BmsState)99;
    input.command = BMS_CMD_NONE;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_FAULT_LOCK, BMS_FAULT_INPUT_ERROR, 0u);

    /* 23：模拟复位，重新自检，不恢复原先运行状态。 */
    Bms_StateMachineInit(&machine);
    CheckState(&machine, BMS_STATE_SELF_TEST, BMS_FAULT_NONE, 0u);

    /* 24：自检阶段发生数据故障，也要锁定。 */
    input.self_test_passed = 0u;
    input.active_faults = BMS_FAULT_DATA_INVALID;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_FAULT_LOCK, BMS_FAULT_DATA_INVALID, 0u);

    /* 25：故障消失，但不能用 CLEAR 绕过尚未通过的自检。 */
    input.active_faults = BMS_FAULT_NONE;
    input.command = BMS_CMD_CLEAR;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_FAULT_LOCK, BMS_FAULT_DATA_INVALID, 0u);

    /* 26：自检通过后，允许清除并返回待机。 */
    input.self_test_passed = 1u;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_STANDBY, BMS_FAULT_NONE, 0u);

    /* 27：待机时若自检通过标志为0，也拒绝启动。 */
    input.self_test_passed = 0u;
    input.command = BMS_CMD_START;
    Bms_StateMachineStep(&machine, &input);
    CheckState(&machine, BMS_STATE_STANDBY, BMS_FAULT_NONE, 0u);

    puts("27 state machine scenarios passed.");
    return 0;
}