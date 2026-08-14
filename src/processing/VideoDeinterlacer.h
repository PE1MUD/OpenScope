#pragma once

#include "video/ProgressiveLumaPair.h"

#include <cstdint>

class VideoDeinterlacer
{
public:
    void deinterlace(
        const std::uint16_t* source,
        int width,
        int height,
        ProgressiveLumaPair& destination);
private:
    std::vector<std::uint16_t> previousLuma_;
    std::vector<std::uint16_t> previousPreviousLuma_;
    std::vector<std::uint8_t> motionMask_;

    bool hasPreviousFrame_ = false;
};