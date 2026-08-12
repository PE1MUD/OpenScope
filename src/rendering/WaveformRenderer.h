#pragma once

#include "analysis/Analyzer.h"
#include "video/ReconstructedLumaFrame.h"
#include "rendering/WaveformGraticule.h"
#include <QImage>
#include <QRectF>
#include <array>
#include <cstdint>
#include <vector>


// Minimum horizontal pixels per cycle for a visually pleasing waveform.
inline constexpr double kPixelsPerCycleForTraceBW = 6.0;

struct WaveformSettings
{
    int fillDensity = 0;
    bool color = false;
};

class WaveformRenderer final : public Analyzer
{
public:
    WaveformRenderer();

    void analyze(
        const Yuv444Frame& frame) override;

    void analyze(
        const Yuv444Frame& frame,
        const ReconstructedLumaFrame& reconstructedLuma);

    void setSelectedLine(int line);
    void setPersistence(int persistence);
    void setOutputSize(
        int width,
        int height);

    void setZoomed(bool zoomed);
    void setScrollPosition(double position);

    [[nodiscard]] double traceBandwidthMHz() const;
    [[nodiscard]] const QImage& image() const;

    void setChromaFillIntensity(
        int intensity);

    void setColor(bool enabled);

private:
    struct TracePixel
    {
        std::uint16_t red = 0;
        std::uint16_t green = 0;
        std::uint16_t blue = 0;
    };

    void clearOrFadeTrace();
    void clearTrace();
    void renderSingleLine(
        const Yuv444Frame& frame,
        const ReconstructedLumaFrame& reconstructedLuma);

    void renderAllLines(
        const Yuv444Frame& frame);

    void composeTraceImage();
    void plotLuminanceTrace();

    void plotBeam(
        double x,
        double y,
        int intensity,
        int red,
        int green,
        int blue);

    void plotSegment(
        double x0,
        double y0,
        double x1,
        double y1,
        int intensity,
        int red,
        int green,
        int blue);

    void addChromaFillPixel(
        int x,
        int y,
        int red,
        int green,
        int blue,
        int intensity);

    
    QRectF scaledScopeRect() const;
    QImage image_;

    WaveformGraticule graticule_;
    QRectF scopeRect() const;
    QRectF viewportRect() const;

    std::vector<std::uint32_t> hits_;
    std::vector<TracePixel> trace_;

    std::vector<float> sourceY_;
    std::vector<float> sourceU_;
    std::vector<float> sourceV_;
    std::vector<float> displayY_;
    std::vector<float> displayYMin_;
    std::vector<float> displayYMax_;
    std::vector<float> displayU_;
    std::vector<float> displayV_;

    std::array<std::uint8_t, 65536> displayLut_{};

    int selectedLine_ = -1;
    int persistence_ = 0;

    bool zoomed_ = false;
    double scrollPosition_ = 0.0;
    int chromaFillIntensity_ = 64;

    WaveformSettings settings_;
};