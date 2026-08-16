#include "widgets/WaveformWidget.h"

#include "rendering/WaveformGraticule.h"
#include "standards/VideoStandard.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetricsF>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double kBlackLevelVolts = 0.3;
constexpr double kWhiteLevelVolts = 1.0;

QPointF clampPointToRect(
    const QPointF& point,
    const QRect& rect)
{
    return
    {
        std::clamp(
            point.x(),
            static_cast<double>(rect.left()),
            static_cast<double>(rect.right())),
        std::clamp(
            point.y(),
            static_cast<double>(rect.top()),
            static_cast<double>(rect.bottom()))
    };
}

QString formatPercent(
    double percent)
{
    if (std::abs(percent) < 0.05)
    {
        percent = 0.0;
    }

    return QStringLiteral("%1%").arg(percent, 0, 'f', 1);
}

void drawArrowHead(
    QPainter& painter,
    const QPointF& tip,
    const QPointF& direction,
    double length,
    double width)
{
    const QPointF baseCenter =
        tip - direction * length;

    const QPointF perpendicular(
        -direction.y(),
        direction.x());

    const QPointF sideA =
        baseCenter + perpendicular * width;

    const QPointF sideB =
        baseCenter - perpendicular * width;

    painter.drawLine(tip, sideA);
    painter.drawLine(tip, sideB);
}
}

WaveformWidget::WaveformWidget(QWidget* parent)
    : VideoWidget(parent)
{
    setMouseTracking(true);
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
    const double newPosition =
        std::clamp(
            position,
            0.0,
            1.0);

    if (newPosition == scrollPosition_)
    {
        return;
    }

    scrollPosition_ = newPosition;

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

    if (zoomFactor_ <= 1)
    {
        panActive_ = false;
    }

    unsetCursor();

    emit zoomFactorChanged(
        zoomFactor_);

    emit zoomChanged(
        zoomFactor_ > 1);

    emitOutputSize();
    update();
}

QRect WaveformWidget::imageRect() const
{
    const QSize outputSize =
        fitAspectSize(
            width(),
            height());

    return QRect(
        (width() - outputSize.width()) / 2,
        (height() - outputSize.height()) / 2,
        outputSize.width(),
        outputSize.height());
}

void WaveformWidget::updateHover(
    const QPointF& position)
{
    const QRect displayRect =
        imageRect();

    if (!displayRect.contains(
            position.toPoint()))
    {
        if (hoverActive_)
        {
            hoverActive_ = false;
            update();
        }

        return;
    }

    hoverPosition_ = position;
    hoverActive_ = true;
    update();
}

void WaveformWidget::leaveEvent(QEvent* event)
{
    hoverActive_ = false;
    measureActive_ = false;
    update();

    QWidget::leaveEvent(event);
}

void WaveformWidget::mousePressEvent(
    QMouseEvent* event)
{
    const QRect displayRect =
        imageRect();

    if (event->button() == Qt::LeftButton &&
        displayRect.contains(
            event->position().toPoint()))
    {
        measureActive_ = true;
        measureStartPosition_ =
            clampPointToRect(
                event->position(),
                displayRect);
        measureCurrentPosition_ =
            measureStartPosition_;
        hoverActive_ = false;

        event->accept();
        update();
        return;
    }

    if (zoomFactor_ > 1 &&
        event->button() == Qt::RightButton &&
        displayRect.contains(
            event->position().toPoint()))
    {
        panActive_ = true;
        panStartX_ = event->position().x();
        panStartScrollPosition_ = scrollPosition_;
        hoverActive_ = false;

        setCursor(Qt::ClosedHandCursor);

        event->accept();
        return;
    }

    VideoWidget::mousePressEvent(event);
}

