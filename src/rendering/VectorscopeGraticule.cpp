#include "VectorscopeGraticule.h"
#include "VectorscopeSettings.h"
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
        VectorscopeSettings::smallToleranceHueDegrees,
        VectorscopeSettings::smallToleranceAmplitudePercent
    };

    constexpr TargetTolerance kLargeTolerance
    {
        VectorscopeSettings::largeToleranceHueDegrees,
        VectorscopeSettings::largeToleranceAmplitudePercent
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



            drawCorner(
                p1,
                p2,
                p4);

            drawCorner(
                p2,
                p1,
                p3);

            drawCorner(
                p3,
                p2,
                p4);

            drawCorner(
                p4,
                p1,
                p3);
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

    QColor targetColor(
        ColorBar color,
        ColorBarLevel level)
    {
        const Rgb rgb =
            colorBar(
                color,
                level);

        return QColor(
            static_cast<int>(rgb.r * 255.0),
            static_cast<int>(rgb.g * 255.0),
            static_cast<int>(rgb.b * 255.0));
    }


    void drawTargets(
        QPainter& painter,
        const QPointF& center,
        double radius,
        const Target* targets,
        std::size_t targetCount,
        ColorBarLevel level,
        double lineWidth)
    {
        painter.setBrush(Qt::NoBrush);
        QFont labelFont =
            painter.font();

        labelFont.setPixelSize(
            static_cast<int>(
                radius *
                VectorscopeSettings::targetLabelSizeFraction));

        painter.setFont(labelFont);

        for (std::size_t i = 0;
            i < targetCount;
            ++i)
        {
            const Target& target =
                targets[i];

            painter.setPen(
                QPen(
                    targetColor(
                        target.color,
                        level),
                    lineWidth));
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
            
            painter.setPen(
                QPen(
                    QColor(135, 135, 125),
                    lineWidth));

            if (level == ColorBarLevel::Percent100)
            {
                const QPointF radialDirection =
                    (position - center) /
                    std::hypot(
                        position.x() - center.x(),
                        position.y() - center.y());

                QPointF tangentialDirection(
                    -radialDirection.y(),
                    radialDirection.x());

                if (position.x() < center.x())
                {
                    tangentialDirection = -tangentialDirection;
                }

                const QPointF labelPosition =
                    position +
                    tangentialDirection *
                    (radius *
                        VectorscopeSettings::targetLabelOffsetFraction) +
                    radialDirection *
                    (radius *
                        VectorscopeSettings::targetLabelRadialOffsetFraction);

                const QString label =
                    colorBarShortName(target.color);

                const QFontMetricsF metrics(
                    painter.font());

                const QRectF textBounds =
                    metrics.boundingRect(label);

                painter.drawText(
                    QPointF(
                        labelPosition.x() -
                        textBounds.width() * 0.5,
                        labelPosition.y() +
                        textBounds.height() * 0.5),
                    label);
            }
        }
    }
}

void VectorscopeGraticule::setScale(double scale)
{
    scale_ = scale;
}

void VectorscopeGraticule::setLineWidth(double width)
{
    lineWidth_ = width;
}

void VectorscopeGraticule::drawAxes(
    QPainter& painter,
    const QPointF& center,
    double radius) const
{
    const double labelOffset =
        radius *
        VectorscopeSettings::axisLabelOffsetFraction;

    QPen graticulePen(
        QColor(135, 135, 125),
        lineWidth_);

    painter.setPen(graticulePen);
    painter.setBrush(Qt::NoBrush);

    // Outer circle.
    painter.drawEllipse(
        center,
        radius,
        radius);

    for (int angle = 0;
        angle < 360;
        angle += 10)
    {
        const double angleRadians =
            static_cast<double>(angle) *
            std::numbers::pi /
            180.0;

        const QPointF direction(
            std::cos(angleRadians),
            -std::sin(angleRadians));

        const QPointF outer =
            center +
            direction * radius;

        const double tickLength =
            radius *
            VectorscopeSettings::degreeTickLengthFraction *
            ((angle % 30 == 0) ? 2.0 : 1.0);

        const QPointF inner =
            center +
            direction *
            (radius - tickLength);

        painter.drawLine(
            inner,
            outer);
    }

    painter.setPen(
        QPen(
            QColor(90, 90, 85),
            lineWidth_));

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

    QFont axisFont =
        painter.font();
    axisFont.setBold(true);

    axisFont.setPixelSize(
        static_cast<int>(
            radius *
            VectorscopeSettings::axisLabelSizeFraction));

    painter.setFont(axisFont);

    const QFontMetricsF metrics(
        painter.font());

    const QRectF uBounds =
        metrics.boundingRect("U");

    const QRectF vBounds =
        metrics.boundingRect("V");

    const QRectF uRect(
        center.x() +
        radius -
        labelOffset -
        uBounds.width(),
        center.y() -
        labelOffset -
        uBounds.height(),
        uBounds.width(),
        uBounds.height());;

    painter.drawText(
        uRect,
        Qt::AlignCenter,
        "U");

    const QRectF vRect(
        center.x() +
        labelOffset,
        center.y() -
        radius +
        labelOffset,
        vBounds.width(),
        vBounds.height());

    painter.drawText(
        vRect,
        Qt::AlignCenter,
        "V");
}

void VectorscopeGraticule::draw(
    QPainter& painter,
    const QRectF& scopeRect) const
{
    const QPointF center =
        scopeRect.center();

    const double radius =
        scopeRect.width() * 0.45 * scale_;
    
    const double axesRadius =
        radius * 1.2;

    drawAxes(
        painter,
        center,
        axesRadius);

    drawTargets(
        painter,
        center,
        radius,
        kTargets75,
        std::size(kTargets75),
        ColorBarLevel::Percent75,
        lineWidth_);

    drawTargets(
        painter,
        center,
        radius,
        kTargets100,
        std::size(kTargets100),
        ColorBarLevel::Percent100,
        lineWidth_);
}