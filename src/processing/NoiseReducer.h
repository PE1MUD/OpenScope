#pragma once

#include "video/Yuv444Frame.h"

#include <cstdint>
#include <vector>

class NoiseReducer
{
public:
    void process(
        const Yuv444Frame& source,
        Yuv444Frame& destination);

private:
    static constexpr std::uint16_t kLumaThreshold = 1536;
    static constexpr std::uint16_t kChromaThreshold = 2304;

    static void filterPlane(
        const std::vector<std::uint16_t>& source,
        std::vector<std::uint16_t>& destination,
        int width,
        int height,
        std::uint16_t threshold);
};