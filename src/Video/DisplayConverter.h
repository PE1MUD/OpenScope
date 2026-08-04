#pragma once

#include "Yuv444Frame.h"

#include <QImage>

class DisplayConverter
{
public:
    QImage convert(const Yuv444Frame& frame) const;
};