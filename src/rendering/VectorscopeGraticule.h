#pragma once

#include <QPainter>
#include <QRectF>

class VectorscopeGraticule
{
public:
    void draw(
        QPainter& painter,
        const QRectF& scopeRect) const;
};