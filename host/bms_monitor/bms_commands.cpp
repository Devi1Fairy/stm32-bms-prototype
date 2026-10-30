#include "bms_commands.h"

bool BmsCommandAllowed(BmsCommand command, bool connected, bool fresh,
                       bool pending, const BmsLogRecord &record)
{
    if (!connected)
    {
        return false;
    }

    // 停止不等待新报文，也不被其他待确认命令挡住。
    if (command == BmsCommand::Stop)
    {
        return true;
    }

    if (!fresh || pending)
    {
        return false;
    }

    if (command == BmsCommand::Start)
    {
        return record.state == 1 && record.load == 0
               && record.activeFaults == 0;
    }

    if (command == BmsCommand::Clear)
    {
        return record.state == 3 && record.load == 0
               && record.activeFaults == 0
               && record.clearAllowed == 1;
    }

    return false;
}

bool BmsCommandTargetObserved(BmsCommand command,
                              const BmsLogRecord &record)
{
    if (command == BmsCommand::Start)
    {
        return record.state == 2 && record.load == 1
               && record.activeFaults == 0;
    }

    if (command == BmsCommand::Stop)
    {
        // 故障锁定时也可能已经关闭负载许可。
        return record.load == 0;
    }

    if (command == BmsCommand::Clear)
    {
        return record.state == 1 && record.load == 0
               && record.activeFaults == 0
               && record.latchedFaults == 0;
    }

    return false;
}