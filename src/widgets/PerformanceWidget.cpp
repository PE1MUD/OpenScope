#include "PerformanceWidget.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QCloseEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QToolTip>

namespace
{
    constexpr double kMaximumUs =
        80000.0;

    constexpr double kField1DeadlineUs =
        40000.0;

    constexpr double kField2DeadlineUs =
        60000.0;

    constexpr double kTimingToleranceUs =
        2000.0;

    enum class BarType
    {
        FieldTiming,
        Field1Ready,
        Field2Ready,
        Normal
    };

    struct Bar
    {
        const char* label;
        const PerformanceMetricSnapshot* metric;
        BarType type;
    };

    int xForUs(
        const QRect& barRect,
        double us)
    {
        const double normalized =
            std::clamp(
                us / kMaximumUs,
                0.0,
                1.0);

        return
            barRect.left() +
            static_cast<int>(
                std::lround(
                    normalized *
                    static_cast<double>(
                        barRect.width())));
    }

    void fillTimingBackground(
        QPainter& painter,
        const QRect& barRect,
        double greenEndUs,
        double yellowEndUs)
    {
        const int greenEnd =
            xForUs(
                barRect,
                greenEndUs);

        const int yellowEnd =
            xForUs(
                barRect,
                yellowEndUs);

        painter.fillRect(
            QRect(
                barRect.left(),
                barRect.top(),
                std::max(
                    greenEnd -
                    barRect.left(),
                    0),
                barRect.height()),
            QColor(30, 125, 48));

        painter.fillRect(
            QRect(
                greenEnd,
                barRect.top(),
                std::max(
                    yellowEnd -
                    greenEnd,
                    0),
                barRect.height()),
            QColor(155, 132, 35));

        painter.fillRect(
            QRect(
                yellowEnd,
                barRect.top(),
                std::max(
                    barRect.right() + 1 -
                    yellowEnd,
                    0),
                barRect.height()),
            QColor(145, 38, 38));
    }

    void fillFieldTimingBackground(
        QPainter& painter,
        const QRect& barRect)
    {
        painter.fillRect(
            barRect,
            QColor(28, 28, 28));

        const auto fillToleranceBand =
            [&](double targetUs)
            {
                const int left =
                    xForUs(
                        barRect,
                        targetUs -
                            kTimingToleranceUs);

                const int right =
                    xForUs(
                        barRect,
                        targetUs +
                            kTimingToleranceUs);

                painter.fillRect(
                    QRect(
                        left,
                        barRect.top(),
                        std::max(
                            right - left,
                            1),
                        barRect.height()),
                    QColor(30, 125, 48));

                const int targetX =
                    xForUs(
                        barRect,
                        targetUs);

                painter.setPen(
                    QColor(150, 150, 150));

                painter.drawLine(
                    targetX,
                    barRect.top() + 1,
                    targetX,
                    barRect.bottom() - 1);
            };

        fillToleranceBand(
            kField1DeadlineUs);

        fillToleranceBand(
            kField2DeadlineUs);
    }

    void drawFieldStatusMarker(
        QPainter& painter,
        const QRect& barRect,
        double valueUs,
        double targetUs)
    {
        const bool inRange =
            std::abs(
                valueUs - targetUs) <=
            kTimingToleranceUs;

        const int x =
            xForUs(
                barRect,
                targetUs);

        constexpr int markerSize = 14;

        const QRect markerRect(
            x - markerSize / 2,
            barRect.center().y() - markerSize / 2,
            markerSize,
            markerSize);

        QPen pen(
            inRange
            ? QColor(95, 215, 120)
            : QColor(235, 95, 95));

        pen.setWidth(2);
        pen.setCapStyle(
            Qt::RoundCap);
        pen.setJoinStyle(
            Qt::RoundJoin);

        painter.setPen(
            pen);

        if (inRange)
        {
            painter.drawLine(
                markerRect.left() + 2,
                markerRect.center().y(),
                markerRect.left() + 6,
                markerRect.bottom() - 2);

            painter.drawLine(
                markerRect.left() + 6,
                markerRect.bottom() - 2,
                markerRect.right() - 1,
                markerRect.top() + 2);
        }
        else
        {
            painter.drawLine(
                markerRect.left() + 2,
                markerRect.top() + 2,
                markerRect.right() - 2,
                markerRect.bottom() - 2);

            painter.drawLine(
                markerRect.right() - 2,
                markerRect.top() + 2,
                markerRect.left() + 2,
                markerRect.bottom() - 2);
        }
    }

