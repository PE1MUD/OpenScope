#include "processing/LumaHighFrequencyCompensator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace
{
    constexpr int kRadius = 8;
    constexpr double kReferenceFrequencyHz = 5'800'000.0;

    // 17-tap symmetric, unity-DC FIR.  The response is a gentle HF lift that
    // approximates a linear-in-dB ramp from 0 dB at DC to +1 dB at 5.8 MHz
    // at a 13.5 MHz luma sample rate.  Runtime scaling preserves unity DC.
    constexpr std::array<double, 17> kUnitCorrectionKernel =
    {
        -0.00043087478646947205,
        -0.0000003169517475910403,
        -0.0007501379827647017,
        -0.00023109977423583537,
        -0.0010725169719288056,
        -0.0019790519694750787,
        -0.0013128860859289422,
        -0.027304513969740495,
         1.066162796984582,
        -0.027304513969740495,
        -0.0013128860859289422,
        -0.0019790519694750787,
        -0.0010725169719288056,
        -0.00023109977423583537,
        -0.0007501379827647017,
        -0.0000003169517475910403,
        -0.00043087478646947205
    };

    // Zero-phase amplitude of kUnitCorrectionKernel at 5.8 MHz.
    constexpr double kUnitEndpointAmplitude =
        1.1170782872165974;

    double correctionScaleForDb(double gainDb)
    {
        const double targetAmplitude =
            std::pow(10.0, gainDb / 20.0);

        return
            (targetAmplitude - 1.0) /
            (kUnitEndpointAmplitude - 1.0);
    }
}

void LumaHighFrequencyCompensator::process(
    Yuv444Frame& frame,
    int gainHundredthsDb)
{
    if (gainHundredthsDb <= 0 ||
        frame.width <= 2 * kRadius ||
        frame.height <= 0 ||
        frame.y.size() <
            static_cast<std::size_t>(frame.width) *
            static_cast<std::size_t>(frame.height))
    {
        return;
    }

    const double gainDb =
        static_cast<double>(
            std::clamp(gainHundredthsDb, 0, 100)) /
        100.0;

    const double scale =
        correctionScaleForDb(gainDb);

    lineBuffer_.resize(
        static_cast<std::size_t>(frame.width));

    for (int y = 0; y < frame.height; ++y)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(frame.width);

        std::copy_n(
            frame.y.data() + lineOffset,
            frame.width,
            lineBuffer_.data());

        // Deliberately leave the first/last kRadius samples untouched.
        // The FIR is centred on x, so there is no added luma group delay and
        // therefore no artificial chroma/luma delay to compensate in U/V.
        for (int x = kRadius;
             x < frame.width - kRadius;
             ++x)
        {
            double value =
                static_cast<double>(
                    lineBuffer_[static_cast<std::size_t>(x)]);

            double correction =
                (kUnitCorrectionKernel[kRadius] - 1.0) *
                static_cast<double>(
                    lineBuffer_[static_cast<std::size_t>(x)]);

            for (int tap = 1; tap <= kRadius; ++tap)
            {
                const double coefficient =
                    kUnitCorrectionKernel[
                        static_cast<std::size_t>(
                            kRadius - tap)];

                correction +=
                    coefficient *
                    (static_cast<double>(
                        lineBuffer_[static_cast<std::size_t>(x - tap)]) +
                     static_cast<double>(
                        lineBuffer_[static_cast<std::size_t>(x + tap)]));
            }

            value += scale * correction;

            frame.y[
                lineOffset +
                static_cast<std::size_t>(x)] =
                static_cast<std::uint16_t>(
                    std::clamp(
                        std::lround(value),
                        0L,
                        65535L));
        }
    }
}
