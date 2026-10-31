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
        "cell0_mv_simulated,temp_est_dC,current_ma,iv,i_age_ms,i_src\n";

    if (!writeBytes(header))
    {
        return false;
    }

    m_clock.start();
    return true;
}

bool BmsCsvLogger::append(const BmsLogRecord &record)
{
    if (!isRecording())
    {
        m_error = QStringLiteral("尚未开始记录。");
        return false;
    }

    m_error.clear();

    // 保留MCU原始单位和有效标志，方便之后分析异常。
    const QStringList fields = {
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