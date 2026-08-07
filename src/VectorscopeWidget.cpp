#include "VectorscopeWidget.h"

#include <QPainter>

#include <algorithm>

VectorscopeWidget::VectorscopeWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(1, 1);
}

void VectorscopeWidget::setImage(const QImage& image)
{
    image_ = image;
    update();
}
void VectorscopeWidget::resizeEvent(
    QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    const int scopeSize =
        std::min(
            width(),
            height());

    emit renderSizeChanged(
        scopeSize,
        scopeSize);
}
void VectorscopeWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);

    painter.fillRect(
        rect(),
        Qt::black);

    const int scopeSize =
        std::min(
            width(),
            height());

    const QRectF scopeRect(
        (width() - scopeSize) * 0.5,
        (height() - scopeSize) * 0.5,
        scopeSize,
        scopeSize);

    painter.setRenderHint(
        QPainter::Antialiasing,
        true);

    if (!image_.isNull())
    {
        painter.drawImage(
            scopeRect,
            image_);
    }

    graticule_.draw(
        painter,
        scopeRect);
}