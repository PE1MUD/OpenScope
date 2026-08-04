#pragma once

#include <cstdint>
#include <vector>
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
    std::vector<std::uint16_t> hits_;
};