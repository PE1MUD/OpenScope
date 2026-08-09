#pragma once
#include "standards/VideoStandard.h"
#include <QPainter>
#include <QRectF>

class VectorscopeGraticule
{
public:
    void draw(
        QPainter& painter,
        const QRectF& scopeRect) const;

    void setLineWidth(double width);
    void setScale(double scale);

private:
    void drawAxes(
        QPainter& painter,
        const QPointF& center,
        double radius) const;

    VideoStandard videoStandard_ =
        VideoStandard::pal625();

    double lineWidth_ = 1.0;
    double scale_ = 1.0;
};