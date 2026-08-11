#include "rendering/WaveformRenderer.h"
#include "processing/SignalReconstructor.h"
#include "standards/VideoStandard.h"

#include <QDebug>
#include <QtGlobal>
#include <QApplication>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
    constexpr double kMaximumSampleValue = 65535.0;
    constexpr double kChromaNoiseThresholdFraction =
        0.03;
    constexpr int kLuminanceBeamIntensity = 256;
    constexpr int kChromaBeamIntensity = 768;
    constexpr std::uint32_t kConnectorIntensity = 160u;
}

void WaveformRenderer::setChromaFillIntensity(
    int intensity)
{
    chromaFillIntensity_ =
        std::clamp(
            intensity,
            0,
            200);
}

void addFillPixel(
    QImage& image,
    int x,
    int y,
    int red,
    int green,
    int blue,
    int intensity)
{
    if (x < 0 ||
        x >= image.width() ||
        y < 0 ||
        y >= image.height())
    {
        return;
    }

    QRgb* scanLine =
        reinterpret_cast<QRgb*>(
            image.scanLine(y));

    const QRgb current =
        scanLine[x];

    const int newRed =
        std::min(
            255,
            qRed(current) +
            red * intensity / 255);

    const int newGreen =
        std::min(
            255,
            qGreen(current) +
            green * intensity / 255);

    const int newBlue =
        std::min(
            255,
            qBlue(current) +
            blue * intensity / 255);

    scanLine[x] =
        qRgb(
            newRed,
            newGreen,
            newBlue);
}

void resampleLinear(
    std::span<const float> input,
    std::span<float> output)
{
    if (output.empty())
    {
        return;
    }

    if (input.empty())
    {
        std::fill(
            output.begin(),
            output.end(),
            0.0f);

        return;
    }

    if (input.size() == 1)
    {
        std::fill(
            output.begin(),
            output.end(),
            input.front());

        return;
    }

    const float scale =
        static_cast<float>(input.size()) /
        static_cast<float>(output.size());

    for (std::size_t outputIndex = 0;
        outputIndex < output.size();
        ++outputIndex)
    {
        const float sourcePosition =
            (static_cast<float>(outputIndex) + 0.5f) *
            scale -
            0.5f;

        const int leftIndex =
            std::clamp(
                static_cast<int>(
                    std::floor(sourcePosition)),
                0,
                static_cast<int>(
                    input.size()) - 1);

        const int rightIndex =
            std::min(
                leftIndex + 1,
                static_cast<int>(
                    input.size()) - 1);

        const float fraction =
            std::clamp(
                sourcePosition -
                static_cast<float>(leftIndex),
                0.0f,
                1.0f);

        const float left =
            input[
                static_cast<std::size_t>(
                    leftIndex)];

        const float right =
            input[
                static_cast<std::size_t>(
                    rightIndex)];

        output[outputIndex] =
            left +
            (right - left) *
            fraction;
    }
}
void resampleMinMax(
    std::span<const float> input,
    std::span<float> outputMin,
    std::span<float> outputMax)
{
    if (input.empty() ||
        outputMin.empty() ||
        outputMax.empty())
    {
        return;
    }

    const std::size_t outputSize =
        std::min(
            outputMin.size(),
            outputMax.size());

    for (std::size_t x = 0;
        x < outputSize;
        ++x)
    {
        const std::size_t first =
            x * input.size() /
            outputSize;

        const std::size_t last =
            std::max(
                first + 1u,
                (x + 1u) * input.size() /
                outputSize);

        float minimum = input[first];
        float maximum = input[first];

        for (std::size_t sourceIndex = first + 1u;
            sourceIndex < last &&
            sourceIndex < input.size();
            ++sourceIndex)
        {
            minimum =
                std::min(
                    minimum,
                    input[sourceIndex]);

            maximum =
                std::max(
                    maximum,
                    input[sourceIndex]);
        }

        outputMin[x] = minimum;
        outputMax[x] = maximum;
    }
}
WaveformRenderer::WaveformRenderer()
    : image_(
        720,
        576,
        QImage::Format_RGB32)
    , hits_(
        720u * 576u,
        0u)
    , trace_(
        720u * 576u)
    , displayY_(720u)
    , displayU_(720u)
    , displayV_(720u)
{
    for (std::size_t i = 0;
        i < displayLut_.size();
        ++i)
    {
        displayLut_[i] =
            static_cast<std::uint8_t>(
                std::min<std::size_t>(
                    i,
                    255u));
    }

    image_.fill(Qt::black);
}

