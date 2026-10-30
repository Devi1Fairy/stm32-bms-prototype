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
    next.activeFaults = read("faults", 0);
    next.latchedFaults = read("latched", 0);
    next.cellMv = read("cell0_mv");
    next.temperatureDc = read("temp_est_dC");
    next.currentMa = read("i_ma");
    next.currentValid = read("iv");
    next.currentAgeMs = read("i_age");
    next.currentSource = read("i_src");

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
        || next.currentMa < minSigned || next.currentMa > maxSigned)
    {
        return false;
    }

    // sample、ctrl等其他字段可以存在，本轮暂不使用。
    result = next;
    return true;
}

QList<QByteArray> BmsLineBuffer::append(const QByteArray &data)
{
    QList<QByteArray> lines;

    for (const char byte : data)
    {
        if (byte == '\n')
        {
            if (!m_discarding && !m_pending.trimmed().isEmpty())
            {
                lines.append(m_pending.trimmed());
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

void BmsLineBuffer::reset()
{
    m_pending.clear();
    m_discarding = false;
}