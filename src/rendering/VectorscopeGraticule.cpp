#include "VectorscopeGraticule.h"

#include <QColor>
#include <QPen>

#include <cmath>
#include <numbers>

void VectorscopeGraticule::draw(
    QPainter& painter,
    const QRectF& scopeRect) const
{
    QPen graticulePen(
        QColor(90, 90, 70),
        1.0);

    painter.setPen(graticulePen);
    painter.setBrush(Qt::NoBrush);

    const QPointF center =
        scopeRect.center();

    const double radius =
        scopeRect.width() * 0.45;

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

    struct Target
    {
        const char* label;
        double u;
        double v;
    };

    /*
     * 75% PAL/Rec.601 colour-bar targets.
     * Coordinates are normalized Cb/Cr values:
     * -1.0 .. +1.0 corresponds to the graticule radius.
     */
    const Target targets[] =
    {
        { "R",  -0.253,  0.750 },
        { "Mg",  0.497,  0.628 },
        { "B",   0.750, -0.122 },
        { "Cy",  0.253, -0.750 },
        { "G",  -0.497, -0.628 },
        { "Yl", -0.750,  0.122 }
    };

    QPen targetPen(
        QColor(120, 115, 80),
        1.0);

    painter.setPen(targetPen);

    constexpr double boxSize = 18.0;

    for (const Target& target : targets)
    {
        const QPointF position(
            center.x() + target.u * radius,
            center.y() - target.v * radius);

        const double angleDegrees =
            std::atan2(-target.v, target.u) *
            180.0 /
            std::numbers::pi;

        painter.save();

        painter.translate(position);
        painter.rotate(angleDegrees + 90.0);

        const QRectF box(
            -boxSize * 0.5,
            -boxSize * 0.5,
            boxSize,
            boxSize);

        painter.drawRect(box);

        painter.restore();

        painter.drawText(
            QPointF(
                position.x() + 12.0,
                position.y() + 5.0),
            target.label);
    }
}