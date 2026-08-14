#include "VideoDeinterlacer.h"

#include <algorithm>
#include <cstddef>

void VideoDeinterlacer::deinterlace(
    const std::uint16_t* source,
    int width,
    int height,
    ProgressiveLumaPair& destination)
{
    if (source == nullptr ||
        width <= 0 ||
        height <= 0)
    {
        return;
    }

    const std::size_t sampleCount =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);

    destination.first.resize(
        width,
        height);

    destination.second.resize(
        width,
        height);

    std::copy_n(
        source,
        sampleCount,
        destination.first.y.data());

    std::copy_n(
        source,
        sampleCount,
        destination.second.y.data());
}