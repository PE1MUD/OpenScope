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
};