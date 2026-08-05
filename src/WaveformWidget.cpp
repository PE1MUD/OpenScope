#include "WaveformWidget.h"

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

    painter.drawImage(rect(), image());
}