    void drawSegment(
        QPainter& painter,
        const QRect& barRect,
        double startUs,
        double durationUs,
        const QColor& color,
        const char* shortLabel)
    {
        if (durationUs <= 0.0)
        {
            return;
        }

        const int left =
            xForUs(
                barRect,
                startUs);

        const int right =
            xForUs(
                barRect,
                startUs + durationUs);

        const int width =
            std::max(
                right - left,
                1);

        const QRect segmentRect(
            left,
            barRect.top() + 1,
            width,
            barRect.height() - 1);

        painter.fillRect(
            segmentRect,
            color);

        painter.setPen(
            QColor(35, 35, 35));

        painter.drawLine(
            segmentRect.topRight(),
            segmentRect.bottomRight());

        if (segmentRect.width() >= 11)
        {
            painter.setPen(
                Qt::black);

            painter.drawText(
                segmentRect,
                Qt::AlignCenter,
                QString::fromLatin1(
                    shortLabel));
        }
    }

    double nonNegativeRemainder(
        double readyUs,
        double knownUs)
    {
        return
            std::max(
                readyUs - knownUs,
                0.0);
    }
}

auto makeBars(
    const PerformanceSnapshot& snapshot)
{
    return std::array
    {
        Bar{
            "Field timing",
            nullptr,
            BarType::FieldTiming },

        Bar{
            "Field 1 ready @ 40ms",
            &snapshot.field1Ready,
            BarType::Field1Ready },

        Bar{
            "Field 2 ready @ 60ms",
            &snapshot.field2Ready,
            BarType::Field2Ready },

        Bar{
            "Waveform",
            &snapshot.waveform,
            BarType::Normal },

        Bar{
            "Vectorscope",
            &snapshot.vectorscope,
            BarType::Normal },

        Bar{
            "Display compose",
            &snapshot.displayCompose,
            BarType::Normal },
    };
}

PerformanceWidget::PerformanceWidget(
    QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(
        320,
        180);

    setMouseTracking(
        true);
}

void PerformanceWidget::setPerformanceSnapshot(
    const PerformanceSnapshot& snapshot)
{
    snapshot_ = snapshot;
    update();
}

