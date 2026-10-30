#ifndef BMS_COMMANDS_H
#define BMS_COMMANDS_H

#include "bms_log_parser.h"

// 枚举值直接对应UART字符。
enum class BmsCommand : char
{
    None = '\0',
    Start = 'S',
    Stop = 'T',
    Clear = 'C'
};

bool BmsCommandAllowed(BmsCommand command, bool connected, bool fresh,
                       bool pending, const BmsLogRecord &record);

bool BmsCommandTargetObserved(BmsCommand command,
                              const BmsLogRecord &record);

#endif // BMS_COMMANDS_H