void WaveformRenderer::setOutputSize(
    int width,
    int height)
{
    width =
        std::max(
            width,
            1);

    height =
        std::max(
            height,
            1);

    if (image_.width() == width &&
        image_.height() == height)
    {
        return;
    }

    image_ =
        QImage(
            width,
            height,
            QImage::Format_RGB32);

    image_.fill(Qt::black);

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);

    hits_.assign(
        pixelCount,
        0u);

    trace_.assign(
        pixelCount,
        TracePixel{});

    displayY_.resize(
        static_cast<std::size_t>(
            width));

    displayYMin_.resize(
        static_cast<std::size_t>(
            width));

    displayYMax_.resize(
        static_cast<std::size_t>(
            width));

    displayU_.resize(
        static_cast<std::size_t>(
            width));

    displayV_.resize(
        static_cast<std::size_t>(
            width));
}

void WaveformRenderer::setZoomed(
    bool zoomed)
{
    qDebug()
        << "WaveformRenderer zoomed:"
        << zoomed;

    zoomed_ = zoomed;
}

void WaveformRenderer::setScrollPosition(
    double position)
{
    scrollPosition_ =
        std::clamp(
            position,
            0.0,
            1.0);
}

void WaveformRenderer::analyze(
    const Yuv444Frame& frame)
{
    const std::size_t requiredSamples =
        frame.width > 0 &&
        frame.height > 0
        ? static_cast<std::size_t>(
            frame.width) *
        static_cast<std::size_t>(
            frame.height)
        : 0u;

    if (requiredSamples == 0u ||
        frame.y.size() < requiredSamples ||
        frame.u.size() < requiredSamples ||
        frame.v.size() < requiredSamples)
    {
        image_.fill(Qt::black);
        return;
    }

    renderAllLines(frame);
}

void WaveformRenderer::analyze(
    const Yuv444Frame& frame,
    const ReconstructedLumaFrame& reconstructedLuma)
{
    if (reconstructedLuma.width <= 0 ||
        reconstructedLuma.height <= 0 ||
        reconstructedLuma.y.empty())
    {
        image_.fill(Qt::black);
        return;
    }

    if (selectedLine_ >= 0 &&
        selectedLine_ < reconstructedLuma.height)
    {
        renderSingleLine(
            frame,
            reconstructedLuma);

        return;
    }

    analyze(frame);
}

void WaveformRenderer::clearOrFadeTrace()
{
    if (persistence_ == 0)
    {
        std::fill(
            trace_.begin(),
            trace_.end(),
            TracePixel{});

        return;
    }

    const std::uint32_t persistence =
        static_cast<std::uint32_t>(
            persistence_);

    for (TracePixel& pixel : trace_)
    {
        pixel.red =
            static_cast<std::uint16_t>(
                (static_cast<std::uint32_t>(
                    pixel.red) *
                    persistence) >>
                8);

        pixel.green =
            static_cast<std::uint16_t>(
                (static_cast<std::uint32_t>(
                    pixel.green) *
                    persistence) >>
                8);

        pixel.blue =
            static_cast<std::uint16_t>(
                (static_cast<std::uint32_t>(
                    pixel.blue) *
                    persistence) >>
                8);
    }
}

QRectF WaveformRenderer::viewportRect() const
{
    const double widgetWidth =
        static_cast<double>(
            image_.width() - 1);

    const double widgetHeight =
        static_cast<double>(
            image_.height() - 1);

    constexpr double aspectRatio =
        4.0 / 3.0;

    double width =
        widgetWidth;

    double height =
        width /
        aspectRatio;

    if (height > widgetHeight)
    {
        height =
            widgetHeight;

        width =
            height *
            aspectRatio;
    }

    const double left =
        (widgetWidth - width) *
        0.5;

    const double top =
        (widgetHeight - height) *
        0.5;

    return QRectF(
        left,
        top,
        width,
        height);
}

