#pragma once
#include <cstddef>
#include <vector>
#include <span>

inline constexpr std::size_t kReconstructedLumaWidth = 2880;

class LineResampler
{
public:
    explicit LineResampler(
        int kernelRadius = 24,
        float cutoff = 1.00f);

    void resample(
        std::span<const float> input,
        std::span<float> output) const;

private:
    static float sinc(float x);
    float kernel(float distance, float cutoff) const;
    struct CachedOutputSample
    {
        int firstInputIndex = 0;
        float inverseWeightSum = 0.0f;
        std::size_t weightOffset = 0;
    };
    int kernelRadius_ = 8;
    void rebuildCache(
        std::size_t inputSize,
        std::size_t outputSize) const;

    // Relative to the Nyquist frequency of the input.
    // 0.85 corresponds to approximately 5.74 MHz at 13.5 MHz sampling.
    float cutoff_ = 0.85f;
    mutable std::size_t cachedInputSize_ = 0;
    mutable std::size_t cachedOutputSize_ = 0;

    mutable std::vector<CachedOutputSample> cache_;
    mutable std::vector<float> cachedWeights_;
};