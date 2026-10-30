#ifndef BMS_LOG_PARSER_H
#define BMS_LOG_PARSER_H

#include <QByteArray>
#include <QList>
#include <QtGlobal>

// PC端先用64位整数接收，再检查是否符合MCU字段的范围。
struct BmsLogRecord
{
    qint64 state = 0;
    qint64 load = 0;
    qint64 activeFaults = 0;
    qint64 latchedFaults = 0;
    qint64 cellMv = 0;
    qint64 temperatureDc = 0;
    qint64 currentMa = 0;
    qint64 currentValid = 0;
    qint64 currentAgeMs = 0;
    qint64 currentSource = 0;
};

// 整行检查通过才更新result，失败时保留原对象。
bool ParseBmsLog(const QByteArray &line, BmsLogRecord &result);

// 将串口收到的数据块整理成完整行。
class BmsLineBuffer
{
public:
    QList<QByteArray> append(const QByteArray &data);
    void reset();

private:
    QByteArray m_pending;
    bool m_discarding = false;
};

#endif // BMS_LOG_PARSER_H