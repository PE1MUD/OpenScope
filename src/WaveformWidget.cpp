#include "WaveformWidget.h"

#include <QPainter>
#include <QResizeEvent>
#include <QSlider>
#include <algorithm>

WaveformWidget::WaveformWidget(QWidget* parent)
    : VideoWidget(parent)
{
    fpsTimer_.start();

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
    qDebug()
        << "WaveformWidget zoomed:"
        << zoomed
        << "output:"
        << (zoomed ? width() * 10 : width());

    if (zoomed_ == zoomed)
    {
        return;
    }

    zoomed_ = zoomed;

    scrollSlider_->setVisible(
        zoomed_);

    emit zoomChanged(
        zoomed_);

    emit outputSizeChanged(
        width(),
        height());

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
    VideoWidget::resizeEvent(event);

    constexpr int sliderHeight = 24;

    scrollSlider_->setGeometry(
        0,
        height() - sliderHeight,
        width(),
        sliderHeight);

    emit outputSizeChanged(
        width(),
        height() - sliderHeight);
}

void WaveformWidget::setDisplayBandwidthMHz(
    double bandwidthMHz)
{
    displayBandwidthMHz_ =
        bandwidthMHz;

    update();
}

void WaveformWidget::notifyFrameRendered()
{
    ++frameCounter_;

    const qint64 elapsedMs =
        fpsTimer_.elapsed();

    if (elapsedMs >= 500)
    {
        fps_ =
            static_cast<double>(
                frameCounter_) *
            1000.0 /
            static_cast<double>(
                elapsedMs);

        frameCounter_ = 0;

        fpsTimer_.restart();

        update();
    }
}