#pragma once

#include "video/Yuv444Frame.h"

#include <QImage>

class VectorscopeAnalyzer
{
public:
    VectorscopeAnalyzer();
    void setSelectedLine(int line);

    void setOutputSize(
        int width,
        int height);

    void analyze(const Yuv444Frame& frame);

    const QImage& image() const;

private:
    QImage image_;
    int selectedLine_ = -1;
};