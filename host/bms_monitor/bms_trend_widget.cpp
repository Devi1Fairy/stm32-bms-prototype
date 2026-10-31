#include "bms_trend_widget.h"

#include <QPainter>
#include <QStringList>
#include <algorithm>

void BmsTrendHistory::append(qint64 timeMs,
                             const BmsLogRecord &record)
{
    if (timeMs < 0
        || (!m_samples.empty()
            && timeMs < m_samples.back().timeMs))
    {
        return;
    }

    m_samples.push_back({timeMs, record});
    trim(timeMs);
}

void BmsTrendHistory::trim(qint64 nowMs)
{
    while (!m_samples.empty()
           && (m_samples.size() > MaxSamples
               || m_samples.front().timeMs < nowMs - WindowMs))
    {
        m_samples.pop_front();
    }
}

void BmsTrendHistory::clear()
{
    m_samples.clear();
}

bool BmsTrendHistory::value(const BmsLogRecord &record,
                            int channel, double &result)
{
    if (channel == 2)
    {
        if (record.currentValid != 1
            || record.currentAgeMs >= 500
            || record.currentSource != 3)
        {
            return false;
        }

        result = static_cast<double>(record.currentMa);
        return true;
    }

    // 与数字概览使用相同的保守有效性判断。
    if (channel < 0 || channel > 1
        || record.state == 0
        || (record.activeFaults & 0xE0) != 0)
    {
        return false;
    }

    result = channel == 0
                 ? record.cellMv / 1000.0
                 : record.temperatureDc / 10.0;

    return true;
}

QList<QPolygonF> BmsTrendHistory::segments(
    int channel, qint64 nowMs) const
{
    QList<QPolygonF> result;
    QPolygonF part;
    qint64 previousMs = -1;

    for (const Sample &sample : m_samples)
    {
        if (sample.timeMs < nowMs - WindowMs
            || sample.timeMs > nowMs)
        {
            continue;
        }

        double number = 0;
        const bool valid =
            value(sample.record, channel, number);

        // 无效样本或时间缺口，将曲线分成不同段。
        if (!valid
            || (previousMs >= 0
                && sample.timeMs - previousMs >= 1000))
        {
            if (!part.isEmpty())
            {
                result.append(part);
                part.clear();
            }
        }

        if (valid)
        {
            part.append(
                QPointF(sample.timeMs / 1000.0, number));
        }

        previousMs = sample.timeMs;
    }

    if (!part.isEmpty())
    {
        result.append(part);
    }

    return result;
}

BmsTrendWidget::BmsTrendWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 200);
    m_clock.start();

    connect(&m_refreshTimer, &QTimer::timeout, this, [this]()
            {
                m_history.trim(m_clock.elapsed());
                update();
            });

    m_refreshTimer.start(200);
}

void BmsTrendWidget::addRecord(const BmsLogRecord &record)
{
    const qint64 nowMs = m_clock.elapsed();

    m_latest = record;
    m_lastReportMs = nowMs;
    m_history.append(nowMs, record);

    update();
}

void BmsTrendWidget::reset()
{
    m_history.clear();
    m_clock.start();
    m_lastReportMs = -1;

    update();
}

void BmsTrendWidget::setChannel(int channel)
{
    if (channel >= 0 && channel <= 2)
    {
        m_channel = channel;
        update();
    }
}

void BmsTrendWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());

    const qint64 nowMs = m_clock.elapsed();
    const auto parts =
        m_history.segments(m_channel, nowMs);

    const QStringList names = {
        QStringLiteral("模拟单体电压 / V"),
        QStringLiteral("NTC估算温度 / ℃"),
        QStringLiteral("INA226电流 / mA")
    };

    QString status = QStringLiteral("最近60秒");
    double latestValue = 0;

    if (m_lastReportMs < 0)
        status = QStringLiteral("等待上报");
    else if (nowMs - m_lastReportMs >= 1000)
        status = QStringLiteral("上报过期");
    else if (!BmsTrendHistory::value(
                 m_latest, m_channel, latestValue))
        status = QStringLiteral("当前采样无效");

    painter.setPen(palette().text().color());
    painter.drawText(
        QRectF(8, 3, width() - 16, 24),
        Qt::AlignLeft | Qt::AlignVCenter,
        names[m_channel] + " — " + status);

    const QRectF plot =
        QRectF(rect()).adjusted(66, 32, -16, -42);

    if (plot.width() <= 0 || plot.height() <= 0)
        return;

    double low = 0;
    double high = 1;
    bool found = false;

    for (const QPolygonF &part : parts)
    {
        for (const QPointF &point : part)
        {
            if (!found)
            {
                low = high = point.y();
                found = true;
            }

            low = std::min(low, point.y());
            high = std::max(high, point.y());
        }
    }

    if (found)
    {
        // 数值不变时也留出纵轴空间。
        const double minimumPadding[] = {0.02, 0.5, 1.0};

        const double padding = std::max(
            (high - low) * 0.1,
            minimumPadding[m_channel]);

        low -= padding;
        high += padding;
    }

    const double leftSeconds = std::max(
        0.0,
        (nowMs - BmsTrendHistory::WindowMs) / 1000.0);

    // 网格与坐标。
    for (int i = 0; i <= 4; ++i)
    {
        const double fraction = i / 4.0;
        const double x =
            plot.left() + fraction * plot.width();
        const double y =
            plot.bottom() - fraction * plot.height();

        painter.setPen(palette().mid().color());
        painter.drawLine(
            QPointF(x, plot.top()),
            QPointF(x, plot.bottom()));
        painter.drawLine(
            QPointF(plot.left(), y),
            QPointF(plot.right(), y));

        painter.setPen(palette().text().color());

        painter.drawText(
            QRectF(x - 30, plot.bottom() + 4, 60, 18),
            Qt::AlignCenter,
            QString::number(
                leftSeconds + fraction * 60, 'f', 0));

        painter.drawText(
            QRectF(0, y - 10, plot.left() - 7, 20),
            Qt::AlignRight | Qt::AlignVCenter,
            QString::number(
                low + fraction * (high - low),
                'f', m_channel == 0 ? 2 : 1));
    }

    painter.drawRect(plot);
    painter.drawText(
        QRectF(plot.left(), height() - 23, plot.width(), 20),
        Qt::AlignCenter,
        QStringLiteral("本次连接经过时间 / s"));

    painter.save();
    painter.setClipRect(plot.adjusted(-1, -1, 1, 1));
    painter.setPen(QPen(QColor(0, 120, 215), 2));

    for (const QPolygonF &part : parts)
    {
        QPolygonF pixels;

        for (const QPointF &point : part)
        {
            const double x = plot.left()
            + (point.x() - leftSeconds)
                / 60.0 * plot.width();

            const double y = plot.bottom()
                             - (point.y() - low)
                                   / (high - low) * plot.height();

            pixels.append(QPointF(x, y));
        }

        painter.drawPolyline(pixels);

        // 只有一个有效点时也能看见。
        if (pixels.size() == 1)
            painter.drawEllipse(pixels.front(), 2, 2);
    }

    painter.restore();
}