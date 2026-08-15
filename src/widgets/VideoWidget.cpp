#include "widgets/VideoWidget.h"

#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

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

void VideoWidget::setAspectRatio(
    OpenScopeSettings::AspectRatio aspectRatio)
{
    if (aspectRatio_ == aspectRatio)
    {
        return;
    }

    aspectRatio_ = aspectRatio;

    emitOutputSize();
    update();
}

const QImage& VideoWidget::image() const
{
    return image_;
}

QSize VideoWidget::fitAspectSize(
    int availableWidth,
    int availableHeight) const
{
    availableWidth =
        (std::max)(availableWidth, 1);

    availableHeight =
        (std::max)(availableHeight, 1);

    const double aspectRatio =
        OpenScopeSettings::aspectRatioValue(
            aspectRatio_);

    int outputWidth =
        availableWidth;

    int outputHeight =
        static_cast<int>(
            std::lround(
                static_cast<double>(outputWidth) /
                aspectRatio));

    if (outputHeight > availableHeight)
    {
        outputHeight =
            availableHeight;

        outputWidth =
            static_cast<int>(
                std::lround(
                    static_cast<double>(outputHeight) *
                    aspectRatio));
    }

    return QSize(
        (std::max)(outputWidth, 1),
        (std::max)(outputHeight, 1));
}

void VideoWidget::emitOutputSize()
{
    const QSize outputSize =
        fitAspectSize(
            width(),
            height());

    emit outputSizeChanged(
        outputSize.width(),
        outputSize.height());
}

bool VideoWidget::emitImagePosition(
    const QPointF& position)
{
    if (image_.isNull())
    {
        return false;
    }

    const QSize outputSize =
        fitAspectSize(
            width(),
            height());

    const QRect imageRect(
        (width() - outputSize.width()) / 2,
        (height() - outputSize.height()) / 2,
        outputSize.width(),
        outputSize.height());

    if (!imageRect.contains(
            position.toPoint()))
    {
        return false;
    }

    const double normalizedX =
        std::clamp(
            (position.x() -
                static_cast<double>(imageRect.left())) /
                static_cast<double>(
                    (std::max)(imageRect.width() - 1, 1)),
            0.0,
            1.0);

    const double normalizedY =
        std::clamp(
            (position.y() -
                static_cast<double>(imageRect.top())) /
                static_cast<double>(
                    (std::max)(imageRect.height() - 1, 1)),
            0.0,
            1.0);

    emit imageClicked(
        normalizedX,
        normalizedY);

    return true;
}

void VideoWidget::mousePressEvent(
    QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        const QSize outputSize =
            fitAspectSize(
                width(),
                height());

        const QRect imageRect(
            (width() - outputSize.width()) / 2,
            (height() - outputSize.height()) / 2,
            outputSize.width(),
            outputSize.height());

        if (imageRect.contains(
                event->position().toPoint()))
        {
            emit leftInteractionStarted();

            if (emitImagePosition(
                    event->position()))
            {
                event->accept();
                return;
            }
        }
    }

    if (event->button() == Qt::RightButton)
    {
        const QSize outputSize =
            fitAspectSize(
                width(),
                height());

        const QRect imageRect(
            (width() - outputSize.width()) / 2,
            (height() - outputSize.height()) / 2,
            outputSize.width(),
            outputSize.height());

        if (imageRect.contains(
                event->position().toPoint()))
        {
            emit rightClicked();
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void VideoWidget::mouseMoveEvent(
    QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) != 0 &&
        emitImagePosition(
            event->position()))
    {
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void VideoWidget::mouseDoubleClickEvent(
    QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        // The first click of a Qt double-click has already travelled
        // through mousePressEvent() and therefore may have changed the
        // selected waveform line / X position. Restore that pre-click
        // state before the parent maximizes the viewport.
        emit doubleClickRestoreRequested();
    }

    // Let the containing ScopeViewport handle maximize/restore.
    event->ignore();
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

    const QSize outputSize =
        fitAspectSize(
            width(),
            height());

    const int x =
        (width() - outputSize.width()) / 2;

    const int y =
        (height() - outputSize.height()) / 2;

    painter.drawImage(
        QRect(
            x,
            y,
            outputSize.width(),
            outputSize.height()),
        image_);
}

void VideoWidget::resizeEvent(
    QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    emitOutputSize();
}