QRectF WaveformRenderer::scopeRect() const
{
    const QRectF viewport =
        viewportRect();

    const double left =
        viewport.left() +
        graticule_.leftInset(
            QApplication::font(),
            &image_,
            viewport.height());

    return QRectF(
        left,
        viewport.top(),
        viewport.right() - left,
        viewport.height());
}

QRectF WaveformRenderer::scaledScopeRect() const
{
    const QRectF scope =
        scopeRect();

    const QFont font =
        graticule_.labelFont(
            QApplication::font(),
            scope.height());

    const QFontMetricsF metrics(
        font,
        &image_);

    const double labelHeight =
        metrics.height();

    return QRectF(
        scope.left(),
        scope.top() + labelHeight * 0.5,
        scope.width(),
        scope.height() - labelHeight);
}

void WaveformRenderer::renderSingleLine(
    const Yuv444Frame& frame,
    const ReconstructedLumaFrame& reconstructedLuma)
{
    clearOrFadeTrace();

    const std::size_t sourceWidth =
        static_cast<std::size_t>(
            frame.width);

    const std::size_t reconstructedWidth =
        static_cast<std::size_t>(
            reconstructedLuma.width);

    const std::size_t sourceLineOffset =
        static_cast<std::size_t>(
            selectedLine_) *
        sourceWidth;

    const std::size_t reconstructedLineOffset =
        static_cast<std::size_t>(
            selectedLine_) *
        reconstructedWidth;

    /*
     * Determine which part of the reconstructed line
     * is visible.
     *
     * X1:
     *     complete reconstructed line.
     *
     * X10:
     *     one tenth of the reconstructed line,
     *     continuously movable with scrollPosition_.
     */
    std::size_t reconstructedViewWidth =
        reconstructedWidth;

    std::size_t reconstructedViewOffset = 0u;

    if (zoomed_)
    {
        reconstructedViewWidth =
            std::max<std::size_t>(
                reconstructedWidth / 10u,
                1u);

        const std::size_t maximumOffset =
            reconstructedWidth -
            reconstructedViewWidth;

        reconstructedViewOffset =
            static_cast<std::size_t>(
                scrollPosition_ *
                static_cast<double>(
                    maximumOffset));
    }

    /*
     * Luma comes from the permanently reconstructed
     * high-resolution line.
     *
     * Convert 10-bit BT.601 legal-range Y
     * to equivalent PAL video voltage:
     *
     *   Y = 64  -> 0.3 V
     *   Y = 940 -> 1.0 V
     */
    sourceY_.resize(
        reconstructedViewWidth);

    const auto digitalLevels =
        levels(VideoStandard::pal625());

    const auto analog =
        analogLevels(
            VideoColorStandard::Rec601_625);

    const double voltsPerCode =
        (analog.whiteVolts - analog.blackVolts) /
        static_cast<double>(
            digitalLevels.yWhite -
            digitalLevels.yBlack);

    for (std::size_t x = 0;
        x < reconstructedViewWidth;
        ++x)
    {
        const double y10 =
            static_cast<double>(
                reconstructedLuma.y[
                    reconstructedLineOffset +
                        reconstructedViewOffset +
                        x]) /
            64.0;

        sourceY_[x] =
            static_cast<float>(
                analog.blackVolts +
                (y10 - digitalLevels.yBlack) *
                voltsPerCode);
    }
    /*
     * U/V are still native 720-sample data.
     * Map the same visible interval from the
     * reconstructed coordinate system back into
     * the native source coordinate system.
     */
    std::size_t sourceViewWidth =
        sourceWidth;

    std::size_t sourceViewOffset = 0u;

    if (zoomed_)
    {
        sourceViewWidth =
            std::max<std::size_t>(
                sourceWidth / 10u,
                1u);

        const std::size_t maximumSourceOffset =
            sourceWidth -
            sourceViewWidth;

        const double normalisedPosition =
            reconstructedWidth > 1u
            ? static_cast<double>(
                reconstructedViewOffset) /
            static_cast<double>(
                reconstructedWidth -
                reconstructedViewWidth)
            : 0.0;

        sourceViewOffset =
            static_cast<std::size_t>(
                normalisedPosition *
                static_cast<double>(
                    maximumSourceOffset));
    }

    sourceU_.resize(
        sourceViewWidth);

    sourceV_.resize(
        sourceViewWidth);

    for (std::size_t x = 0;
        x < sourceViewWidth;
        ++x)
    {
        const std::size_t sourceX =
            std::min(
                sourceViewOffset + x,
                sourceWidth - 1u);

        const std::size_t sampleIndex =
            sourceLineOffset +
            sourceX;

        sourceU_[x] =
            static_cast<float>(
                frame.u[sampleIndex]);

        sourceV_[x] =
            static_cast<float>(
                frame.v[sampleIndex]);
    }

    /*
     * Resample only the visible interval to the
     * actual display width.
     */
    if (sourceY_.size() >
        displayY_.size())
    {
        resampleMinMax(
            sourceY_,
            displayYMin_,
            displayYMax_);

        resampleLinear(
            sourceY_,
            displayY_);
    }
    else
    {
        resampleLinear(
            sourceY_,
            displayY_);
    }

    resampleLinear(
        sourceU_,
        displayU_);

    resampleLinear(
        sourceV_,
        displayV_);
    
    const int displayWidth =
        image_.width();

    std::vector<double> chromaUpperY(
        static_cast<std::size_t>(displayWidth));

    std::vector<double> chromaLowerY(
        static_cast<std::size_t>(displayWidth));

    std::vector<int> chromaRed(
        static_cast<std::size_t>(displayWidth));

    std::vector<int> chromaGreen(
        static_cast<std::size_t>(displayWidth));

    std::vector<int> chromaBlue(
        static_cast<std::size_t>(displayWidth));

    const QRectF scope =
        scaledScopeRect();

    const double voltsToPixels =
        scope.height() /
        analog.graticuleMaxVolts;

    const double chromaXScale =
        scope.width() /
        static_cast<double>(
            displayWidth - 1);

    for (int x = 0;
        x < displayWidth;
        ++x)
    {
        const std::size_t index =
            static_cast<std::size_t>(
                x);
        
        const double plotX =
            scope.left() +
            static_cast<double>(x) *
            chromaXScale;

        const double yValue =
            std::clamp(
                static_cast<double>(
                    displayY_[index]),
                0.0,
                kMaximumSampleValue);

        const double uValue =
            std::clamp(
                static_cast<double>(
                    displayU_[index]),
                0.0,
                kMaximumSampleValue);

        const double vValue =
            std::clamp(
                static_cast<double>(
                    displayV_[index]),
                0.0,
                kMaximumSampleValue);

        const double neutralChroma =
            static_cast<double>(
                digitalLevels.chromaNeutral) *
            64.0;

        const double chromaNoiseThreshold =
            neutralChroma *
            kChromaNoiseThresholdFraction;

        double chromaU =
            uValue -
            neutralChroma;

        double chromaV =
            vValue -
            neutralChroma;

        const auto palChroma =
            palChromaCoefficients(
                VideoColorStandard::Rec601_625);

        const double palU =
            chromaU *
            palChroma.cbToU;

        const double palV =
            chromaV *
            palChroma.crToV;

        const double chromaMagnitude =
            std::hypot(
                palU,
                palV);

        if (chromaMagnitude <
            chromaNoiseThreshold)
        {
            chromaU = 0.0;
            chromaV = 0.0;
        }

        const double plotY =
            scope.bottom() -
            yValue *
            voltsToPixels;

        const double nominalChromaExcursion =
            static_cast<double>(
                digitalLevels.chromaPositiveExcursion) *
            64.0;

        const double chromaVolts =
            std::hypot(
                palU,
                palV) *
            analog.chromaPeakVolts /
            nominalChromaExcursion;

        const double spread =
            chromaVolts *
            voltsToPixels;

        chromaUpperY[index] =
            plotY - spread;

        chromaLowerY[index] =
            plotY + spread;

        const auto rgb =
            yuvToRgbCoefficients(
                VideoColorStandard::Rec601_625);

        double redValue =
            rgb.rCr * chromaV;

        double greenValue =
            rgb.gCb * chromaU +
            rgb.gCr * chromaV;

        double blueValue =
            rgb.bCb * chromaU;

        const double minimum =
            std::min({
                redValue,
                greenValue,
                blueValue
                });

        redValue -= minimum;
        greenValue -= minimum;
        blueValue -= minimum;

        const double maximum =
            std::max({
                redValue,
                greenValue,
                blueValue
                });

        int red = 0;
        int green = 0;
        int blue = 0;

        if (maximum > 0.0)
        {
            red =
                static_cast<int>(
                    std::clamp(
                        redValue *
                        255.0 /
                        maximum,
                        0.0,
                        255.0));

            green =
                static_cast<int>(
                    std::clamp(
                        greenValue *
                        255.0 /
                        maximum,
                        0.0,
                        255.0));

            blue =
                static_cast<int>(
                    std::clamp(
                        blueValue *
                        255.0 /
                        maximum,
                        0.0,
                        255.0));
            chromaRed[index] = red;
            chromaGreen[index] = green;
            chromaBlue[index] = blue;
        }

        //if (maximum > 0.0 &&
        //    spread > 0.0)
        //{
        //    plotBeam(
        //        plotX,
        //        plotY - spread,
        //        kChromaBeamIntensity,
        //        red,
        //        green,
        //        blue);

        //    plotBeam(
        //        plotX,
        //        plotY + spread,
        //        kChromaBeamIntensity,
        //        red,
        //        green,
        //        blue);
        //}
    }


    plotLuminanceTrace();
    composeTraceImage();

    const int firstScreenX =
        static_cast<int>(
            std::ceil(scope.left()));

    const int lastScreenX =
        static_cast<int>(
            std::floor(scope.right()));

    for (int screenX = firstScreenX;
        screenX <= lastScreenX;
        ++screenX)
    {
        const double normalisedX =
            (static_cast<double>(screenX) -
                scope.left()) /
            scope.width();

        const std::size_t index =
            std::min(
                static_cast<std::size_t>(
                    normalisedX *
                    static_cast<double>(
                        displayWidth - 1)),
                static_cast<std::size_t>(
                    displayWidth - 1));

        const int firstY =
            std::clamp(
                static_cast<int>(
                    std::ceil(
                        chromaUpperY[index])),
                0,
                image_.height() - 1);

        const int lastY =
            std::clamp(
                static_cast<int>(
                    std::floor(
                        chromaLowerY[index])),
                0,
                image_.height() - 1);

        for (int y = firstY;
            y <= lastY;
            ++y)
        {
            addFillPixel(
                image_,
                screenX,
                y,
                chromaRed[index],
                chromaGreen[index],
                chromaBlue[index],
                chromaFillIntensity_);
        }
    }

    QPainter painter(&image_);

    graticule_.draw(
        painter,
        scaledScopeRect(),
        VideoStandard::pal625());
}


