#include "VectorscopeGraticule.h"
#include "standards/ColorBars.h"
#include "standards/ColorMatrices.h"
#include <QColor>
#include <QPen>

#include <cmath>
#include <iterator>
#include <numbers>

namespace
{
    struct Target
    {
        ColorBar color;
        double cb;
        double cr;
    };
    constexpr Target makeTarget(
        ColorBar color,
        ColorBarLevel level,
        VideoColorStandard standard)
    {
        const CbCr cbcr =
            rgbToCbCr(
                colorBar(
                    color,
                    level),
                standard);

        return
        {
            color,
            cbcr.cb,
            cbcr.cr
        };
    }

    constexpr Target kTargets75[] =
    {
        makeTarget(
            ColorBar::Red,
            ColorBarLevel::Percent75,
            VideoColorStandard::Rec601_625),

        makeTarget(
            ColorBar::Magenta,
            ColorBarLevel::Percent75,
            VideoColorStandard::Rec601_625),

        makeTarget(
            ColorBar::Blue,
            ColorBarLevel::Percent75,
            VideoColorStandard::Rec601_625),

        makeTarget(
            ColorBar::Cyan,
            ColorBarLevel::Percent75,
            VideoColorStandard::Rec601_625),

        makeTarget(
            ColorBar::Green,
            ColorBarLevel::Percent75,
            VideoColorStandard::Rec601_625),

        makeTarget(
            ColorBar::Yellow,
            ColorBarLevel::Percent75,
            VideoColorStandard::Rec601_625)
    };
    constexpr Target kTargets100[] =
    {
        makeTarget(
            ColorBar::Red,
            ColorBarLevel::Percent100,
            VideoColorStandard::Rec601_625),

        makeTarget(
            ColorBar::Magenta,
            ColorBarLevel::Percent100,
            VideoColorStandard::Rec601_625),

        makeTarget(
            ColorBar::Blue,
            ColorBarLevel::Percent100,
            VideoColorStandard::Rec601_625),

        makeTarget(
            ColorBar::Cyan,
            ColorBarLevel::Percent100,
            VideoColorStandard::Rec601_625),

        makeTarget(
            ColorBar::Green,
            ColorBarLevel::Percent100,
            VideoColorStandard::Rec601_625),

        makeTarget(
            ColorBar::Yellow,
            ColorBarLevel::Percent100,
            VideoColorStandard::Rec601_625)
    };
    constexpr double kTargetBoxSize = 18.0;

    void drawTargets(
        QPainter& painter,
        const QPointF& center,
        double radius,
        const Target* targets,
        std::size_t targetCount)
    {
        QPen targetPen(
            QColor(120, 115, 80),
            1.0);

        painter.setPen(targetPen);
        painter.setBrush(Qt::NoBrush);

        for (std::size_t i = 0;
            i < targetCount;
            ++i)
        {
            const Target& target =
                targets[i];

            constexpr double kCbCrFullScale = 0.5;

            const QPointF position(
                center.x() +
                (target.cb / kCbCrFullScale) * radius,
                center.y() -
                (target.cr / kCbCrFullScale) * radius);

            const double angleDegrees =
                std::atan2(
                    -target.cr,
                    target.cb) *
                180.0 /
                std::numbers::pi;

            painter.save();

            painter.translate(position);
            painter.rotate(
                angleDegrees + 90.0);

            const QRectF box(
                -kTargetBoxSize * 0.5,
                -kTargetBoxSize * 0.5,
                kTargetBoxSize,
                kTargetBoxSize);

            painter.drawRect(box);

            painter.restore();

            painter.drawText(
                QPointF(
                    position.x() + 12.0,
                    position.y() + 5.0),
                colorBarShortName(target.color));
        }
    }
}

void VectorscopeGraticule::drawAxes(
    QPainter& painter,
    const QPointF& center,
    double radius) const
{
    QPen graticulePen(
        QColor(90, 90, 70),
        1.0);

    painter.setPen(graticulePen);
    painter.setBrush(Qt::NoBrush);

    // Outer circle.
    painter.drawEllipse(
        center,
        radius,
        radius);

    // Horizontal axis.
    painter.drawLine(
        QPointF(
            center.x() - radius,
            center.y()),
        QPointF(
            center.x() + radius,
            center.y()));

    // Vertical axis.
    painter.drawLine(
        QPointF(
            center.x(),
            center.y() - radius),
        QPointF(
            center.x(),
            center.y() + radius));
}

void VectorscopeGraticule::draw(
    QPainter& painter,
    const QRectF& scopeRect) const
{
    const QPointF center =
        scopeRect.center();

    const double radius =
        scopeRect.width() * 0.45;

    drawAxes(
        painter,
        center,
        radius);

    drawTargets(
        painter,
        center,
        radius,
        kTargets75,
        std::size(kTargets75));

    drawTargets(
        painter,
        center,
        radius,
        kTargets100,
        std::size(kTargets100));
}