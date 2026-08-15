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


bool VideoDeinterlacer::beginFrame(
    const std::uint16_t* source,
    int width,
    int height,
    ProgressiveLumaPair& destination)
{
    if (source == nullptr ||
        width <= 0 ||
        height <= 0)
    {
        return false;
    }

    currentSource_ = source;
    currentDestination_ = &destination;
    currentWidth_ = width;
    currentHeight_ = height;
    currentSampleCount_ =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);

    destination.first.resize(
        width,
        height);

    destination.second.resize(
        width,
        height);

    if (!hasPreviousFrame_ ||
        previousLuma_.size() != currentSampleCount_)
    {
        currentMode_ =
            FrameMode::First;
    }
    else if (previousPreviousLuma_.size() !=
        currentSampleCount_)
    {
        currentMode_ =
            FrameMode::Second;
    }
    else
    {
        currentMode_ =
            FrameMode::Normal;
    }

    return true;
}

void VideoDeinterlacer::processRange(
    int firstLine,
    int lastLine)
{
    if (currentSource_ == nullptr ||
        currentDestination_ == nullptr ||
        currentWidth_ <= 0 ||
        currentHeight_ <= 0)
    {
        return;
    }

    firstLine =
        std::clamp(
            firstLine,
            0,
            currentHeight_);

    lastLine =
        std::clamp(
            lastLine,
            firstLine,
            currentHeight_);

    const std::size_t width =
        static_cast<std::size_t>(
            currentWidth_);

    if (currentMode_ ==
        FrameMode::First)
    {
        for (int y = firstLine;
            y < lastLine;
            ++y)
        {
            const std::size_t lineOffset =
                static_cast<std::size_t>(y) *
                width;

            std::copy_n(
                currentSource_ +
                lineOffset,
                width,
                currentDestination_->
                first.y.data() +
                lineOffset);

            std::copy_n(
                currentSource_ +
                lineOffset,
                width,
                currentDestination_->
                second.y.data() +
                lineOffset);
        }

        return;
    }

    if (currentMode_ ==
        FrameMode::Second)
    {
        for (int y = firstLine;
            y < lastLine;
            ++y)
        {
            const std::size_t lineOffset =
                static_cast<std::size_t>(y) *
                width;

            std::copy_n(
                previousLuma_.data() +
                lineOffset,
                width,
                currentDestination_->
                first.y.data() +
                lineOffset);

            std::copy_n(
                previousLuma_.data() +
                lineOffset,
                width,
                currentDestination_->
                second.y.data() +
                lineOffset);
        }

        return;
    }

    const std::uint16_t* older =
        previousPreviousLuma_.data();

    const std::uint16_t* center =
        previousLuma_.data();

    const std::uint16_t* newer =
        currentSource_;

    for (int y = firstLine;
        y < lastLine;
        ++y)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(y) *
            width;

        std::uint16_t* firstDestinationLine =
            currentDestination_->
            first.y.data() +
            lineOffset;

        if ((y & 1) ==
            kFirstFieldParity)
        {
            std::copy_n(
                center +
                lineOffset,
                width,
                firstDestinationLine);
        }
        else
        {
            const std::uint16_t* beforeLine =
                older +
                lineOffset;

            std::copy_n(
                beforeLine,
                width,
                firstDestinationLine);

            if (y > 0 &&
                y < currentHeight_ - 1)
            {
                const std::uint16_t* afterLine =
                    center +
                    lineOffset;

                const std::uint16_t* upperLine =
                    center +
                    static_cast<std::size_t>(
                        y - 1) *
                    width;

                const std::uint16_t* lowerLine =
                    center +
                    static_cast<std::size_t>(
                        y + 1) *
                    width;

                for (int x = 0;
                    x < currentWidth_;
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
                            currentWidth_ - 1,
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
                            currentWidth_,
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
                            currentWidth_,
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

                    firstDestinationLine[x] =
                        static_cast<std::uint16_t>(
                            std::clamp(
                                output,
                                0,
                                65535));
                }
            }
        }

        std::uint16_t* secondDestinationLine =
            currentDestination_->
            second.y.data() +
            lineOffset;

        std::copy_n(
            center +
            lineOffset,
            width,
            secondDestinationLine);

        if (y <= 0 ||
            y >= currentHeight_ - 1 ||
            (y & 1) !=
            kFirstFieldParity)
        {
            continue;
        }

        const std::uint16_t* beforeLine =
            center +
            lineOffset;

        const std::uint16_t* afterLine =
            newer +
            lineOffset;

        const std::uint16_t* upperLine =
            center +
            static_cast<std::size_t>(
                y - 1) *
            width;

        const std::uint16_t* lowerLine =
            center +
            static_cast<std::size_t>(
                y + 1) *
            width;

        for (int x = 0;
            x < currentWidth_;
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
                    currentWidth_ - 1,
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
                    currentWidth_,
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
                    currentWidth_,
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

            secondDestinationLine[x] =
                static_cast<std::uint16_t>(
                    std::clamp(
                        output,
                        0,
                        65535));
        }
    }
}

void VideoDeinterlacer::endFrame()
{
    if (currentSource_ == nullptr ||
        currentSampleCount_ == 0)
    {
        return;
    }

    if (currentMode_ ==
        FrameMode::First)
    {
        previousLuma_.assign(
            currentSource_,
            currentSource_ +
            currentSampleCount_);

        previousPreviousLuma_.clear();

        hasPreviousFrame_ = true;
    }
    else if (currentMode_ ==
        FrameMode::Second)
    {
        previousPreviousLuma_ =
            previousLuma_;

        previousLuma_.assign(
            currentSource_,
            currentSource_ +
            currentSampleCount_);
    }
    else
    {
        previousPreviousLuma_.swap(
            previousLuma_);

        previousLuma_.assign(
            currentSource_,
            currentSource_ +
            currentSampleCount_);
    }

    currentSource_ = nullptr;
    currentDestination_ = nullptr;
    currentWidth_ = 0;
    currentHeight_ = 0;
    currentSampleCount_ = 0;
}

void VideoDeinterlacer::deinterlace(
    const std::uint16_t* source,
    int width,
    int height,
    ProgressiveLumaPair& destination)
{
    if (!beginFrame(
        source,
        width,
        height,
        destination))
    {
        return;
    }

    processRange(
        0,
        height);

    endFrame();
}