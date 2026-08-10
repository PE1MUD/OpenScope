#include "WaveformGraticule.h"

#include <array>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr std::array<double, 8> kGraticuleLevels
    {
        0.0,
        0.2,
        0.3,
        0.4,
        0.6,
        0.8,
        1.0,
        1.2
    };
}

void WaveformGraticule::draw(
    QPainter& painter,
    const QRectF& scopeRect,
    VideoStandard standard) const
{
    const auto analog =
        analogLevels(
            standard.colorStandard);

    const auto voltsToY =
        [&](double volts)
        {
            return
                scopeRect.bottom() -
                volts *
                scopeRect.height() /
                analog.graticuleMaxVolts;
        };


    painter.save();

    QPen pen(
        QColor(
            180,
            150,
            50));

    pen.setWidth(1);

    painter.setPen(pen);

    const QFont font =
        labelFont(
            painter.font(),
            scopeRect.height());

    painter.setFont(font);

    const QFontMetricsF metrics(
        font,
        painter.device());

    const double labelHeight =
        metrics.height();

    const double labelWidth =
        metrics.horizontalAdvance("1.2");

    const double labelGap =
        metrics.horizontalAdvance(".");

    for (double volts : kGraticuleLevels)
    {
        const double y =
            voltsToY(volts);

        painter.drawLine(
            QPointF(
                scopeRect.left(),
                y),
            QPointF(
                scopeRect.right(),
                y));

        const QString label =
            QString::number(
                volts,
                'f',
                1);

        const QRectF labelRect(
            scopeRect.left() -
            labelGap -
            labelWidth,
            y - labelHeight * 0.5,
            labelWidth,
            labelHeight);

        painter.drawText(
            labelRect,
            Qt::AlignVCenter |
            Qt::AlignRight,
            label);
    }

    painter.restore();
}

QFont WaveformGraticule::labelFont(
    const QFont& baseFont,
    double scopeHeight) const
{
    QFont font =
        baseFont;

    font.setBold(true);

    constexpr double kFontHeightFraction = 0.045;

    const int pixelSize =
        std::max(
            1,
            static_cast<int>(
                std::lround(
                    scopeHeight *
                    kFontHeightFraction)));

    font.setPixelSize(
        pixelSize);

    return font;
}

double WaveformGraticule::leftInset(
    const QFont& baseFont,
    const QPaintDevice* device,
    double scopeHeight) const
{
    const QFont font =
        labelFont(
            baseFont,
            scopeHeight);

    const QFontMetricsF metrics(
        font,
        device);

    return
        metrics.horizontalAdvance("1.2") +
        metrics.horizontalAdvance(".");
}