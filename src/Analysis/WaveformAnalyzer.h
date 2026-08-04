#pragma once

#include "Analyzer.h"

#include <QImage>

class WaveformAnalyzer final : public Analyzer
{
public:
    WaveformAnalyzer();

    void analyze(const Yuv444Frame& frame) override;

    const QImage& image() const;

private:
    QImage image_;
};