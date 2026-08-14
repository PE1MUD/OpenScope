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

    //
    // The first received frame has no previous
    // opposite field available yet.
    //
    if (!hasPreviousFrame_ ||
        previousLuma_.size() != sampleCount)
    {
        std::copy_n(
            source,
            sampleCount,
            destination.first.y.data());

        std::copy_n(
            source,
            sampleCount,
            destination.second.y.data());

        previousLuma_.resize(
            sampleCount);

        std::copy_n(
            source,
            sampleCount,
            previousLuma_.data());

        hasPreviousFrame_ = true;

        return;
    }

    constexpr int firstFieldParity = 0;
    constexpr int secondFieldParity = 1;

    //
    // Output field 1:
    //
    // Current field 1 lines +
    // previous frame's field 2 lines.
    //
    for (int y = 0;
        y < height;
        ++y)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width);

        const std::uint16_t* sourceLine =
            ((y & 1) == firstFieldParity)
            ? source + lineOffset
            : previousLuma_.data() + lineOffset;

        std::copy_n(
            sourceLine,
            static_cast<std::size_t>(width),
            destination.first.y.data() +
            lineOffset);
    }

    //
    // Output field 2:
    //
    // Current field 2 lines +
    // current frame's field 1 lines.
    //
    // Both fields are already present at their
    // correct line positions in the captured frame.
    //
    std::copy_n(
        source,
        sampleCount,
        destination.second.y.data());

    //
    // Save this complete interlaced frame so its
    // second field can be paired with the first
    // field of the next captured frame.
    //
    previousLuma_.resize(
        sampleCount);

    std::copy_n(
        source,
        sampleCount,
        previousLuma_.data());

    hasPreviousFrame_ = true;
}