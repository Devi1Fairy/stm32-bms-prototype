#ifndef BMS_TREND_WIDGET_H
#define BMS_TREND_WIDGET_H

#include "bms_log_parser.h"
#include <QElapsedTimer>
#include <QPolygonF>
#include <QTimer>
#include <QWidget>
#include <cstddef>
#include <deque>

// 历史模型只整理数据，绘图控件负责显示。
class BmsTrendHistory
{
public:
    static constexpr qint64 WindowMs = 60000;
    static constexpr std::size_t MaxSamples = 600;

    void append(qint64 timeMs, const BmsLogRecord &record);
    void trim(qint64 nowMs);
    void clear();

    std::size_t size() const { return m_samples.size(); }

    static bool value(const BmsLogRecord &record,
                      int channel, double &result);

    QList<QPolygonF> segments(int channel, qint64 nowMs) const;

private:
    struct Sample
    {
        qint64 timeMs;
        BmsLogRecord record;
    };

    std::deque<Sample> m_samples;
};

class BmsTrendWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BmsTrendWidget(QWidget *parent = nullptr);

    void addRecord(const BmsLogRecord &record);
    void reset();
    void setChannel(int channel);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    BmsTrendHistory m_history;
    QElapsedTimer m_clock;
    QTimer m_refreshTimer;
    BmsLogRecord m_latest;

    qint64 m_lastReportMs = -1;
    int m_channel = 0;
};

#endif // BMS_TREND_WIDGET_H