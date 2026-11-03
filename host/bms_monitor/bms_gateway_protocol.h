#ifndef BMS_GATEWAY_PROTOCOL_H
#define BMS_GATEWAY_PROTOCOL_H

#include "bms_commands.h"

// 网关消息分为：能力声明、串口接收回执、错误回执。
enum class GatewayMessageType
{
    Unknown,
    Capability,
    UartAccepted,
    Error
};

struct GatewayMessage
{
    GatewayMessageType type = GatewayMessageType::Unknown;
    quint32 id = 0;
    QByteArray reason;
};

// line是不包含LF的原始正文，不能提前trimmed。
bool ParseGatewayMessage(const QByteArray &line, GatewayMessage &result);

bool GatewayIdValid(quint64 id);

QByteArray EncodeGatewayCommand(quint64 id, BmsCommand command);

#endif