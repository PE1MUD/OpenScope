#pragma once

#include "analysis/Analyzer.h"
#include "video/ReconstructedLumaFrame.h"
#include "rendering/WaveformGraticule.h"
#include "processing/SignalReconstructor.h"
#include "settings/OpenScopeSettings.h"

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

    void setSelectedLine(int line);
    void setPersistence(int persistence);
    void setGlow(int glow);
    void setOutputSize(
        int width,
        int height);

    void setZoomed(bool zoomed);
    void setZoomFactor(int factor);
    void setScrollPosition(double position);
    void setContentScale(double scale);

    [[nodiscard]] double traceBandwidthMHz() const;
    [[nodiscard]] const QImage& image() const;

    void setChromaFillIntensity(
        int intensity);

    void setColor(bool enabled);

    void setAspectRatio(
        OpenScopeSettings::AspectRatio aspectRatio);

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
        const Yuv444Frame& frame);

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

    LineResampler singleLineReconstructor_{
    kLumaReconstructionRadius,
    kLumaReconstructionCutoff
    };

    QRectF scaledScopeRect() const;
    QImage image_;

    WaveformGraticule graticule_;
    QRectF scopeRect() const;
    QRectF viewportRect() const;

    //    LineResampler singleLineReconstructor_;

    std::vector<std::uint32_t> hits_;
    std::vector<float> allLinesPersistence_;
    std::vector<TracePixel> trace_;
    std::vector<TracePixel> chromaTrace_;

    std::vector<float> sourceY_;
    std::vector<float> sourceU_;
    std::vector<float> sourceV_;
    std::vector<float> displayY_;
    std::vector<float> displayYMin_;
    std::vector<float> displayYMax_;
    std::vector<float> displayU_;
    std::vector<float> displayV_;

    std::vector<float> singleLineSource_;
    std::vector<float> singleLineReconstructed_;

    std::array<std::uint8_t, 65536> displayLut_{};

    static constexpr int kLumaReconstructionRadius = 24;
    static constexpr float kLumaReconstructionCutoff = 1.00f;

    int selectedLine_ = -1;
    int persistence_ = 0;
    int glow_ = 50;

    int zoomFactor_ = 1;
    double scrollPosition_ = 0.0;
    double contentScale_ = 1.0;
    int chromaFillIntensity_ = 64;

    WaveformSettings settings_;

    OpenScopeSettings::AspectRatio aspectRatio_ =
        OpenScopeSettings::AspectRatio::Ratio16x9;
};