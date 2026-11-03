#include "bms_gateway_protocol.h"

#include <limits>

bool GatewayIdValid(quint64 id)
{
    return id >= 1 && id <= std::numeric_limits<quint32>::max();
}

bool ParseGatewayMessage(const QByteArray &line, GatewayMessage &result)
{
    GatewayMessage next;

    // 必须完全匹配，前后多空格或带CR都不能授予控制能力。
    if (line == "GW 2 CONTROL")
    {
        next.type = GatewayMessageType::Capability;
        result = next;
        return true;
    }

    if (line.size() > 64)
        return false;

    const auto parts = line.split(' ');
    if (parts.size() != 3 || parts[1].isEmpty())
        return false;

    // 编号中只允许十进制数字。
    for (char c : parts[1])
    {
        if (c < '0' || c > '9')
            return false;
    }

    bool ok = false;
    const quint64 id = parts[1].toULongLong(&ok, 10);
    if (!ok || id > std::numeric_limits<quint32>::max())
        return false;

    if (parts[0] == "ACK" && id != 0
        && parts[2] == "UART_ACCEPTED")
    {
        next.type = GatewayMessageType::UartAccepted;
    }
    else if (parts[0] == "ERR")
    {
        static const QList<QByteArray> reasons = {
            "BAD_REQUEST", "BAD_COMMAND", "STALE_ID", "QUEUE_FULL",
            "CANCELLED", "EXPIRED", "NOT_READY", "SERIAL_WRITE"
        };

        if (!reasons.contains(parts[2]))
            return false;

        next.type = GatewayMessageType::Error;
        next.reason = parts[2];
    }
    else
    {
        return false;
    }

    next.id = static_cast<quint32>(id);
    result = next;
    return true;
}

QByteArray EncodeGatewayCommand(quint64 id, BmsCommand command)
{
    if (!GatewayIdValid(id))
        return {};

    QByteArray action;

    switch (command)
    {
    case BmsCommand::Stop:  action = "STOP";  break;
    case BmsCommand::Start: action = "START"; break;
    case BmsCommand::Clear: action = "CLEAR"; break;
    default: return {};
    }

    // 只追加一个LF，不追加CR或NUL。
    return "CMD " + QByteArray::number(id) + ' ' + action + '\n';
}