void PerformanceWidget::paintEvent(
    QPaintEvent* /*event*/)
{
    QPainter painter(this);

    painter.fillRect(
        rect(),
        Qt::black);

    constexpr int barHeight =
        18;

    constexpr int spacing =
        12;

    const auto bars =
        makeBars(
            snapshot_);

    const QFontMetrics fontMetrics(
        painter.font());

    int labelWidth = 0;

    for (const Bar& bar : bars)
    {
        labelWidth =
            std::max(
                labelWidth,
                fontMetrics.horizontalAdvance(
                    bar.label));
    }

    labelWidth +=
        12;

    constexpr int margin =
        12;

    int y = margin;

    const int barLeft =
        margin + labelWidth;

    const int barWidth =
        width() -
        labelWidth -
        margin * 2;

    const int scaleY =
        y;

    painter.setPen(
        Qt::lightGray);

    const auto drawScaleLabel =
        [&](double us,
            const QString& text,
            Qt::Alignment alignment)
        {
            const int x =
                barLeft +
                static_cast<int>(
                    std::lround(
                        (us / kMaximumUs) *
                        static_cast<double>(
                            barWidth)));

            QRect textRect(
                x - 30,
                scaleY,
                60,
                barHeight);

            if (alignment & Qt::AlignLeft)
            {
                textRect.moveLeft(
                    x);
            }
            else if (alignment & Qt::AlignRight)
            {
                textRect.moveRight(
                    x);
            }

            painter.drawText(
                textRect,
                alignment |
                Qt::AlignVCenter,
                text);
        };

    drawScaleLabel(
        0.0,
        "0",
        Qt::AlignLeft);

    drawScaleLabel(
        20000.0,
        "20",
        Qt::AlignHCenter);

    drawScaleLabel(
        40000.0,
        "40",
        Qt::AlignHCenter);

    drawScaleLabel(
        60000.0,
        "60",
        Qt::AlignHCenter);

    drawScaleLabel(
        80000.0,
        "80 ms",
        Qt::AlignRight);

    y +=
        barHeight +
        6;

    for (const Bar& bar : bars)
    {
        painter.setPen(
            Qt::white);

        painter.drawText(
            QRect(
                margin,
                y,
                labelWidth,
                barHeight),
            Qt::AlignVCenter |
            Qt::AlignLeft,
            bar.label);

        const QRect barRect(
            margin + labelWidth,
            y,
            width() -
            labelWidth -
            margin * 2,
            barHeight);

        switch (bar.type)
        {
        case BarType::FieldTiming:
            fillFieldTimingBackground(
                painter,
                barRect);
            break;

        case BarType::Field1Ready:
            fillTimingBackground(
                painter,
                barRect,
                35000.0,
                kField1DeadlineUs);
            break;

        case BarType::Field2Ready:
            fillTimingBackground(
                painter,
                barRect,
                55000.0,
                kField2DeadlineUs);
            break;

        case BarType::Normal:
        default:
            fillTimingBackground(
                painter,
                barRect,
                40000.0,
                60000.0);
            break;
        }

        if (bar.type == BarType::FieldTiming)
        {
            drawFieldStatusMarker(
                painter,
                barRect,
                static_cast<double>(
                    snapshot_.field1Present.latestUs),
                kField1DeadlineUs);

            drawFieldStatusMarker(
                painter,
                barRect,
                static_cast<double>(
                    snapshot_.field2Present.latestUs),
                kField2DeadlineUs);
        }
        else if (bar.type == BarType::Field1Ready ||
            bar.type == BarType::Field2Ready)
        {
            const double noiseUs =
                static_cast<double>(
                    snapshot_.noiseReduction.latestUs);

            const double deinterlaceUs =
                static_cast<double>(
                    snapshot_.deinterlace.latestUs);

            const double firstConvertUs =
                static_cast<double>(
                    snapshot_.displayFirst.latestUs);

            const double secondConvertUs =
                bar.type == BarType::Field2Ready
                ? static_cast<double>(
                    snapshot_.displaySecond.latestUs)
                : 0.0;

            const double convertUs =
                firstConvertUs +
                secondConvertUs;

            const double readyUs =
                static_cast<double>(
                    bar.metric->latestUs);

            const double knownUs =
                noiseUs +
                deinterlaceUs +
                convertUs;

            const double overheadUs =
                nonNegativeRemainder(
                    readyUs,
                    knownUs);

            double startUs = 0.0;

            drawSegment(
                painter,
                barRect,
                startUs,
                noiseUs,
                QColor(185, 225, 205),
                "N");

            startUs +=
                noiseUs;

            drawSegment(
                painter,
                barRect,
                startUs,
                deinterlaceUs,
                QColor(185, 215, 235),
                "D");

            startUs +=
                deinterlaceUs;

            drawSegment(
                painter,
                barRect,
                startUs,
                convertUs,
                QColor(215, 200, 235),
                "C");

            startUs +=
                convertUs;

            drawSegment(
                painter,
                barRect,
                startUs,
                overheadUs,
                QColor(205, 205, 205),
                "O");
        }
        else
        {
            const int valueX =
                xForUs(
                    barRect,
                    static_cast<double>(
                        bar.metric->latestUs));

            const int valueWidth =
                std::max(
                    valueX -
                    barRect.left(),
                    0);

            painter.fillRect(
                QRect(
                    barRect.left(),
                    barRect.top(),
                    valueWidth,
                    barRect.height()),
                QColor(220, 220, 220));
        }

        painter.setPen(
            Qt::gray);

        painter.drawRect(
            barRect);

        y +=
            barHeight +
            spacing;
    }

    painter.setPen(
        Qt::lightGray);

    painter.drawText(
        QRect(
            margin,
            y,
            width() - margin * 2,
            barHeight),
        Qt::AlignLeft |
        Qt::AlignVCenter,
        QString(
            "Deadline misses: F1 %1   F2 %2")
        .arg(
            snapshot_.field1DeadlineMisses)
        .arg(
            snapshot_.field2DeadlineMisses));
}

