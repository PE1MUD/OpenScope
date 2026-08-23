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
#include <functional>
#include <vector>

class QPainter;


// Minimum horizontal pixels per cycle for a visually pleasing waveform.
inline constexpr double kPixelsPerCycleForTraceBW = 6.0;

struct WaveformSettings
{
    int fillDensity = 0;
    bool color = false;
};

struct WaveformRenderTimings
{
    std::uint64_t persistenceUs = 0;
    std::uint64_t traceUs = 0;
    std::uint64_t tracePrepUs = 0;
    std::uint64_t traceRasterUs = 0;
    std::uint64_t composeUs = 0;
    std::uint64_t glowUs = 0;
    std::uint64_t overlayUs = 0;
    bool traceParallel = false;
    bool outputSizeChanged = false;
    bool outputBufferCapacityGrew = false;
    bool resamplerCacheRebuilt = false;
    std::uint32_t traceJobCount = 0;
    double beamCoreRadiusPx = 0.0;
    std::int32_t beamCoreMarginPx = 0;
    std::uint32_t glowDirtyTiles = 0;
    std::uint32_t glowTotalTiles = 0;
    std::uint32_t glowHorizontalPass1Tiles = 0;
    std::uint32_t glowVerticalPass1Tiles = 0;
    std::uint32_t glowHorizontalPass2Tiles = 0;
    std::uint32_t glowVerticalPass2Tiles = 0;
    std::int32_t glowActiveX = 0;
    std::int32_t glowActiveY = 0;
    std::int32_t glowActiveWidth = 0;
    std::int32_t glowActiveHeight = 0;
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
    void setContentScale(
        double horizontalScale,
        double verticalScale);

    void setFitAspectRatio(bool enabled);

    [[nodiscard]] double traceBandwidthMHz() const;
    [[nodiscard]] const QImage& image() const;
    [[nodiscard]] const std::vector<float>& visibleLumaVolts() const noexcept;
    [[nodiscard]] const std::vector<float>& fullLumaVolts() const noexcept;
    [[nodiscard]] const WaveformRenderTimings& renderTimings() const noexcept;

    void setChromaFillIntensity(
        int intensity);

    void setColor(bool enabled);

    using TraceJobExecutor = std::function<void(
        std::size_t,
        const std::function<void(std::size_t)>&)>;

    void setTraceJobExecutor(TraceJobExecutor executor);
    void setLineInfoOverlayEnabled(bool enabled, bool palOutput = false);

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
    void drawLineInfoOverlay(QPainter& painter);
    void plotLuminanceTraceRange(
        int firstPixelX,
        int lastPixelX);

    void plotBeam(
        double x,
        double y,
        int intensity,
        int red,
        int green,
        int blue,
        int clipFirstX = 0,
        int clipLastX = -1);

    void plotSegment(
        double x0,
        double y0,
        double x1,
        double y1,
        int intensity,
        int red,
        int green,
        int blue,
        int clipFirstX = 0,
        int clipLastX = -1);

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
    std::vector<float> fullLumaVolts_;

    std::array<std::uint8_t, 65536> displayLut_{};

    static constexpr int kLumaReconstructionRadius = 24;
    static constexpr float kLumaReconstructionCutoff = 1.00f;

    int selectedLine_ = -1;
    int persistence_ = 0;
    int glow_ = 50;
    double beamCoreRadiusPx_ = 0.82;

    int zoomFactor_ = 1;
    double scrollPosition_ = 0.0;
    double contentScaleX_ = 1.0;
    double contentScaleY_ = 1.0;
    bool fitAspectRatio_ = true;
    bool lineInfoOverlayEnabled_ = false;
    bool lineInfoOverlayPalOutput_ = false;
    int chromaFillIntensity_ = 64;

    TraceJobExecutor traceJobExecutor_;

    double inputSampleClockHz_ = 13'500'000.0;
    int inputSampleWidth_ = 720;

    WaveformSettings settings_;
    WaveformRenderTimings renderTimings_;
    bool outputSizeChangedSinceRender_ = false;
    bool outputBufferCapacityGrewSinceRender_ = false;

    OpenScopeSettings::AspectRatio aspectRatio_ =
        OpenScopeSettings::AspectRatio::Ratio16x9;
};