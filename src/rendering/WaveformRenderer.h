#pragma once

#include "analysis/Analyzer.h"
#include "processing/LineResampler.h"

#include <QImage>

#include <cstdint>
#include <vector>

// Minimum horizontal pixels per cycle for a visually pleasing waveform.
static constexpr double kPixelsPerCycleForTraceBW = 6.0;

class WaveformRenderer final : public Analyzer
{
public:
    WaveformRenderer();

    void analyze(const Yuv444Frame& frame) override;

    void setSelectedLine(int line);
    void setPersistence(int persistence);
    void setOutputSize(int width, int height);
    double traceBandwidthMHz() const;
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

    LineResampler lineResampler_;

    std::vector<float> sourceY_;
    std::vector<float> sourceU_;
    std::vector<float> sourceV_;

    std::vector<float> displayY_;
    std::vector<float> displayU_;
    std::vector<float> displayV_;

    int selectedLine_ = -1;
    int persistence_ = 0;
};