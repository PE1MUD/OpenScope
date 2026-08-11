#include <QPainter>
#include <QColor>
#include "VideoWidget.h"
#include <QResizeEvent>

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
    , image_(720, 576, QImage::Format_RGB32)
{
}

void VideoWidget::setImage(const QImage& image)
{
    image_ = image;
    update();
}

void VideoWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    painter.fillRect(
        rect(),
        Qt::black);

    if (image_.isNull())
    {
        return;
    }

    constexpr double displayAspectRatio =
        4.0 / 3.0;

    int targetWidth =
        width();

    int targetHeight =
        static_cast<int>(
            targetWidth /
            displayAspectRatio);

    if (targetHeight > height())
    {
        targetHeight =
            height();

        targetWidth =
            static_cast<int>(
                targetHeight *
                displayAspectRatio);
    }

    const int x =
        (width() - targetWidth) / 2;

    const int y =
        (height() - targetHeight) / 2;

    painter.drawImage(
        QRect(
            x,
            y,
            targetWidth,
            targetHeight),
        image_);
}
const QImage& VideoWidget::image() const
{
    return image_;
}

void VideoWidget::resizeEvent(
    QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    constexpr double displayAspectRatio =
        4.0 / 3.0;

    int outputWidth =
        event->size().width();

    int outputHeight =
        static_cast<int>(
            outputWidth /
            displayAspectRatio);

    if (outputHeight >
        event->size().height())
    {
        outputHeight =
            event->size().height();

        outputWidth =
            static_cast<int>(
                outputHeight *
                displayAspectRatio);
    }

    emit outputSizeChanged(
        outputWidth,
        outputHeight);
}