#include "WaveformGraticule.h"

#include <array>
#include <algorithm>
#include <cmath>

namespace
{
    struct GraticuleLevel
    {
        double volts;
        bool showLabel;
        bool majorLine;
    };

    constexpr double kBlackLevelVolts = 0.3;
    constexpr double kWhiteLevelVolts = 1.0;
    constexpr double kActiveRangeVolts =
        kWhiteLevelVolts -
        kBlackLevelVolts;

    constexpr std::array<GraticuleLevel, 8> kGraticuleLevels
    {{
        { 0.0, true,  true  },
        { 0.2, true,  false },
        { 0.3, true,  true  },
        { kBlackLevelVolts + 0.20 * kActiveRangeVolts, false, false },
        { kBlackLevelVolts + 0.50 * kActiveRangeVolts, false, false },
        { kBlackLevelVolts + 0.80 * kActiveRangeVolts, false, false },
        { 1.0, true,  true  },
        { 1.2, true,  false }
    }};
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

    for (const GraticuleLevel& level : kGraticuleLevels)
    {
        const double y =
            voltsToY(level.volts);

        if (level.majorLine)
        {
            pen.setWidth(3);
            pen.setStyle(Qt::SolidLine);
            pen.setColor(
                QColor(
                    180,
                    150,
                    50,
                    255));
        }
        else
        {
            pen.setWidth(2);
            pen.setStyle(Qt::DotLine);
            pen.setColor(
                QColor(
                    180,
                    150,
                    50,
                    140));
        }

        painter.setPen(pen);

        painter.drawLine(
            QPointF(
                scopeRect.left(),
                y),
            QPointF(
                scopeRect.right(),
                y));

        if (level.showLabel)
        {
            const QString label =
                QString::number(
                    level.volts,
                    'f',
                    1);

            const QRectF labelRect(
                scopeRect.left() -
                labelGap -
                labelWidth,
                y - labelHeight * 0.5,
                labelWidth,
                labelHeight);

            QPen labelPen(
                QColor(
                    180,
                    150,
                    50,
                    255));

            painter.setPen(labelPen);

            painter.drawText(
                labelRect,
                Qt::AlignVCenter |
                Qt::AlignRight,
                label);
        }
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