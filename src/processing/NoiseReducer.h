#pragma once

#include "video/Yuv444Frame.h"

#include <cstdint>
#include <vector>

class NoiseReducer
{
public:
    void process(
        const Yuv444Frame& source,
        Yuv444Frame& destination,
        int intensity);

private:
    static constexpr int kMinimumIntensity = 0;
    static constexpr int kMaximumIntensity = 100;

    // Intensity controls both edge acceptance and blend.
    // 0 keeps the original image.
    // 50 matches the earlier conservative filter.
    // 100 deliberately becomes much more aggressive.
    static constexpr std::uint16_t kMinimumLumaThreshold = 1536;
    static constexpr std::uint16_t kMaximumLumaThreshold = 6144;

    static constexpr std::uint16_t kMinimumChromaThreshold = 2304;
    static constexpr std::uint16_t kMaximumChromaThreshold = 9216;

    static constexpr int kFilterRadius = 2;

    static void filterPlane(
        const std::vector<std::uint16_t>& source,
        std::vector<std::uint16_t>& destination,
        int width,
        int height,
        std::uint16_t threshold);

    static void blendPlane(
        const std::vector<std::uint16_t>& source,
        std::vector<std::uint16_t>& filtered,
        int intensity);
};