void WaveformRenderer::plotLuminanceTrace()
{
    const int width =
        image_.width();

    const int height =
        image_.height();

    const QRectF scope =
        scaledScopeRect();

    const double xScale =
        scope.width() /
        static_cast<double>(
            width - 1);

    if (width < 2 ||
        displayY_.size() <
        static_cast<std::size_t>(
            width))
    {
        return;
    }

    const auto analog =
        analogLevels(
            VideoColorStandard::Rec601_625);


    const double voltsToPixels =
        scope.height() /
        analog.graticuleMaxVolts;

    auto sampleToPlotY =
        [scope,
        voltsToPixels](double volts)
        {
            return
                scope.bottom() -
                volts *
                voltsToPixels;
        };

    auto plotSmoothCurve =
        [this,
        scope](
            const auto& sampleY,
            int sampleCount,
            int intensity)
        {
            constexpr double targetStepPixels = 0.5;
            
            const double curveXScale =
                scope.width() /
                static_cast<double>(
                    sampleCount - 1);
            
            for (int x = 0;
                x < sampleCount - 1;
                ++x)
            {
                const double p0 =
                    sampleY(x - 1);

                const double p1 =
                    sampleY(x);

                const double p2 =
                    sampleY(x + 1);

                const double p3 =
                    sampleY(x + 2);

                const double distance =
                    std::hypot(
                        1.0,
                        p2 - p1);

                const int subdivisions =
                    std::clamp(
                        static_cast<int>(
                            std::ceil(
                                distance /
                                targetStepPixels)),
                        1,
                        256);
                double previousX =
                    scope.left() +
                    static_cast<double>(x) *
                    curveXScale;

                double previousY =
                    p1;
                for (int step = 0;
                    step <= subdivisions;
                    ++step)
                {
                    const double t =
                        static_cast<double>(step) /
                        static_cast<double>(
                            subdivisions);

                    const double t2 =
                        t * t;

                    const double t3 =
                        t2 * t;

                    const double y =
                        0.5 *
                        (
                            2.0 * p1 +
                            (-p0 + p2) * t +
                            (2.0 * p0 -
                                5.0 * p1 +
                                4.0 * p2 -
                                p3) * t2 +
                            (-p0 +
                                3.0 * p1 -
                                3.0 * p2 +
                                p3) * t3
                            );

                    const double plotX =
                        scope.left() +
                        (
                            static_cast<double>(x) +
                            t
                            ) *
                        curveXScale;

                    if (step > 0)
                    {
                        plotSegment(
                            previousX,
                            previousY,
                            plotX,
                            y,
                            intensity,
                            255,
                            255,
                            255);
                    }

                    previousX = plotX;
                    previousY = y;
                }
            }
        };

    auto sampleCenter =
        [this, &sampleToPlotY](int x)
        {
            x =
                std::clamp(
                    x,
                    0,
                    image_.width() - 1);

            return
                sampleToPlotY(
                    static_cast<double>(
                        displayY_[
                            static_cast<std::size_t>(
                                x)]));
        };

    auto sampleZoomed =
        [this, &sampleToPlotY](int x)
        {
            x =
                std::clamp(
                    x,
                    0,
                    static_cast<int>(
                        sourceY_.size()) - 1);

            return
                sampleToPlotY(
                    static_cast<double>(
                        sourceY_[
                            static_cast<std::size_t>(
                                x)]));
        };

    // Keep the original smooth waveform as the primary trace.
    if (zoomed_)
    {
        plotSmoothCurve(
            sampleZoomed,
            static_cast<int>(
                sourceY_.size()),
            kLuminanceBeamIntensity);
    }
    else
    {
        plotSmoothCurve(
            sampleCenter,
            width,
            kLuminanceBeamIntensity);
    }

    if (sourceY_.size() <=
        displayY_.size())
    {
        return;
    }

    /*
     * When several reconstructed source samples collapse into one
     * display column, preserve their full vertical energy.
     *
     * The smooth centre trace stays visible where the waveform still
     * fits the horizontal display resolution. If the samples within
     * one display column span a meaningful vertical range, draw that
     * complete min/max range solid white instead of inventing a
     * misleading interpolated waveform.
     */
    constexpr double kMinMaxFillThresholdPixels = 3.0;

    for (int x = 0;
        x < width;
        ++x)
    {
        const std::size_t index =
            static_cast<std::size_t>(x);

        const double upperY =
            sampleToPlotY(
                static_cast<double>(
                    displayYMax_[index]));

        const double lowerY =
            sampleToPlotY(
                static_cast<double>(
                    displayYMin_[index]));

        const double spanPixels =
            std::abs(
                lowerY - upperY);

        if (spanPixels <
            kMinMaxFillThresholdPixels)
        {
            continue;
        }

        const int firstY =
            std::clamp(
                static_cast<int>(
                    std::ceil(
                        std::min(
                            upperY,
                            lowerY))),
                0,
                height - 1);

        const int lastY =
            std::clamp(
                static_cast<int>(
                    std::floor(
                        std::max(
                            upperY,
                            lowerY))),
                0,
                height - 1);

        for (int y = firstY;
            y <= lastY;
            ++y)
        {
            plotBeam(
                scope.left() +
                static_cast<double>(x) *
                xScale,
                static_cast<double>(y),
                kLuminanceBeamIntensity,
                255,
                255,
                255);
        }
    }

}

