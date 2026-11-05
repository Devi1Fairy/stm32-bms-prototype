#include "bms_log_parser.h"

#include <QHash>
#include <limits>
#include <QStringList>

bool ParseBmsLog(const QByteArray &line, BmsLogRecord &result)
{
    const QByteArray text = line.trimmed();

    if (!text.startsWith("BMS "))
    {
        return false;
    }

    QHash<QByteArray, QByteArray> fields;
    const auto parts = text.mid(4).split(',');

    for (const QByteArray &part : parts)
    {
        const qsizetype equal = part.indexOf('=');

        if (equal <= 0 || equal != part.lastIndexOf('='))
        {
            return false;
        }

        const QByteArray key = part.left(equal).trimmed();
        const QByteArray value = part.mid(equal + 1).trimmed();

        // 空字段、空数值和重复字段都拒绝。
        if (key.isEmpty() || value.isEmpty() || fields.contains(key))
        {
            return false;
        }

        fields.insert(key, value);
    }

    bool valid = true;

    // 按名字读取字段，并累计转换结果。
    // 转换失败返回的0不能被当成真实测量值。
    const auto read = [&fields, &valid](const char *key, int base = 10)
    {
        bool ok = false;
        const qint64 value = fields.value(key).toLongLong(&ok, base);

        valid = valid && ok;
        return value;
    };

    BmsLogRecord next;
    next.state = read("state");
    next.load = read("load");
    next.clearAllowed = read("clear");
    next.activeFaults = read("faults", 0);
    next.latchedFaults = read("latched", 0);
    next.cellMv = read("cell0_mv");
    next.temperatureDc = read("temp_est_dC");
    next.currentMa = read("i_ma");
    next.currentValid = read("iv");
    next.currentAgeMs = read("i_age");
    next.currentSource = read("i_src");

    // 旧固件只有bus_mv，不足以判断这份母线测量是否有效。
    // 一旦出现有效性或年龄字段，三项必须一起存在且合法。
    next.busFieldsPresent = fields.contains("bus_v") || fields.contains("bus_age");

    if (next.busFieldsPresent)
    {
        const auto readBus = [&fields, &valid](const char *key) -> qint64
        {
            const QByteArray value = fields.value(key);

            if (value.isEmpty())
            {
                valid = false;
                return 0;
            }

            // 母线字段只接受非负十进制整数。
            for (char c : value)
            {
                if (c < '0' || c > '9')
                {
                    valid = false;
                    return 0;
                }
            }

            bool ok = false;
            const qint64 number = value.toLongLong(&ok, 10);

            valid = valid && ok;
            return number;
        };

        next.busMv = readBus("bus_mv");
        next.busValid = readBus("bus_v");
        next.busAgeMs = readBus("bus_age");

        if (next.busMv > 36000 || next.busValid > 1
            || next.busAgeMs > std::numeric_limits<quint32>::max())
            valid = false;
    }

    const qint64 minSigned = std::numeric_limits<qint32>::min();
    const qint64 maxSigned = std::numeric_limits<qint32>::max();
    const qint64 maxUnsigned = std::numeric_limits<quint32>::max();

    if (!valid
        || next.state < 0 || next.state > 3
        || next.load < 0 || next.load > 1
        || next.currentValid < 0 || next.currentValid > 1
        || next.currentSource < 0 || next.currentSource > 3
        || next.activeFaults < 0 || next.activeFaults > maxUnsigned
        || next.latchedFaults < 0 || next.latchedFaults > maxUnsigned
        || next.currentAgeMs < 0 || next.currentAgeMs > maxUnsigned
        || next.cellMv < 0 || next.cellMv > maxSigned
        || next.temperatureDc < minSigned
        || next.temperatureDc > maxSigned
        || next.currentMa < minSigned || next.currentMa > maxSigned
        || next.clearAllowed < 0 || next.clearAllowed > 1)
    {
        return false;
    }

    // sample、ctrl等其他字段可以存在，本轮暂不使用。
    result = next;
    return true;
}

QList<QByteArray> BmsLineBuffer::append(
    const QByteArray &data, bool trimLines)
{
    QList<QByteArray> lines;

    for (const char byte : data)
    {
        if (byte == '\n')
        {
            if (!m_discarding && !m_pending.trimmed().isEmpty())
            {
                // 串口保持原有行为；TCP保留原始正文供协议精确检查。
                lines.append(trimLines ? m_pending.trimmed() : m_pending);
            }

            m_pending.clear();
            m_discarding = false;
        }
        else if (!m_discarding)
        {
            if (m_pending.size() >= 1024)
            {
                // 超长行一直丢到换行。
                // 不能清空后把这行的后半截误认为新报文。
                m_pending.clear();
                m_discarding = true;
            }
            else
            {
                m_pending.append(byte);
            }
        }
    }

    return lines;
}

bool ParseBmsStatLog(const QByteArray &line, BmsStatRecord &result)
{
    const QByteArray text = line.trimmed();

    if (!text.startsWith("STAT ") || text.size() > 1024)
        return false;

    // 先把“字段名=字段值”拆开。
    QHash<QByteArray, QByteArray> fields;

    for (const QByteArray &part : text.mid(5).split(','))
    {
        const qsizetype equal = part.indexOf('=');

        // 每一项只能有一个等号。
        if (equal <= 0 || equal != part.lastIndexOf('='))
            return false;

        const QByteArray key = part.left(equal).trimmed();
        const QByteArray value = part.mid(equal + 1).trimmed();

        // 不接受空字段和重复字段。
        if (key.isEmpty() || value.isEmpty() || fields.contains(key))
            return false;

        fields.insert(key, value);
    }

    bool valid = true;

    const auto read = [&fields, &valid](const char *key) -> quint64
    {
        const QByteArray value = fields.value(key);

        if (value.isEmpty())
        {
            valid = false;
            return 0;
        }

        // 累计量只接受十进制数字，拒绝负号、小数和十六进制。
        for (char c : value)
        {
            if (c < '0' || c > '9')
            {
                valid = false;
                return 0;
            }
        }

        bool ok = false;
        const quint64 number = value.toULongLong(&ok, 10);

        valid = valid && ok;
        return number;
    };

    BmsStatRecord next;

    next.qOutUah = read("q_out_uah");
    next.qInUah = read("q_in_uah");
    next.eOutUwh = read("e_out_uwh");
    next.eInUwh = read("e_in_uwh");
    next.coveredMs = read("covered_ms");

    const quint64 gaps = read("gaps");
    const quint64 overflow = read("overflow");

    if (!valid || gaps > std::numeric_limits<quint32>::max()
        || overflow > 1)
        return false;

    next.gaps = static_cast<quint32>(gaps);
    next.overflow = overflow != 0;

    // 避免报文前几个字段正常、后几个异常时，只更新了一半数据。
    result = next;
    return true;
}

void BmsLineBuffer::reset()
{
    m_pending.clear();
    m_discarding = false;
}

bool BmsBusUsable(const BmsLogRecord &record)
{
    // 判断本条报文携带的采样状态。
    // 整体上报超时仍由窗口原来的逻辑处理。
    return record.busFieldsPresent && record.busValid == 1
           && record.busMv >= 0 && record.busMv <= 36000
           && record.busAgeMs >= 0 && record.busAgeMs < BmsBusMaxAgeMs;
}
