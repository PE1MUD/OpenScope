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
    struct PolarTarget
    {
        double radius;
        double angleDegrees;
    };

    struct TargetTolerance
    {
        double hueDegrees;
        double amplitudePercent;
    };

    constexpr TargetTolerance kSmallTolerance
    {
        3.0,
        5.0
    };

    constexpr TargetTolerance kLargeTolerance
    {
        10.0,
        20.0
    };
    inline PolarTarget toPolar(
        const Target& target)
    {
        const double radius =
            std::hypot(
                target.cb,
                target.cr);

        const double angleDegrees =
            std::atan2(
                target.cr,
                target.cb) *
            180.0 /
            std::numbers::pi;

        return
        {
            radius,
            angleDegrees
        };
    }
    QPointF polarPoint(
        const QPointF& center,
        double radius,
        double angleDegrees)
    {
        const double angleRadians =
            angleDegrees *
            std::numbers::pi /
            180.0;

        return QPointF(
            center.x() +
            std::cos(angleRadians) * radius,
            center.y() -
            std::sin(angleRadians) * radius);
    }
    void drawToleranceTarget(
        QPainter& painter,
        const QPointF& center,
        double scopeRadius,
        const Target& target,
        const TargetTolerance& tolerance)
    {
        constexpr double kCbCrFullScale = 0.5;

        const PolarTarget polar =
            toPolar(target);

        const double nominalRadius =
            (polar.radius / kCbCrFullScale) *
            scopeRadius;

        const double amplitudeFraction =
            tolerance.amplitudePercent /
            100.0;

        const double radiusMin =
            nominalRadius *
            (1.0 - amplitudeFraction);

        const double radiusMax =
            nominalRadius *
            (1.0 + amplitudeFraction);

        const double angleMinDegrees =
            polar.angleDegrees -
            tolerance.hueDegrees;

        const double angleMaxDegrees =
            polar.angleDegrees +
            tolerance.hueDegrees;

        const QPointF p1 =
            polarPoint(
                center,
                radiusMin,
                angleMinDegrees);

        const QPointF p2 =
            polarPoint(
                center,
                radiusMax,
                angleMinDegrees);

        const QPointF p3 =
            polarPoint(
                center,
                radiusMax,
                angleMaxDegrees);

        const QPointF p4 =
            polarPoint(
                center,
                radiusMin,
                angleMaxDegrees);

        //painter.drawLine(
        //    p1,
        //    p2);

        //painter.drawLine(
        //    p2,
        //    p3);

        //painter.drawLine(
        //    p3,
        //    p4);

        //painter.drawLine(
        //    p4,
        //    p1);
        const bool largeTolerance =
            tolerance.amplitudePercent > 10.0;

        if (!largeTolerance)
        {
            // Small tolerance: closed box

            painter.drawLine(
                p1,
                p2);

            painter.drawLine(
                p2,
                p3);

            painter.drawLine(
                p3,
                p4);

            painter.drawLine(
                p4,
                p1);
        }
        else
        {
            // Large tolerance: outer corner marker

            constexpr double kCornerFraction = 0.15;

            auto drawCorner =
                [&](const QPointF& corner,
                    const QPointF& a,
                    const QPointF& b)
                {
                    painter.drawLine(
                        corner,
                        corner +
                        (a - corner) *
                        kCornerFraction);

                    painter.drawLine(
                        corner,
                        corner +
                        (b - corner) *
                        kCornerFraction);
                };


            // Pick the outer corner of the tolerance box.

            const double direction =
                polar.radius >= 0.0
                ? 1.0
                : -1.0;


            if (polar.angleDegrees >= 0.0)
            {
                drawCorner(
                    p3,
                    p2,
                    p4);
            }
            else
            {
                drawCorner(
                    p1,
                    p2,
                    p4);
            }
        }
    }
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
            drawToleranceTarget(
                painter,
                center,
                radius,
                target,
                kLargeTolerance);
           
            drawToleranceTarget(
                painter,
                center,
                radius,
                target,
                kSmallTolerance);
            
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