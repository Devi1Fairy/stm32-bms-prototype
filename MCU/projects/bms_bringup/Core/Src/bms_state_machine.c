#include <stddef.h>
#include "bms_state_machine.h"

void Bms_StateMachineInit(BmsStateMachine *machine)
{
    if (machine == NULL)
    {
        return;
    }

    machine->state = BMS_STATE_SELF_TEST;
    machine->latched_faults = BMS_FAULT_NONE;
    machine->load_enabled = 0u;
}

void Bms_StateMachineStep(
    BmsStateMachine *machine,
    const BmsControlInput *input)
{
    if (machine == NULL)
    {
        return;
    }

    /* 先关闭软件许可，只有最后确认处于 RUN 才重新置为1。 */
    machine->load_enabled = 0u;

    if (input == NULL)
    {
        machine->state = BMS_STATE_FAULT_LOCK;
        machine->latched_faults |= BMS_FAULT_INPUT_ERROR;
        return;
    }

    /* 故障优先于所有指令，包括 STOP 和 CLEAR。 */
    if (input->active_faults != BMS_FAULT_NONE)
    {
        machine->state = BMS_STATE_FAULT_LOCK;

        /* 累计故障，不覆盖锁定期间已经记录的其他故障。 */
        machine->latched_faults |= input->active_faults;
        return;
    }

    switch (machine->state)
    {
        case BMS_STATE_SELF_TEST:
            if (input->self_test_passed == 1u)
            {
                /* 本轮最多转到待机，不继续执行同一轮的 START。 */
                machine->state = BMS_STATE_STANDBY;
            }
            break;

        case BMS_STATE_STANDBY:
            if (input->command == BMS_CMD_START &&
                input->self_test_passed == 1u)
            {
                machine->state = BMS_STATE_RUN;
            }
            break;

        case BMS_STATE_RUN:
            if (input->command == BMS_CMD_STOP)
            {
                machine->state = BMS_STATE_STANDBY;
            }
            break;

        case BMS_STATE_FAULT_LOCK:
            /*
             * 到这里说明本轮没有活动故障。
             * 仍须收到 CLEAR、自检通过、恢复条件满足，才能解除锁定。
             */
            if (input->command == BMS_CMD_CLEAR &&
                input->self_test_passed == 1u &&
                input->clear_allowed == 1u)
            {
                machine->latched_faults = BMS_FAULT_NONE;
                machine->state = BMS_STATE_STANDBY;
            }
            break;

        default:
            /* 状态值不属于已定义的四种状态，关闭许可并锁定。 */
            machine->state = BMS_STATE_FAULT_LOCK;
            machine->latched_faults |= BMS_FAULT_INPUT_ERROR;
            break;
    }

    /* 输出许可由最终状态统一决定。 */
    if (machine->state == BMS_STATE_RUN)
    {
        machine->load_enabled = 1u;
    }
}