void WaveformRenderer::composeTraceImage()
{
    const int width =
        image_.width();

    const int height =
        image_.height();

    for (int y = 0;
        y < height;
        ++y)
    {
        auto* destination =
            reinterpret_cast<QRgb*>(
                image_.scanLine(y));

        for (int x = 0;
            x < width;
            ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);

            const TracePixel& pixel =
                trace_[index];

            destination[x] =
                qRgb(
                    displayLut_[pixel.red],
                    displayLut_[pixel.green],
                    displayLut_[pixel.blue]);
        }
    }
}

void WaveformRenderer::renderAllLines(
    const Yuv444Frame& frame)
{
    std::fill(
        hits_.begin(),
        hits_.end(),
        0u);

    const int displayWidth =
        image_.width();

    const int displayHeight =
        image_.height();

    for (int line = 0;
        line < frame.height;
        ++line)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(line) *
            static_cast<std::size_t>(
                frame.width);

        int previousPlotY = -1;

        for (int sourceX = 0;
            sourceX < frame.width;
            ++sourceX)
        {
            const int displayX =
                std::clamp(
                    sourceX *
                    displayWidth /
                    frame.width,
                    0,
                    displayWidth - 1);

            const std::uint16_t y16 =
                frame.y[
                    lineOffset +
                        static_cast<std::size_t>(
                            sourceX)];

            const std::uint32_t scaledY =
                static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(
                        y16) *
                    static_cast<std::uint64_t>(
                        (displayHeight - 1) << 8) /
                    65535u);

            const int plotY =
                displayHeight - 1 -
                static_cast<int>(
                    scaledY >> 8);

            const std::uint32_t fraction =
                scaledY & 0xffu;

            const std::size_t currentIndex =
                static_cast<std::size_t>(
                    plotY) *
                static_cast<std::size_t>(
                    displayWidth) +
                static_cast<std::size_t>(
                    displayX);

            hits_[currentIndex] +=
                256u - fraction;

            if (plotY > 0)
            {
                const std::size_t adjacentIndex =
                    static_cast<std::size_t>(
                        plotY - 1) *
                    static_cast<std::size_t>(
                        displayWidth) +
                    static_cast<std::size_t>(
                        displayX);

                hits_[adjacentIndex] +=
                    fraction;
            }

            if (previousPlotY >= 0)
            {
                const int firstY =
                    std::min(
                        previousPlotY,
                        plotY);

                const int lastY =
                    std::max(
                        previousPlotY,
                        plotY);

                for (int y = firstY;
                    y <= lastY;
                    ++y)
                {
                    const std::size_t segmentIndex =
                        static_cast<std::size_t>(
                            y) *
                        static_cast<std::size_t>(
                            displayWidth) +
                        static_cast<std::size_t>(
                            displayX);

                    hits_[segmentIndex] +=
                        64u;
                }
            }

            previousPlotY = plotY;
        }
    }

    for (int y = 0;
        y < displayHeight;
        ++y)
    {
        auto* destination =
            reinterpret_cast<QRgb*>(
                image_.scanLine(y));

        for (int x = 0;
            x < displayWidth;
            ++x)
        {
            const std::uint32_t hit =
                hits_[
                    static_cast<std::size_t>(
                        y) *
                        static_cast<std::size_t>(
                            displayWidth) +
                        static_cast<std::size_t>(
                            x)];

            const int green =
                std::min(
                    255,
                    static_cast<int>(
                        hit >> 8) * 8);

            destination[x] =
                qRgb(
                    green,
                    green,
                    green);
        }
    }
}

