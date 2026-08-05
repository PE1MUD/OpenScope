#include "LineResampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

LineResampler::LineResampler(
    int kernelRadius,
    float cutoff)
    : kernelRadius_(std::max(kernelRadius, 1))
    , cutoff_(std::clamp(cutoff, 0.01f, 1.0f))
{}

void LineResampler::resample(
    std::span<const float> input,
    std::span<float> output) const
{
    if (output.empty()) {
        return;
    }

    if (input.empty()) {
        std::fill(output.begin(), output.end(), 0.0f);
        return;
    }

    if (input.size() == 1) {
        std::fill(output.begin(), output.end(), input.front());
        return;
    }

    const float scale =
        static_cast<float>(output.size()) /
        static_cast<float>(input.size());

    // When reducing the number of samples, also reduce the cutoff
    // to prevent aliasing.
    const float effectiveCutoff =
        cutoff_ * std::min(1.0f, scale);

    for (std::size_t outputIndex = 0;
        outputIndex < output.size();
        ++outputIndex) {

        // Map sample centres instead of sample edges.
        const float sourcePosition =
            (static_cast<float>(outputIndex) + 0.5f) / scale
            - 0.5f;

        const int centre =
            static_cast<int>(std::floor(sourcePosition));

        const int firstSample =
            centre - kernelRadius_ + 1;

        const int lastSample =
            centre + kernelRadius_;

        float weightedSum = 0.0f;
        float weightSum = 0.0f;

        for (int sourceIndex = firstSample;
            sourceIndex <= lastSample;
            ++sourceIndex) {

            if (sourceIndex < 0 ||
                sourceIndex >= static_cast<int>(input.size())) {
                continue;
            }

            const float distance =
                sourcePosition -
                static_cast<float>(sourceIndex);

            const float weight =
                kernel(distance, effectiveCutoff);

            weightedSum +=
                input[static_cast<std::size_t>(sourceIndex)] *
                weight;

            weightSum += weight;
        }

        if (std::abs(weightSum) > 1.0e-8f) {
            output[outputIndex] =
                weightedSum / weightSum;
        }
        else {
            output[outputIndex] = 0.0f;
        }
    }
}

float LineResampler::sinc(float x)
{
    if (std::abs(x) < 1.0e-6f) {
        return 1.0f;
    }

    const float piX =
        std::numbers::pi_v<float> *x;

    return std::sin(piX) / piX;
}

float LineResampler::kernel(
    float distance,
    float cutoff) const
{
    const float absoluteDistance =
        std::abs(distance);

    if (absoluteDistance >=
        static_cast<float>(kernelRadius_)) {
        return 0.0f;
    }

    // Blackman window over the finite sinc kernel.
    const float normalisedDistance =
        absoluteDistance /
        static_cast<float>(kernelRadius_);

    const float window =
        0.42f
        + 0.5f * std::cos(
            std::numbers::pi_v<float> *
            normalisedDistance)
        + 0.08f * std::cos(
            2.0f *
            std::numbers::pi_v<float> *
            normalisedDistance);

    return cutoff *
        sinc(cutoff * distance) *
        window;
}