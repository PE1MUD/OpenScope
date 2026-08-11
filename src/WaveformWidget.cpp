#include "WaveformWidget.h"

#include <QPainter>
#include <QResizeEvent>
#include <QSlider>
#include <algorithm>

WaveformWidget::WaveformWidget(QWidget* parent)
    : VideoWidget(parent)
{
    scrollSlider_ =
        new QSlider(
            Qt::Horizontal,
            this);

    scrollSlider_->setRange(
        0,
        1000);

    scrollSlider_->setValue(0);
    scrollSlider_->hide();

    connect(
        scrollSlider_,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            setScrollPosition(
                static_cast<double>(value) /
                1000.0);
        });
}

bool WaveformWidget::isZoomed() const
{
    return zoomed_;
}

void WaveformWidget::setScrollPosition(
    double position)
{
    scrollPosition_ =
        std::clamp(
            position,
            0.0,
            1.0);
    emit scrollPositionChanged(
        scrollPosition_);

    update();
}

void WaveformWidget::setZoomed(bool zoomed)
{
    if (zoomed_ == zoomed)
    {
        return;
    }

    zoomed_ = zoomed;

    scrollSlider_->setVisible(
        zoomed_);

    emit zoomChanged(
        zoomed_);

    const int sliderHeight =
        scrollSlider_->isVisible()
        ? scrollSlider_->height()
        : 0;

    emit outputSizeChanged(
        width(),
        height() - sliderHeight);

    update();
}

void WaveformWidget::paintEvent(
    QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    painter.fillRect(
        rect(),
        Qt::black);

    if (image().isNull())
    {
        return;
    }

    const int sliderHeight =
        scrollSlider_->isVisible()
        ? scrollSlider_->height()
        : 0;

    const int waveformHeight =
        height() - sliderHeight;

    painter.drawImage(
        QRect(
            0,
            0,
            width(),
            waveformHeight),
        image());
}

void WaveformWidget::resizeEvent(
    QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    constexpr int sliderHeight = 24;

    scrollSlider_->setGeometry(
        0,
        height() - sliderHeight,
        width(),
        sliderHeight);

    const int waveformHeight =
        height() -
        (scrollSlider_->isVisible()
            ? sliderHeight
            : 0);

    emit outputSizeChanged(
        width(),
        waveformHeight);
}