void WaveformRenderer::plotSegment(
    double x0,
    double y0,
    double x1,
    double y1,
    int intensity,
    int red,
    int green,
    int blue)
{
    const double dx =
        x1 - x0;

    const double dy =
        y1 - y0;

    const double lengthSquared =
        dx * dx +
        dy * dy;

    if (lengthSquared <= 0.0)
    {
        plotBeam(
            x0,
            y0,
            intensity,
            red,
            green,
            blue);

        return;
    }

    const int firstX =
        std::max(
            0,
            static_cast<int>(
                std::floor(
                    std::min(x0, x1))) - 1);

    const int lastX =
        std::min(
            image_.width() - 1,
            static_cast<int>(
                std::ceil(
                    std::max(x0, x1))) + 1);

    const int firstY =
        std::max(
            0,
            static_cast<int>(
                std::floor(
                    std::min(y0, y1))) - 1);

    const int lastY =
        std::min(
            image_.height() - 1,
            static_cast<int>(
                std::ceil(
                    std::max(y0, y1))) + 1);

    for (int y = firstY;
        y <= lastY;
        ++y)
    {
        for (int x = firstX;
            x <= lastX;
            ++x)
        {
            const double px =
                static_cast<double>(x);

            const double py =
                static_cast<double>(y);

            const double projection =
                ((px - x0) * dx +
                    (py - y0) * dy) /
                lengthSquared;

            const double t =
                std::clamp(
                    projection,
                    0.0,
                    1.0);

            const double nearestX =
                x0 + t * dx;

            const double nearestY =
                y0 + t * dy;

            const double distance =
                std::hypot(
                    px - nearestX,
                    py - nearestY);

            const double coverage =
                std::clamp(
                    1.25 - distance,
                    0.0,
                    1.0);

            if (coverage <= 0.0)
            {
                continue;
            }

            const std::uint32_t contribution =
                static_cast<std::uint32_t>(
                    coverage *
                    static_cast<double>(
                        intensity));

            const std::size_t index =
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(
                    image_.width()) +
                static_cast<std::size_t>(x);

            TracePixel& pixel =
                trace_[index];

            const auto addChannel =
                [contribution](
                    std::uint16_t& destination,
                    int channel)
                {
                    const std::uint32_t scaled =
                        contribution *
                        static_cast<std::uint32_t>(
                            std::clamp(
                                channel,
                                0,
                                255)) /
                        255u;

                    destination =
                        static_cast<std::uint16_t>(
                            std::min<std::uint32_t>(
                                65535u,
                                static_cast<std::uint32_t>(
                                    destination) +
                                scaled));
                };

            addChannel(
                pixel.red,
                red);

            addChannel(
                pixel.green,
                green);

            addChannel(
                pixel.blue,
                blue);
        }
    }
}

