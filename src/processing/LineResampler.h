#pragma once

#include <span>

class LineResampler
{
public:
    explicit LineResampler(
        int kernelRadius = 8,
        float cutoff = 0.85f);

    void resample(
        std::span<const float> input,
        std::span<float> output) const;

private:
    static float sinc(float x);
    float kernel(float distance, float cutoff) const;

    int kernelRadius_ = 8;

    // Relative to the Nyquist frequency of the input.
    // 0.85 corresponds to approximately 5.74 MHz at 13.5 MHz sampling.
    float cutoff_ = 0.85f;
};