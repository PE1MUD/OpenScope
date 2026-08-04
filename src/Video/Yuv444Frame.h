#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct Yuv444Frame
{
    int width = 0;
    int height = 0;

    // Each plane contains width * height samples.
    std::vector<std::uint16_t> y;
    std::vector<std::uint16_t> u;
    std::vector<std::uint16_t> v;

    void resize(int newWidth, int newHeight)
    {
        width = newWidth;
        height = newHeight;

        const std::size_t sampleCount =
            static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height);

        y.resize(sampleCount);
        u.resize(sampleCount);
        v.resize(sampleCount);
    }
};