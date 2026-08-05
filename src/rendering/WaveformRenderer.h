#pragma once

#include "analysis/Analyzer.h"

#include <QImage>

#include <cstdint>
#include <vector>

class WaveformRenderer final : public Analyzer
{
public:
    WaveformRenderer();

    void analyze(const Yuv444Frame& frame) override;

    void setSelectedLine(int line);
    void setPersistence(int persistence);

    const QImage& image() const;

private:
    void plotBeam(
        int x,
        double y,
        int intensity = 255,
        int red = 0,
        int green = 255,
        int blue = 0);

    QImage image_;

    std::vector<std::uint32_t> hits_;
    std::vector<std::uint16_t> traceRed_;
    std::vector<std::uint16_t> traceGreen_;
    std::vector<std::uint16_t> traceBlue_;
    std::vector<float> chroma_;

    int selectedLine_ = -1;
    int persistence_ = 0;
};