#include "widgets/VectorscopeWidget.h"
#include "VectorscopeSettings.h"

#include <QPainter>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

VectorscopeWidget::VectorscopeWidget(QWidget* parent)
    : QWidget(parent)
{
    graticule_.setScale(
        VectorscopeSettings::scale);

    setGraticuleLineWidth(
        VectorscopeSettings::graticuleLineWidth);
}

void VectorscopeWidget::setImage(const QImage& image)
{
    image_ = image;
    update();
}

void VectorscopeWidget::setAspectRatio(
    OpenScopeSettings::AspectRatio aspectRatio)
{
    if (aspectRatio_ == aspectRatio)
    {
        return;
    }

    aspectRatio_ = aspectRatio;

    emitRenderSize();
    update();
}

QSize VectorscopeWidget::fitAspectSize() const
{
    const int availableWidth =
        (std::max)(width(), 1);

    const int availableHeight =
        (std::max)(height(), 1);

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

void VectorscopeWidget::emitRenderSize()
{
    const QSize renderSize =
        fitAspectSize();

    emit renderSizeChanged(
        renderSize.width(),
        renderSize.height());
}

void VectorscopeWidget::resizeEvent(
    QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    emitRenderSize();
}

void VectorscopeWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);

    painter.fillRect(
        rect(),
        Qt::black);

    const QSize renderSize =
        fitAspectSize();

    const QRectF renderRect(
        (width() - renderSize.width()) * 0.5,
        (height() - renderSize.height()) * 0.5,
        renderSize.width(),
        renderSize.height());

    painter.setRenderHint(
        QPainter::Antialiasing,
        true);

    if (!image_.isNull())
    {
        painter.drawImage(
            renderRect,
            image_);
    }

    const double scopeSize =
        std::min(
            renderRect.width(),
            renderRect.height());

    const QRectF scopeRect(
        renderRect.center().x() - scopeSize * 0.5,
        renderRect.center().y() - scopeSize * 0.5,
        scopeSize,
        scopeSize);

    graticule_.draw(
        painter,
        scopeRect);
}

void VectorscopeWidget::setGraticuleLineWidth(double width)
{
    graticule_.setLineWidth(width);
    update();
}
