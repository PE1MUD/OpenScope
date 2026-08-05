#include "VectorscopeWidget.h"

#include <QPainter>
#include <QDebug>

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

void VectorscopeWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (image_.isNull())
    {
        return;
    }

    const QSize scaledSize =
        image_.size().scaled(
            size(),
            Qt::KeepAspectRatio);

    const QRect target(
        (width() - scaledSize.width()) / 2,
        (height() - scaledSize.height()) / 2,
        scaledSize.width(),
        scaledSize.height());

    painter.drawImage(target, image_);
}

