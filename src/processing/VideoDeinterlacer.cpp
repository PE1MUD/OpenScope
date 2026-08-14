#include "VideoDeinterlacer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace
{
    constexpr int kFirstFieldParity = 0;
    constexpr int kSecondFieldParity = 1;

    constexpr int kMotionThreshold = 1024;
    constexpr int kCombThreshold = 1536;
    constexpr int kMaskExpansion = 3;

    inline int average2(
        int a,
        int b) noexcept
    {
        return
            (a + b + 1) >>
            1;
    }

    //
    // Edge-directed spatial interpolation.
    //
    // IMPORTANT:
    // This is called ONLY for pixels already classified
    // as moving + combed.
    //
    int spatialPredict(
        const std::uint16_t* upperLine,
        const std::uint16_t* lowerLine,
        int width,
        int x) noexcept
    {
        if (x < 2 ||
            x >= width - 2)
        {
            return
                (static_cast<int>(upperLine[x]) +
                    static_cast<int>(lowerLine[x]) +
                    1) >>
                1;
        }

        int bestScore =
            0x7fffffff;

        int bestDirection =
            0;

        for (int direction = -1;
            direction <= 1;
            ++direction)
        {
            int score =
                0;

            for (int offset = -1;
                offset <= 1;
                ++offset)
            {
                const int upper =
                    static_cast<int>(
                        upperLine[
                            x +
                                offset -
                                direction]);

                const int lower =
                    static_cast<int>(
                        lowerLine[
                            x +
                                offset +
                                direction]);

                score +=
                    std::abs(
                        upper -
                        lower);
            }

            if (score < bestScore)
            {
                bestScore =
                    score;

                bestDirection =
                    direction;
            }
        }

        const int upper =
            static_cast<int>(
                upperLine[
                    x -
                        bestDirection]);

        const int lower =
            static_cast<int>(
                lowerLine[
                    x +
                        bestDirection]);

        return
            (upper +
                lower +
                1) >>
            1;
    }
    int temporalClamp(
        int spatialValue,
        int before,
        int after) noexcept
    {
        const int temporalCenter =
            average2(
                before,
                after);

        const int temporalDifference =
            std::abs(
                before -
                after);

        const int temporalLimit =
            std::max(
                temporalDifference >> 1,
                256);

        return std::clamp(
            spatialValue,
            temporalCenter -
            temporalLimit,
            temporalCenter +
            temporalLimit);
    }

    //
    // Cheap first-stage detector.
    //
    // No spatialPredict().
    // No clamp per sample.
    // Only direct neighbouring memory accesses.
    //
    inline bool needsInterpolation(
        const std::uint16_t* beforeLine,
        const std::uint16_t* afterLine,
        const std::uint16_t* upperLine,
        const std::uint16_t* lowerLine,
        int width,
        int x,
        bool weaveFromBefore) noexcept
    {
        int temporalDifference =
            0;

        int combDifference =
            0;

        const int firstX =
            std::max(
                0,
                x - 1);

        const int lastX =
            std::min(
                width - 1,
                x + 1);

        for (int testX = firstX;
            testX <= lastX;
            ++testX)
        {
            const int before =
                static_cast<int>(
                    beforeLine[testX]);

            const int after =
                static_cast<int>(
                    afterLine[testX]);

            temporalDifference +=
                std::abs(
                    before -
                    after);

            const int upper =
                static_cast<int>(
                    upperLine[testX]);

            const int lower =
                static_cast<int>(
                    lowerLine[testX]);

            const int verticalPrediction =
                average2(
                    upper,
                    lower);

            const int weave =
                weaveFromBefore
                ? before
                : after;

            combDifference +=
                std::abs(
                    weave -
                    verticalPrediction);
        }

        //
        // Maximum three samples.
        // Compare accumulated values directly,
        // avoiding divisions.
        //
        const int sampleCount =
            lastX -
            firstX +
            1;

        return
            temporalDifference >
            kMotionThreshold *
            sampleCount &&
            combDifference >
            kCombThreshold *
            sampleCount;
    }
}

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
    // First frame:
    // just establish history.
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

        previousLuma_.assign(
            source,
            source + sampleCount);

        previousPreviousLuma_.clear();

        hasPreviousFrame_ = true;

        return;
    }

    //
    // Second frame:
    // still no complete older / center / newer set.
    //
    if (previousPreviousLuma_.size() !=
        sampleCount)
    {
        std::copy_n(
            previousLuma_.data(),
            sampleCount,
            destination.first.y.data());

        std::copy_n(
            previousLuma_.data(),
            sampleCount,
            destination.second.y.data());

        previousPreviousLuma_ =
            previousLuma_;

        previousLuma_.assign(
            source,
            source + sampleCount);

        return;
    }

    //
    // Temporal layout:
    //
    // older  = previousPreviousLuma_
    // center = previousLuma_
    // newer  = source
    //
    const std::uint16_t* older =
        previousPreviousLuma_.data();

    const std::uint16_t* center =
        previousLuma_.data();

    const std::uint16_t* newer =
        source;

    //
    // ============================================================
    // FIRST OUTPUT
    //
    // Native field 1:
    //     center
    //
    // Opposite field baseline:
    //     older field 2
    //
    // ============================================================
    //

    for (int y = 0;
        y < height;
        ++y)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width);

        std::uint16_t* destinationLine =
            destination.first.y.data() +
            lineOffset;

        if ((y & 1) ==
            kFirstFieldParity)
        {
            //
            // Genuine field line.
            // Never touch it.
            //
            std::copy_n(
                center +
                lineOffset,
                static_cast<std::size_t>(width),
                destinationLine);

            continue;
        }

        //
        // Default is pure temporal weave.
        //
        const std::uint16_t* beforeLine =
            older +
            lineOffset;

        std::copy_n(
            beforeLine,
            static_cast<std::size_t>(width),
            destinationLine);

        if (y == 0 ||
            y == height - 1)
        {
            continue;
        }

        const std::uint16_t* afterLine =
            center +
            lineOffset;

        const std::uint16_t* upperLine =
            center +
            static_cast<std::size_t>(y - 1) *
            static_cast<std::size_t>(width);

        const std::uint16_t* lowerLine =
            center +
            static_cast<std::size_t>(y + 1) *
            static_cast<std::size_t>(width);

        for (int x = 0;
            x < width;
            ++x)
        {
            bool interpolate =
                false;

            const int firstTestX =
                std::max(
                    0,
                    x - kMaskExpansion);

            const int lastTestX =
                std::min(
                    width - 1,
                    x + kMaskExpansion);

            for (int testX = firstTestX;
                testX <= lastTestX;
                ++testX)
            {
                if (needsInterpolation(
                    beforeLine,
                    afterLine,
                    upperLine,
                    lowerLine,
                    width,
                    testX,
                    true))
                {
                    interpolate =
                        true;

                    break;
                }
            }

            if (!interpolate)
            {
                continue;
            }

            const int spatial =
                spatialPredict(
                    upperLine,
                    lowerLine,
                    width,
                    x);

            const int before =
                static_cast<int>(
                    beforeLine[x]);

            const int after =
                static_cast<int>(
                    afterLine[x]);

            const int output =
                temporalClamp(
                    spatial,
                    before,
                    after);

            destinationLine[x] =
                static_cast<std::uint16_t>(
                    std::clamp(
                        output,
                        0,
                        65535));
        }
    }

    //
    // ============================================================
    // SECOND OUTPUT
    //
    // Native field 2:
    //     center
    //
    // Opposite field baseline:
    //     center field 1
    //
    // Temporal neighbour:
    //     newer field 1
    //
    // ============================================================
    //

    std::copy_n(
        center,
        sampleCount,
        destination.second.y.data());

    for (int y = 1;
        y < height - 1;
        ++y)
    {
        //
        // Only the temporally older field-1 lines
        // are candidates.
        //
        if ((y & 1) !=
            kFirstFieldParity)
        {
            continue;
        }

        const std::size_t lineOffset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width);

        const std::uint16_t* beforeLine =
            center +
            lineOffset;

        const std::uint16_t* afterLine =
            newer +
            lineOffset;

        const std::uint16_t* upperLine =
            center +
            static_cast<std::size_t>(y - 1) *
            static_cast<std::size_t>(width);

        const std::uint16_t* lowerLine =
            center +
            static_cast<std::size_t>(y + 1) *
            static_cast<std::size_t>(width);

        std::uint16_t* destinationLine =
            destination.second.y.data() +
            lineOffset;

        for (int x = 0;
            x < width;
            ++x)
        {
            bool interpolate =
                false;

            const int firstTestX =
                std::max(
                    0,
                    x - kMaskExpansion);

            const int lastTestX =
                std::min(
                    width - 1,
                    x + kMaskExpansion);

            for (int testX = firstTestX;
                testX <= lastTestX;
                ++testX)
            {
                if (needsInterpolation(
                    beforeLine,
                    afterLine,
                    upperLine,
                    lowerLine,
                    width,
                    testX,
                    true))
                {
                    interpolate =
                        true;

                    break;
                }
            }

            if (!interpolate)
            {
                continue;
            }

            const int spatial =
                spatialPredict(
                    upperLine,
                    lowerLine,
                    width,
                    x);

            const int before =
                static_cast<int>(
                    beforeLine[x]);

            const int after =
                static_cast<int>(
                    afterLine[x]);

            const int output =
                temporalClamp(
                    spatial,
                    before,
                    after);

            destinationLine[x] =
                static_cast<std::uint16_t>(
                    std::clamp(
                        output,
                        0,
                        65535));
        }
    }

    //
    // Advance history.
    //
    previousPreviousLuma_.swap(
        previousLuma_);

    previousLuma_.assign(
        source,
        source + sampleCount);
}