#include "WaveformWidget.h"
#include <QResizeEvent>
#include <QPainter>

WaveformWidget::WaveformWidget(QWidget* parent)
    : VideoWidget(parent)
{}

void WaveformWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (image().isNull()) {
        return;
    }

    painter.drawImage(
        QPoint(0, 0),
        image());
    QString text =
        QString("Trace BW %1 MHz")
        .arg(displayBandwidthMHz_, 0, 'f', 2);

    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(10);

    painter.setFont(font);

QColor color;

if (displayBandwidthMHz_ >= 6.75)
{
    // Full bandwidth
    color = QColor(255,255,255);
}
else if (displayBandwidthMHz_ >= 5.5)
{
    // Light yellow
    color = QColor(255,255,128);
}
else if (displayBandwidthMHz_ >= 4.0)
{
    // Yellow
    color = QColor(255,255,0);
}
else if (displayBandwidthMHz_ >= 2.5)
{
    // Orange
    color = QColor(255,180,0);
}
else
{
    // Red
    color = QColor(255,64,64);
}

    QRect r =
        painter.fontMetrics()
        .boundingRect(text);

    r.adjust(-6, -4, 6, 4);

    r.moveTopRight(
        QPoint(width() - 10, 10));

    painter.fillRect(
        r,
        QColor(0, 0, 0, 180));

    painter.setPen(color);

    painter.drawText(
        r,
        Qt::AlignCenter,
        text);
}

void WaveformWidget::resizeEvent(QResizeEvent* event)
{
    VideoWidget::resizeEvent(event);

    emit outputSizeChanged(
        event->size().width(),
        event->size().height());
}

void WaveformWidget::setDisplayBandwidthMHz(
    double bandwidthMHz)
{
    displayBandwidthMHz_ = bandwidthMHz;
    update();
}