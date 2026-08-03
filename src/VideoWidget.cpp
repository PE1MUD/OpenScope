#include <QPainter>
#include <QColor>
#include "VideoWidget.h"

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
    , image_(720, 576, QImage::Format_RGB32)
{
    for (int y = 0; y < image_.height(); ++y) {
        for (int x = 0; x < image_.width(); ++x) {
            image_.setPixelColor(
                x,
                y,
                QColor(
                    x * 255 / image_.width(),
                    y * 255 / image_.height(),
                    128
                )
            );
        }
    }
}

void VideoWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.drawImage(rect(), image_);
}
