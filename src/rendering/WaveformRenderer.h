#pragma once

#include "analysis/Analyzer.h"
#include <QImage>
#include "processing/SignalReconstructor.h"

#include <array>
#include <cstdint>
#include <vector>

// Minimum horizontal pixels per cycle for a visually pleasing waveform.
inline constexpr double kPixelsPerCycleForTraceBW = 6.0;


class WaveformRenderer final : public Analyzer
{
public:
    WaveformRenderer();

    void analyze(const Yuv444Frame& frame) override;

    void setSelectedLine(int line);
    void setPersistence(int persistence);
    void setOutputSize(int width, int height);

    [[nodiscard]] double traceBandwidthMHz() const;
    [[nodiscard]] const QImage& image() const;

private:
    struct TracePixel
    {
        std::uint16_t red = 0;
        std::uint16_t green = 0;
        std::uint16_t blue = 0;
    };
    LineResampler lineResampler_;
    void clearOrFadeTrace();
    void renderSingleLine(const Yuv444Frame& frame);
    void renderAllLines(const Yuv444Frame& frame);
    void composeTraceImage();

    void plotBeam(
        int x,
        double y,
        int intensity,
        int red,
        int green,
        int blue);

    QImage image_;

    std::vector<std::uint32_t> hits_;
    std::vector<TracePixel> trace_;

    std::vector<float> sourceY_;
    std::vector<float> sourceU_;
    std::vector<float> sourceV_;

    std::vector<float> displayY_;
    std::vector<float> displayU_;
    std::vector<float> displayV_;

    std::array<std::uint8_t, 65536> displayLut_{};

    int selectedLine_ = -1;
    int persistence_ = 0;
};
