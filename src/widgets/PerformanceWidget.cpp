#include "PerformanceWidget.h"
#include <array>
#include <algorithm>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QCloseEvent>

namespace
{
    struct Bar
    {
        const char* label;
        const PerformanceMetricSnapshot* metric;
    };
}

auto makeBars(
    const PerformanceSnapshot& snapshot)
{
    return std::array
    {
        Bar{ "Deinterlace total", &snapshot.deinterlace },
        Bar{ "Deint worker 0", &snapshot.deinterlaceWorker0 },
        Bar{ "Deint worker 1", &snapshot.deinterlaceWorker1 },
        Bar{ "Field 1 convert", &snapshot.displayFirst },
        Bar{ "Field 1 ready / 40", &snapshot.field1Ready },
        Bar{ "Field 1 margin", &snapshot.field1Margin },
        Bar{ "Field 2 convert", &snapshot.displaySecond },
        Bar{ "Field 2 ready / 60", &snapshot.field2Ready },
        Bar{ "Field 2 margin", &snapshot.field2Margin },
        Bar{ "Present interval", &snapshot.presentInterval },
        Bar{ "Waveform", &snapshot.waveform },
        Bar{ "Vectorscope", &snapshot.vectorscope },
        Bar{ "Display allocation", &snapshot.displayAllocation },
        Bar{ "Display setup", &snapshot.displaySetup },
        Bar{ "Display compose", &snapshot.displayCompose },
        Bar{ "Display interpolation", &snapshot.displayInterpolation },
        Bar{ "Display YUV-RGB", &snapshot.displayColorConversion },
        Bar{ "Display output", &snapshot.displayOutput },
    };
}

PerformanceWidget::PerformanceWidget(
    QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 180);
    setMouseTracking(true);
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

    constexpr double maximumUs =
        60000.0;

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

    painter.drawText(
        QRect(
            barLeft,
            scaleY,
            barWidth,
            barHeight),
        Qt::AlignLeft |
        Qt::AlignVCenter,
        "0");

    painter.drawText(
        QRect(
            barLeft +
            barWidth / 3 -
            20,
            scaleY,
            40,
            barHeight),
        Qt::AlignCenter,
        "20");

    painter.drawText(
        QRect(
            barLeft +
            (barWidth * 2) / 3 -
            20,
            scaleY,
            40,
            barHeight),
        Qt::AlignCenter,
        "40");

    painter.drawText(
        QRect(
            barLeft +
            barWidth -
            60,
            scaleY,
            60,
            barHeight),
        Qt::AlignRight |
        Qt::AlignVCenter,
        "60 ms");

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

        const int greenWidth =
            (barRect.width() * 2) / 3;

        const int yellowWidth =
            barRect.width() / 6;

        painter.fillRect(
            QRect(
                barRect.left(),
                barRect.top(),
                greenWidth,
                barRect.height()),
            QColor(0, 80, 0));

        painter.fillRect(
            QRect(
                barRect.left() + greenWidth,
                barRect.top(),
                yellowWidth,
                barRect.height()),
            QColor(100, 90, 0));

        painter.fillRect(
            QRect(
                barRect.left() +
                greenWidth +
                yellowWidth,
                barRect.top(),
                barRect.width() -
                greenWidth -
                yellowWidth,
                barRect.height()),
            QColor(100, 0, 0));

        const double normalized =
            std::clamp(
                static_cast<double>(
                    bar.metric->latestUs) /
                maximumUs,
                0.0,
                1.0);

        const int valueWidth =
            static_cast<int>(
                normalized *
                barRect.width());

        painter.fillRect(
            QRect(
                barRect.left(),
                barRect.top(),
                valueWidth,
                barRect.height()),
            QColor(220, 220, 220));

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
        margin + scaleHeight;

    const int relativeY =
        event->position().toPoint().y() -
        firstBarY;

    if (relativeY < 0)
    {
        QToolTip::hideText();
        return;
    }

    const int rowHeight =
        barHeight + spacing;

    const int index =
        relativeY / rowHeight;

    const auto bars =
        makeBars(
            snapshot_);

    if (index < 0 ||
        index >= static_cast<int>(bars.size()) ||
        (relativeY % rowHeight) >= barHeight)
    {
        QToolTip::hideText();
        return;
    }

    const Bar& bar =
        bars[index];

    QToolTip::showText(
        event->globalPosition().toPoint(),
        QString("%1: %2 ms")
        .arg(
            QString::fromLatin1(
                bar.label))
        .arg(
            bar.metric->latestMs(),
            0,
            'f',
            2),
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