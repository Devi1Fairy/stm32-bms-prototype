#ifndef BMS_CSV_LOGGER_H
#define BMS_CSV_LOGGER_H

#include "bms_log_parser.h"
#include <QElapsedTimer>
#include <QFile>

class BmsCsvLogger
{
public:
    ~BmsCsvLogger();

    bool start(const QString &fileName);
    bool append(const BmsLogRecord &record);
    bool stop();

    bool isRecording() const { return m_file.isOpen(); }
    quint64 rowCount() const { return m_rows; }
    QString fileName() const { return m_file.fileName(); }
    QString errorString() const { return m_error; }

private:
    bool writeBytes(const QByteArray &data);

    QFile m_file;
    QElapsedTimer m_clock;
    quint64 m_rows = 0;
    QString m_error;
};

#endif // BMS_CSV_LOGGER_H