void PerformanceWidget::mouseMoveEvent(
    QMouseEvent* event)
{
    constexpr int barHeight = 18;
    constexpr int spacing = 12;
    constexpr int margin = 12;

    constexpr int scaleHeight =
        barHeight + 6;

    const int firstBarY =
        margin +
        scaleHeight;

    const int relativeY =
        event->position().toPoint().y() -
        firstBarY;

    if (relativeY < 0)
    {
        QToolTip::hideText();
        return;
    }

    const int rowHeight =
        barHeight +
        spacing;

    const int index =
        relativeY /
        rowHeight;

    const auto bars =
        makeBars(
            snapshot_);

    if (index < 0 ||
        index >= static_cast<int>(
            bars.size()) ||
        (relativeY % rowHeight) >=
            barHeight)
    {
        QToolTip::hideText();
        return;
    }

    const Bar& bar =
        bars[index];

    QString valueText;

    if (bar.type == BarType::FieldTiming)
    {
        const double field1PresentMs =
            snapshot_.field1Present.latestMs();

        const double field2PresentMs =
            snapshot_.field2Present.latestMs();

        const double presentIntervalMs =
            snapshot_.presentInterval.latestMs();

        const double field1DeltaMs =
            field1PresentMs - 40.0;

        const double field2DeltaMs =
            field2PresentMs - 60.0;

        const bool field1InRange =
            std::abs(field1DeltaMs) <= 2.0;

        const bool field2InRange =
            std::abs(field2DeltaMs) <= 2.0;

        valueText =
            QString(
                "Field 1 render: %1ms\n"
                "Target: 40ms   Delta: %2%3ms   %4\n\n"
                "Field 2 render: %5ms\n"
                "Target: 60ms   Delta: %6%7ms   %8\n\n"
                "F1 -> F2 interval: %9ms")
            .arg(
                field1PresentMs,
                0,
                'f',
                2)
            .arg(
                field1DeltaMs >= 0.0
                ? "+"
                : "")
            .arg(
                field1DeltaMs,
                0,
                'f',
                2)
            .arg(
                field1InRange
                ? "OK"
                : "OUT")
            .arg(
                field2PresentMs,
                0,
                'f',
                2)
            .arg(
                field2DeltaMs >= 0.0
                ? "+"
                : "")
            .arg(
                field2DeltaMs,
                0,
                'f',
                2)
            .arg(
                field2InRange
                ? "OK"
                : "OUT")
            .arg(
                presentIntervalMs,
                0,
                'f',
                2);
    }
    else if (bar.type == BarType::Field1Ready ||
        bar.type == BarType::Field2Ready)
    {
        const double readyMs =
            bar.metric->latestMs();

        const double noiseMs =
            snapshot_.noiseReduction.latestMs();

        const double deinterlaceMs =
            snapshot_.deinterlace.latestMs();

        const double firstConvertMs =
            snapshot_.displayFirst.latestMs();

        const double secondConvertMs =
            bar.type == BarType::Field2Ready
            ? snapshot_.displaySecond.latestMs()
            : 0.0;

        const double convertMs =
            firstConvertMs +
            secondConvertMs;

        const double overheadMs =
            std::max(
                readyMs -
                noiseMs -
                deinterlaceMs -
                convertMs,
                0.0);

        const double deadlineMs =
            bar.type == BarType::Field1Ready
            ? 40.0
            : 60.0;

        const double marginMs =
            deadlineMs -
            readyMs;

        valueText =
            QString(
                "%1: %2ms\n"
                "N  Noise reduction: %3ms\n"
                "D  Deinterlace: %4ms\n"
                "C  Convert: %5ms\n"
                "O  Other / overhead: %6ms")
            .arg(
                bar.type == BarType::Field1Ready
                ? "Field 1 ready"
                : "Field 2 ready")
            .arg(
                readyMs,
                0,
                'f',
                2)
            .arg(
                noiseMs,
                0,
                'f',
                2)
            .arg(
                deinterlaceMs,
                0,
                'f',
                2)
            .arg(
                convertMs,
                0,
                'f',
                2)
            .arg(
                overheadMs,
                0,
                'f',
                2);

        if (bar.type == BarType::Field2Ready)
        {
            valueText +=
                QString(
                    "\n   Field 1 convert: %1ms"
                    "\n   Field 2 convert: %2ms")
                .arg(
                    firstConvertMs,
                    0,
                    'f',
                    2)
                .arg(
                    secondConvertMs,
                    0,
                    'f',
                    2);
        }

        valueText +=
            QString(
                "\n\nDeadline: %1ms"
                "\nMargin: %2%3ms")
            .arg(
                deadlineMs,
                0,
                'f',
                0)
            .arg(
                marginMs >= 0.0
                ? "+"
                : "")
            .arg(
                marginMs,
                0,
                'f',
                2);
    }
    else
    {
        valueText =
            QString("%1: %2ms")
            .arg(
                QString::fromLatin1(
                    bar.label))
            .arg(
                bar.metric->latestMs(),
                0,
                'f',
                2);
    }

    QToolTip::showText(
        event->globalPosition().toPoint(),
        valueText,
        this);
}

void PerformanceWidget::closeEvent(
    QCloseEvent* event)
{
    emit visibilityChanged(
        false);

    QWidget::closeEvent(
        event);
}

QSize PerformanceWidget::sizeHint() const
{
    constexpr int barHeight = 18;
    constexpr int spacing = 12;
    constexpr int margin = 12;

    constexpr int scaleHeight =
        barHeight + 6;

    const auto bars =
        makeBars(
            snapshot_);

    const int barCount =
        static_cast<int>(
            bars.size());

    const int height =
        margin +
        scaleHeight +
        barCount * barHeight +
        (barCount - 1) * spacing +
        barHeight +
        spacing +
        margin;

    return QSize(
        520,
        height);
}
