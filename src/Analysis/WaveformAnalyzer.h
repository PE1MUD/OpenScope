#pragma once

#include <cstdint>
#include <vector>
#include "Analyzer.h"

#include <QImage>

class WaveformAnalyzer final : public Analyzer
{
public:
    void setSelectedLine(int line);
    WaveformAnalyzer();

    void analyze(const Yuv444Frame& frame) override;
    void setPersistence(int persistence);

    const QImage& image() const;

private:
    QImage image_;
    std::vector<std::uint32_t> hits_;
    std::vector<std::uint16_t> trace_;
    int selectedLine_ = -1;
    int persistence_ = 0;
    void plotBeam(
        int x,
        double y,
        int intensity = 255);
};