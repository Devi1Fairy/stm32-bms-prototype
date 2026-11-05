#include "bms_csv_logger.h"

#include <QDateTime>
#include <QStringList>

BmsCsvLogger::~BmsCsvLogger()
{
    // 对象销毁时释放文件。
    stop();
}

bool BmsCsvLogger::start(const QString &fileName)
{
    if (isRecording())
    {
        m_error = QStringLiteral("当前已经在记录。");
        return false;
    }

    m_error.clear();
    m_rows = 0;
    m_file.setFileName(fileName);

    // 每次创建新文件，保护以前的实验记录。
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        m_error = m_file.errorString();
        return false;
    }

    const QByteArray header =
        "rx_utc,record_ms,state,load,clear_allowed,"
        "active_faults_hex,latched_faults_hex,"
        "cell0_mv_simulated,temp_est_dC,current_ma,iv,i_age_ms,i_src,"
        "stat_fresh,stat_age_ms,stat_segment,q_out_uah,q_in_uah,"
        "e_out_uwh,e_in_uwh,stat_covered_ms,stat_gaps,stat_overflow,"
        "bus_fields_present,bus_mv_measured,bus_v,bus_age_ms,bus_usable\n";

    if (!writeBytes(header))
    {
        return false;
    }

    m_clock.start();
    return true;
}

bool BmsCsvLogger::append(const BmsLogRecord &record,
                          const BmsStatRecord *stat,
                          qint64 statAgeMs, quint64 segment)
{
    if (!isRecording())
    {
        m_error = QStringLiteral("尚未开始记录。");
        return false;
    }

    m_error.clear();

    // 保留MCU原始单位和有效标志，方便之后分析异常。
    QStringList fields = {
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
        QString::number(m_clock.elapsed()),
        QString::number(record.state),
        QString::number(record.load),
        QString::number(record.clearAllowed),
        QStringLiteral("0x%1").arg(
            record.activeFaults, 8, 16, QLatin1Char('0')),
        QStringLiteral("0x%1").arg(
            record.latchedFaults, 8, 16, QLatin1Char('0')),
        QString::number(record.cellMv),
        QString::number(record.temperatureDc),
        QString::number(record.currentMa),
        QString::number(record.currentValid),
        QString::number(record.currentAgeMs),
        QString::number(record.currentSource)
    };

    // 每条BMS记录附带最近一次有效STAT。
    // 两种报文异步到达，stat_age_ms记录这份统计已接收多久。
    if (stat != nullptr && statAgeMs >= 0
        && statAgeMs < BmsStatTimeoutMs && segment != 0)
    {
        fields += QStringList{
            QStringLiteral("1"),
            QString::number(statAgeMs),
            QString::number(segment),
            QString::number(stat->qOutUah),
            QString::number(stat->qInUah),
            QString::number(stat->eOutUwh),
            QString::number(stat->eInUwh),
            QString::number(stat->coveredMs),
            QString::number(stat->gaps),
            QString::number(stat->overflow ? 1 : 0)
        };
    }
    else
    {
        fields.append(QStringLiteral("0"));

        // 没有统计或统计已过期时留空。
        // 填0会被误认为MCU实际测得的累计量为零。
        for (int i = 0; i < 9; ++i)
            fields.append(QString());
    }

    // 保留原始母线值，即使无效/过期也不伪造为0。
    // 分析时结合有效位、年龄和bus_usable判断是否能使用。
    fields.append(QString::number(record.busFieldsPresent ? 1 : 0));

    if (record.busFieldsPresent)
    {
        fields += QStringList{
            QString::number(record.busMv),
            QString::number(record.busValid),
            QString::number(record.busAgeMs)
        };
    }
    else
    {
        for (int i = 0; i < 3; ++i)
            fields.append(QString());
    }

    fields.append(QString::number(BmsBusUsable(record) ? 1 : 0));

    const QByteArray line = (fields.join(',') + '\n').toUtf8();

    if (!writeBytes(line))
    {
        return false;
    }

    ++m_rows;
    return true;
}

bool BmsCsvLogger::writeBytes(const QByteArray &data)
{
    if (m_file.write(data) != data.size() || !m_file.flush())
    {
        m_error = QStringLiteral("文件写入失败：%1")
                      .arg(m_file.errorString());

        m_file.close();
        m_clock.invalidate();
        return false;
    }

    return true;
}

bool BmsCsvLogger::stop()
{
    if (!isRecording())
    {
        return true;
    }

    m_error.clear();
    const bool ok = m_file.flush();

    if (!ok)
    {
        m_error = m_file.errorString();
    }

    m_file.close();
    m_clock.invalidate();
    return ok;
}