void WaveformWidget::mouseMoveEvent(
    QMouseEvent* event)
{
    const QRect displayRect =
        imageRect();

    if (measureActive_ &&
        (event->buttons() & Qt::LeftButton) != 0)
    {
        measureCurrentPosition_ =
            clampPointToRect(
                event->position(),
                displayRect);

        event->accept();
        update();
        return;
    }

    if (panActive_ &&
        (event->buttons() & Qt::RightButton) != 0 &&
        zoomFactor_ > 1)
    {
        const QSize outputSize =
            fitAspectSize(
                width(),
                height());

        const double displayWidth =
            static_cast<double>(
                (std::max)(
                    outputSize.width(),
                    1));

        const double dragPixels =
            event->position().x() -
            panStartX_;

        const double normalizedDelta =
            dragPixels /
            (displayWidth *
                static_cast<double>(
                    zoomFactor_ - 1));

        setScrollPosition(
            panStartScrollPosition_ -
            normalizedDelta);

        event->accept();
        return;
    }

    if (event->buttons() == Qt::NoButton)
    {
        updateHover(
            event->position());
    }

    VideoWidget::mouseMoveEvent(event);
}

void WaveformWidget::mouseReleaseEvent(
    QMouseEvent* event)
{
    if (measureActive_ &&
        event->button() == Qt::LeftButton)
    {
        measureActive_ = false;
        updateHover(event->position());
        event->accept();
        update();
        return;
    }

    if (panActive_ &&
        event->button() == Qt::RightButton)
    {
        panActive_ = false;

        unsetCursor();

        updateHover(
            event->position());

        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
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

    const QRect displayRect =
        imageRect();

    painter.drawImage(
        displayRect,
        image());

    const VideoStandard standard =
        VideoStandard::pal625();

    const WaveformGraticule graticule;

    const double renderedScopeHeight =
        static_cast<double>(
            (std::max)(image().height() - 1, 1));

    const QFont graticuleFont =
        graticule.labelFont(
            QApplication::font(),
            renderedScopeHeight);

    const QFontMetricsF graticuleMetrics(
        graticuleFont,
        &image());

    const double labelHeight =
        graticuleMetrics.height();

    const double renderedScopeTop =
        labelHeight * 0.5;

    const double renderedScopeUsableHeight =
        renderedScopeHeight -
        labelHeight;

    const AnalogVideoLevels analog =
        analogLevels(
            standard.colorStandard);

    const auto voltsToRenderedY =
        [&](double volts)
        {
            return
                renderedScopeTop +
                renderedScopeUsableHeight -
                volts *
                renderedScopeUsableHeight /
                analog.graticuleMaxVolts;
        };

    const double imageToDisplayScaleY =
        static_cast<double>(displayRect.height()) /
        static_cast<double>(
            (std::max)(image().height(), 1));

    const auto renderedYToDisplayY =
        [&](double renderedY)
        {
            return
                static_cast<double>(displayRect.top()) +
                renderedY * imageToDisplayScaleY;
        };

    const double zeroVoltY =
        renderedYToDisplayY(
            voltsToRenderedY(0.0));

    const double oneVoltY =
        renderedYToDisplayY(
            voltsToRenderedY(1.0));

    const double voltsPerDisplayPixel =
        1.0 /
        (zeroVoltY - oneVoltY);

    const auto displayYToVolts =
        [&](double displayY)
        {
            return
                (zeroVoltY - displayY) *
                voltsPerDisplayPixel;
        };

    // Mirror the renderer's horizontal scope geometry. The trace does not
    // start at image x=0: the graticule reserves a left inset for labels.
    // Frequency measurements must therefore use the actual scope width,
    // not the complete widget/image width.
    const double renderedViewportHeight =
        static_cast<double>(
            (std::max)(image().height() - 1, 1));

    const double renderedScopeLeft =
        graticule.leftInset(
            QApplication::font(),
            &image(),
            renderedViewportHeight);

    const double renderedScopeRight =
        static_cast<double>(
            (std::max)(image().width() - 1, 1));

    const double imageToDisplayScaleX =
        static_cast<double>(displayRect.width()) /
        static_cast<double>(
            (std::max)(image().width(), 1));

    const double displayScopeLeft =
        static_cast<double>(displayRect.left()) +
        renderedScopeLeft * imageToDisplayScaleX;

    const double displayScopeRight =
        static_cast<double>(displayRect.left()) +
        renderedScopeRight * imageToDisplayScaleX;

    const auto displayXToSourcePixels =
        [&](double displayX)
        {
            const double normalized =
                std::clamp(
                    (displayX - displayScopeLeft) /
                        (std::max)(
                            displayScopeRight - displayScopeLeft,
                            1.0),
                    0.0,
                    1.0);

            const double visibleSourceWidth =
                static_cast<double>(standard.sampleWidth) /
                static_cast<double>((std::max)(zoomFactor_, 1));

            const double maxScrollSourcePixels =
                (std::max)(
                    static_cast<double>(standard.sampleWidth) - visibleSourceWidth,
                    0.0);

            return
                scrollPosition_ * maxScrollSourcePixels +
                normalized * visibleSourceWidth;
        };

    if (measureActive_)
    {
        const QPointF startPoint =
            clampPointToRect(
                measureStartPosition_,
                displayRect);

        const QPointF endPoint =
            clampPointToRect(
                measureCurrentPosition_,
                displayRect);

        const QLineF measureLine(
            startPoint,
            endPoint);

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QColor measureColor(80, 255, 120);
        QPen measurePen(measureColor, 2.0);
        painter.setPen(measurePen);

        painter.drawLine(measureLine);

        const double lineLength =
            measureLine.length();

        if (lineLength > 0.001)
        {
            const QPointF direction(
                measureLine.dx() / lineLength,
                measureLine.dy() / lineLength);

            drawArrowHead(
                painter,
                startPoint,
                QPointF(-direction.x(), -direction.y()),
                12.0,
                5.0);

            drawArrowHead(
                painter,
                endPoint,
                direction,
                12.0,
                5.0);
        }

        const double startVolts =
            displayYToVolts(startPoint.y());

        const double endVolts =
            displayYToVolts(endPoint.y());

        const int deltaMillivolts =
            static_cast<int>(
                std::lround(
                    (endVolts - startVolts) * 1000.0));

        const double startSourcePixels =
            displayXToSourcePixels(startPoint.x());

        const double endSourcePixels =
            displayXToSourcePixels(endPoint.x());

        const double deltaSourcePixels =
            std::abs(
                endSourcePixels - startSourcePixels);

        const double deltaSeconds =
            deltaSourcePixels /
            standard.sampleClockHz;

        QString frequencyText =
            QStringLiteral("∞ MHz");

        if (deltaSeconds > 1.0e-12)
        {
            const double frequencyMHz =
                1.0 /
                (deltaSeconds * 1.0e6);

            frequencyText =
                QStringLiteral("%1 MHz")
                    .arg(frequencyMHz, 0, 'f', 3);
        }

        const QString deltaMillivoltsText =
            deltaMillivolts >= 0
            ? QStringLiteral("+%1").arg(deltaMillivolts)
            : QString::number(deltaMillivolts);

        const QString measureText =
            QStringLiteral("ΔV %1 mV   %2")
                .arg(deltaMillivoltsText)
                .arg(frequencyText);

        QFont labelFont = painter.font();
        labelFont.setBold(true);
        const double pointSize = labelFont.pointSizeF();
        if (pointSize > 0.0)
        {
            labelFont.setPointSizeF(pointSize * 1.2);
        }
        painter.setFont(labelFont);

        const QFontMetricsF labelMetrics(labelFont, painter.device());

        constexpr double kPaddingX = 10.0;
        constexpr double kPaddingY = 6.0;

        const QSizeF textSize(
            labelMetrics.horizontalAdvance(measureText),
            labelMetrics.height());

        QPointF labelCenter =
            (startPoint + endPoint) * 0.5;

        // Keep the label clear of the measurement arrow. Offset it
        // perpendicular to the A-B line, preferring the upper side.
        QPointF labelNormal(0.0, -1.0);

        if (lineLength > 0.001)
        {
            labelNormal =
            {
                -measureLine.dy() / lineLength,
                measureLine.dx() / lineLength
            };

            if (labelNormal.y() > 0.0)
            {
                labelNormal = -labelNormal;
            }
        }

        constexpr double kMeasureLabelGap = 34.0;

        labelCenter +=
            labelNormal * kMeasureLabelGap;

        QRectF labelRect(
            labelCenter.x() - (textSize.width() + 2.0 * kPaddingX) * 0.5,
            labelCenter.y() - (textSize.height() + 2.0 * kPaddingY) * 0.5,
            textSize.width() + 2.0 * kPaddingX,
            textSize.height() + 2.0 * kPaddingY);

        if (labelRect.left() < displayRect.left())
        {
            labelRect.moveLeft(displayRect.left());
        }
        if (labelRect.right() > displayRect.right())
        {
            labelRect.moveRight(displayRect.right());
        }
        if (labelRect.top() < displayRect.top())
        {
            labelRect.moveTop(displayRect.top());
        }
        if (labelRect.bottom() > displayRect.bottom())
        {
            labelRect.moveBottom(displayRect.bottom());
        }

        painter.setPen(QPen(measureColor, 1.5));
        painter.setBrush(QColor(0, 0, 0, 210));
        painter.drawRoundedRect(labelRect, 4.0, 4.0);
        painter.drawText(
            labelRect.adjusted(
                kPaddingX,
                kPaddingY,
                -kPaddingX,
                -kPaddingY),
            Qt::AlignCenter,
            measureText);

        painter.restore();
        return;
    }

    if (!hoverActive_ ||
        panActive_ ||
        !displayRect.contains(
            hoverPosition_.toPoint()))
    {
        return;
    }

    const double volts =
        displayYToVolts(
            hoverPosition_.y());

    const int millivolts =
        static_cast<int>(
            std::lround(
                volts * 1000.0));

    const double percent =
        (volts - kBlackLevelVolts) *
        100.0 /
        (kWhiteLevelVolts - kBlackLevelVolts);

    const QString text =
        QStringLiteral("%1 mV   %2")
            .arg(millivolts)
            .arg(formatPercent(percent));

    painter.save();

    QFont font =
        painter.font();

    font.setBold(true);

    const double pointSize =
        font.pointSizeF();

    if (pointSize > 0.0)
    {
        font.setPointSizeF(
            pointSize * 1.35);
    }

    painter.setFont(font);

    const QFontMetricsF metrics(
        font,
        painter.device());

    constexpr double kPaddingX = 10.0;
    constexpr double kPaddingY = 6.0;
    constexpr double kCursorGap = 12.0;

    const QSizeF textSize(
        metrics.horizontalAdvance(text),
        metrics.height());

    QRectF labelRect(
        hoverPosition_.x() + kCursorGap,
        hoverPosition_.y() -
            textSize.height() * 0.5 -
            kPaddingY,
        textSize.width() +
            2.0 * kPaddingX,
        textSize.height() +
            2.0 * kPaddingY);

    if (labelRect.right() > displayRect.right())
    {
        labelRect.moveRight(
            hoverPosition_.x() - kCursorGap);
    }

    if (labelRect.top() < displayRect.top())
    {
        labelRect.moveTop(
            static_cast<double>(displayRect.top()));
    }

    if (labelRect.bottom() > displayRect.bottom())
    {
        labelRect.moveBottom(
            static_cast<double>(displayRect.bottom()));
    }

    painter.setPen(
        QPen(
            QColor(80, 170, 255),
            1.5));

    painter.setBrush(
        QColor(0, 0, 0, 210));

    painter.drawRoundedRect(
        labelRect,
        4.0,
        4.0);

    painter.drawText(
        labelRect.adjusted(
            kPaddingX,
            kPaddingY,
            -kPaddingX,
            -kPaddingY),
        Qt::AlignCenter,
        text);

    painter.restore();
}

void WaveformWidget::resizeEvent(
    QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    emitOutputSize();
}
