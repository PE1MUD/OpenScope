#pragma once

#include "standards/VideoStandard.h"

#include <QFont>
#include <QPaintDevice>
#include <QPainter>
#include <QRectF>

class WaveformGraticule
{
public:
    void draw(
        QPainter& painter,
        const QRectF& scopeRect,
        VideoStandard standard) const;

    QFont labelFont(
        const QFont& baseFont,
        double scopeHeight) const;

    double leftInset(
        const QFont& baseFont,
        const QPaintDevice* device,
        double scopeHeight) const;
};