#include "widgets/WaveformWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kScrollSliderHeight = 24;

    class ScrollValueSlider final : public QSlider
    {
    public:
        explicit ScrollValueSlider(
            Qt::Orientation orientation,
            QWidget* parent = nullptr)
            : QSlider(
                orientation,
                parent)
        {
        }

    protected:
        void mousePressEvent(
            QMouseEvent* event) override
        {
            if (event->button() == Qt::LeftButton)
            {
                setSliderDown(
                    true);

                setValueFromMouse(
                    event->position());

                event->accept();
                return;
            }

            QSlider::mousePressEvent(
                event);
        }

        void mouseMoveEvent(
            QMouseEvent* event) override
        {
            if (isSliderDown() &&
                (event->buttons() & Qt::LeftButton) != 0)
            {
                setValueFromMouse(
                    event->position());

                event->accept();
                return;
            }

            QSlider::mouseMoveEvent(
                event);
        }

        void mouseReleaseEvent(
            QMouseEvent* event) override
        {
            if (event->button() == Qt::LeftButton &&
                isSliderDown())
            {
                setValueFromMouse(
                    event->position());

                setSliderDown(
                    false);

                event->accept();
                return;
            }

            QSlider::mouseReleaseEvent(
                event);
        }

        void paintEvent(
            QPaintEvent* event) override
        {
            QSlider::paintEvent(
                event);

            QStyleOptionSlider option;
            initStyleOption(
                &option);

            const QRect nativeHandleRect =
                style()->subControlRect(
                    QStyle::CC_Slider,
                    &option,
                    QStyle::SC_SliderHandle,
                    this);

            QRect handleRect =
                nativeHandleRect;

            constexpr int kValueWidth = 34;

            handleRect.setWidth(
                kValueWidth);

            handleRect.moveCenter(
                nativeHandleRect.center());

            const int displayedPosition =
                static_cast<int>(
                    std::lround(
                        static_cast<double>(
                            value()) /
                        10.0));

            QPainter painter(
                this);

            painter.setRenderHint(
                QPainter::Antialiasing);

            painter.setPen(
                palette().color(
                    QPalette::Mid));

            painter.setBrush(
                palette().color(
                    QPalette::Button));

            painter.drawRoundedRect(
                handleRect.adjusted(
                    0,
                    1,
                    -1,
                    -1),
                3.0,
                3.0);

            QFont font =
                painter.font();

            if (font.pointSizeF() > 0.0)
            {
                font.setPointSizeF(
                    std::max(
                        7.0,
                        font.pointSizeF() - 1.0));
            }

            font.setBold(
                true);

            painter.setFont(
                font);

            painter.setPen(
                isEnabled()
                ? palette().color(
                    QPalette::ButtonText)
                : palette().color(
                    QPalette::Disabled,
                    QPalette::ButtonText));

            painter.drawText(
                handleRect,
                Qt::AlignCenter,
                QString::number(
                    displayedPosition));
        }

    private:
        void setValueFromMouse(
            const QPointF& position)
        {
            constexpr int kValueWidth = 34;

            const int span =
                (std::max)(
                    width() - kValueWidth,
                    1);

            const int pixelPosition =
                std::clamp(
                    static_cast<int>(
                        std::lround(
                            position.x() -
                            kValueWidth * 0.5)),
                    0,
                    span);

            setValue(
                QStyle::sliderValueFromPosition(
                    minimum(),
                    maximum(),
                    pixelPosition,
                    span,
                    invertedAppearance()));
        }
    };

}

WaveformWidget::WaveformWidget(QWidget* parent)
    : VideoWidget(parent)
{
    scrollSlider_ =
        new ScrollValueSlider(
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
    return zoomFactor_ > 1;
}

int WaveformWidget::zoomFactor() const
{
    return zoomFactor_;
}

void WaveformWidget::setScrollPosition(
    double position)
{
    scrollPosition_ =
        std::clamp(
            position,
            0.0,
            1.0);

    const int sliderValue =
        static_cast<int>(
            std::lround(
                scrollPosition_ *
                1000.0));

    if (scrollSlider_->value() != sliderValue)
    {
        scrollSlider_->setValue(
            sliderValue);

        return;
    }

    emit scrollPositionChanged(
        scrollPosition_);

    update();
}

void WaveformWidget::setZoomEnabled(bool enabled)
{
    if (zoomEnabled_ == enabled)
    {
        return;
    }

    zoomEnabled_ = enabled;

    if (!zoomEnabled_ && zoomFactor_ != 1)
    {
        setZoomFactor(1);
    }
}

void WaveformWidget::setZoomed(bool zoomed)
{
    setZoomFactor(
        zoomed
        ? 10
        : 1);
}

void WaveformWidget::setZoomFactor(int factor)
{
    if (factor != 1 &&
        factor != 5 &&
        factor != 10)
    {
        factor = 1;
    }

    if (factor > 1 && !zoomEnabled_)
    {
        factor = 1;
    }

    if (zoomFactor_ == factor)
    {
        return;
    }

    zoomFactor_ = factor;

    scrollSlider_->setVisible(
        zoomFactor_ > 1);

    updateScrollSliderGeometry();

    emit zoomFactorChanged(
        zoomFactor_);

    emit zoomChanged(
        zoomFactor_ > 1);

    emitOutputSize();
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
        image());
}

void WaveformWidget::resizeEvent(
    QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    updateScrollSliderGeometry();
    emitOutputSize();
}

void WaveformWidget::updateScrollSliderGeometry()
{
    scrollSlider_->setGeometry(
        0,
        height() - kScrollSliderHeight,
        width(),
        kScrollSliderHeight);

    scrollSlider_->raise();
}
