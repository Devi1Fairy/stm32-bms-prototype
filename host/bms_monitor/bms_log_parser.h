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
    qint64 clearAllowed = 0;
    // 只有bus_mv、bus_v、bus_age完整存在时才为true。
    bool busFieldsPresent = false;
    qint64 busMv = 0;
    qint64 busValid = 0;
    qint64 busAgeMs = 0;
};

// 整行检查通过才更新result，失败时保留原对象。
bool ParseBmsLog(const QByteArray &line, BmsLogRecord &result);

// 与当前MCU母线采样时效门限保持一致，单位ms。
constexpr qint64 BmsBusMaxAgeMs = 500;

bool BmsBusUsable(const BmsLogRecord &record);

// 对应MCU发来的STAT报文。
// 累计量使用无符号64位整数，避免长期运行后超出32位范围。
struct BmsStatRecord
{
    quint64 qOutUah = 0;   // 累计放出电量，单位：微安时
    quint64 qInUah = 0;    // 累计充入电量，单位：微安时
    quint64 eOutUwh = 0;   // 累计放出能量，单位：微瓦时
    quint64 eInUwh = 0;    // 累计充入能量，单位：微瓦时
    quint64 coveredMs = 0; // 实际参与积分的时间，单位：毫秒
    quint32 gaps = 0;      // 积分连续性中断次数
    bool overflow = false;
};

// MCU每秒发送一次STAT，允许一定的通信延迟。
constexpr qint64 BmsStatTimeoutMs = 2500;

// 整条报文合法时才更新result。
bool ParseBmsStatLog(const QByteArray &line, BmsStatRecord &result);

// 将串口收到的数据块整理成完整行。
class BmsLineBuffer
{
public:
    QList<QByteArray> append(const QByteArray &data, bool trimLines = true);
    void reset();

private:
    QByteArray m_pending;
    bool m_discarding = false;
};

#endif // BMS_LOG_PARSER_H