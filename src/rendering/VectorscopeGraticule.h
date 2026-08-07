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

private:
    void drawAxes(
        QPainter& painter,
        const QPointF& center,
        double radius) const;
    VideoStandard videoStandard_ =
        VideoStandard::pal625();
};