void WaveformRenderer::plotBeam(
    double x,
    double y,
    int intensity,
    int red,
    int green,
    int blue)
{
    if (x < 0.0 ||
        x >= static_cast<double>(
            image_.width()))
    {
        return;
    }

    const int x0 =
        static_cast<int>(
            std::floor(x));

    const int y0 =
        static_cast<int>(
            std::floor(y));

    const double fx =
        x -
        static_cast<double>(x0);

    const double fy =
        y -
        static_cast<double>(y0);

    for (int dy = 0;
        dy <= 1;
        ++dy)
    {
        const int destinationY =
            y0 + dy;

        if (destinationY < 0 ||
            destinationY >= image_.height())
        {
            continue;
        }

        const double wy =
            dy == 0
            ? 1.0 - fy
            : fy;

        for (int dx = 0;
            dx <= 1;
            ++dx)
        {
            const int destinationX =
                x0 + dx;

            if (destinationX < 0 ||
                destinationX >= image_.width())
            {
                continue;
            }

            const double wx =
                dx == 0
                ? 1.0 - fx
                : fx;

            const double weight =
                wx * wy;

            const std::uint32_t contribution =
                static_cast<std::uint32_t>(
                    weight *
                    static_cast<double>(
                        intensity));

            const std::size_t index =
                static_cast<std::size_t>(
                    destinationY) *
                static_cast<std::size_t>(
                    image_.width()) +
                static_cast<std::size_t>(
                    destinationX);

            TracePixel& pixel =
                trace_[index];

            const auto addChannel =
                [contribution](
                    std::uint16_t& destination,
                    int channel)
                {
                    const std::uint32_t scaled =
                        contribution *
                        static_cast<std::uint32_t>(
                            std::clamp(
                                channel,
                                0,
                                255)) /
                        255u;

                    destination =
                        static_cast<std::uint16_t>(
                            std::max<std::uint32_t>(
                                static_cast<std::uint32_t>(
                                    destination),
                                scaled));
                };

            addChannel(
                pixel.red,
                red);

            addChannel(
                pixel.green,
                green);

            addChannel(
                pixel.blue,
                blue);
        }
    }
}

void WaveformRenderer::setSelectedLine(
    int line)
{
    selectedLine_ = line;
}

void WaveformRenderer::setPersistence(
    int persistence)
{
    persistence_ =
        std::clamp(
            persistence,
            0,
            255);
}

const QImage& WaveformRenderer::image() const
{
    return image_;
}

double WaveformRenderer::traceBandwidthMHz() const
{
    constexpr double captureSampleRateMHz = 13.5;
    constexpr double captureSamplesPerLine = 720.0;

    return
        captureSampleRateMHz *
        static_cast<double>(
            image_.width()) /
        (
            captureSamplesPerLine *
            kPixelsPerCycleForTraceBW);
}
