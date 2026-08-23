#pragma once

#include "video/Yuv444Frame.h"

#include <cstdint>
#include <vector>

class LumaHighFrequencyCompensator
{
public:
    void process(
        Yuv444Frame& frame,
        int gainHundredthsDb);

private:
    std::vector<std::uint16_t> lineBuffer_;
};
