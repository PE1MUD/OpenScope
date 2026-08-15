#include "NoiseReducer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>

namespace
{
    inline std::uint32_t absoluteDifference(
        std::uint16_t first,
        std::uint16_t second) noexcept
    {
        return first >= second
            ? static_cast<std::uint32_t>(
                first - second)
            : static_cast<std::uint32_t>(
                second - first);
    }

    inline __m256i load8Unsigned16As32(
        const std::uint16_t* source) noexcept
    {
        const __m128i packed =
            _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(
                    source));

        return
            _mm256_cvtepu16_epi32(
                packed);
    }

    inline void accumulateNeighbour(
        __m256i neighbour,
        __m256i center,
        __m256i threshold,
        __m256i one,
        __m256i& sum,
        __m256i& count) noexcept
    {
        const __m256i difference =
            _mm256_abs_epi32(
                _mm256_sub_epi32(
                    neighbour,
                    center));

        const __m256i greaterThanThreshold =
            _mm256_cmpgt_epi32(
                difference,
                threshold);

        const __m256i acceptedMask =
            _mm256_xor_si256(
                greaterThanThreshold,
                _mm256_set1_epi32(-1));

        sum =
            _mm256_add_epi32(
                sum,
                _mm256_and_si256(
                    neighbour,
                    acceptedMask));

        count =
            _mm256_add_epi32(
                count,
                _mm256_and_si256(
                    one,
                    acceptedMask));
    }

    inline void storeRoundedAverage(
        __m256i sum,
        __m256i count,
        std::uint16_t* destination) noexcept
    {
        // Exact integer rule of the scalar implementation:
        //
        //     (sum + count / 2) / count
        //
        // All values here are small enough to be represented exactly as
        // float integers. Truncation after the division therefore gives
        // the same positive integer rounding rule.
        const __m256i numerator =
            _mm256_add_epi32(
                sum,
                _mm256_srli_epi32(
                    count,
                    1));

        const __m256 quotient =
            _mm256_div_ps(
                _mm256_cvtepi32_ps(
                    numerator),
                _mm256_cvtepi32_ps(
                    count));

        const __m256i values32 =
            _mm256_cvttps_epi32(
                quotient);

        const __m128i low =
            _mm256_castsi256_si128(
                values32);

        const __m128i high =
            _mm256_extracti128_si256(
                values32,
                1);

        const __m128i packed =
            _mm_packus_epi32(
                low,
                high);

        _mm_storeu_si128(
            reinterpret_cast<__m128i*>(
                destination),
            packed);
    }
}

void NoiseReducer::process(
    const Yuv444Frame& source,
    Yuv444Frame& destination)
{
    if (source.width <= 0 ||
        source.height <= 0 ||
        source.y.empty() ||
        source.u.empty() ||
        source.v.empty())
    {
        return;
    }

    if (destination.width != source.width ||
        destination.height != source.height)
    {
        destination.resize(
            source.width,
            source.height);
    }

    filterPlane(
        source.y,
        destination.y,
        source.width,
        source.height,
        kLumaThreshold);

    filterPlane(
        source.u,
        destination.u,
        source.width,
        source.height,
        kChromaThreshold);

    filterPlane(
        source.v,
        destination.v,
        source.width,
        source.height,
        kChromaThreshold);
}

void NoiseReducer::filterPlane(
    const std::vector<std::uint16_t>& source,
    std::vector<std::uint16_t>& destination,
    int width,
    int height,
    std::uint16_t threshold)
{
    const std::size_t expectedSize =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);

    if (source.size() < expectedSize ||
        destination.size() < expectedSize)
    {
        return;
    }

    if (width < 3 ||
        height < 3)
    {
        std::copy_n(
            source.begin(),
            expectedSize,
            destination.begin());
        return;
    }

    std::copy_n(
        source.begin(),
        static_cast<std::size_t>(width),
        destination.begin());

    const std::size_t lastLineOffset =
        static_cast<std::size_t>(
            height - 1) *
        static_cast<std::size_t>(width);

    std::copy_n(
        source.begin() +
        static_cast<std::ptrdiff_t>(
            lastLineOffset),
        static_cast<std::size_t>(width),
        destination.begin() +
        static_cast<std::ptrdiff_t>(
            lastLineOffset));

    const __m256i thresholdVector =
        _mm256_set1_epi32(
            static_cast<int>(
                threshold));

    const __m256i one =
        _mm256_set1_epi32(
            1);

    for (int y = 1;
        y < height - 1;
        ++y)
    {
        const std::size_t lineOffset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width);

        destination[lineOffset] =
            source[lineOffset];

        destination[
            lineOffset +
                static_cast<std::size_t>(
                    width - 1)] =
            source[
                lineOffset +
                    static_cast<std::size_t>(
                        width - 1)];

            int x = 1;

            const int vectorEnd =
                width - 1 - 7;

            for (;
                x < vectorEnd;
                x += 8)
            {
                const std::size_t centerIndex =
                    lineOffset +
                    static_cast<std::size_t>(x);

                const __m256i center =
                    load8Unsigned16As32(
                        source.data() +
                        centerIndex);

                __m256i sum =
                    _mm256_setzero_si256();

                __m256i count =
                    _mm256_setzero_si256();

                for (int offsetY = -1;
                    offsetY <= 1;
                    ++offsetY)
                {
                    const std::size_t neighbourLine =
                        static_cast<std::size_t>(
                            y + offsetY) *
                        static_cast<std::size_t>(
                            width);

                    for (int offsetX = -1;
                        offsetX <= 1;
                        ++offsetX)
                    {
                        const std::size_t neighbourIndex =
                            neighbourLine +
                            static_cast<std::size_t>(
                                x + offsetX);

                        const __m256i neighbour =
                            load8Unsigned16As32(
                                source.data() +
                                neighbourIndex);

                        accumulateNeighbour(
                            neighbour,
                            center,
                            thresholdVector,
                            one,
                            sum,
                            count);
                    }
                }

                storeRoundedAverage(
                    sum,
                    count,
                    destination.data() +
                    centerIndex);
            }

            for (;
                x < width - 1;
                ++x)
            {
                const std::size_t centerIndex =
                    lineOffset +
                    static_cast<std::size_t>(x);

                const std::uint16_t center =
                    source[centerIndex];

                std::uint32_t sum = 0;
                std::uint32_t count = 0;

                for (int offsetY = -1;
                    offsetY <= 1;
                    ++offsetY)
                {
                    const std::size_t neighbourLine =
                        static_cast<std::size_t>(
                            y + offsetY) *
                        static_cast<std::size_t>(
                            width);

                    for (int offsetX = -1;
                        offsetX <= 1;
                        ++offsetX)
                    {
                        const std::uint16_t neighbour =
                            source[
                                neighbourLine +
                                    static_cast<std::size_t>(
                                        x + offsetX)];

                        if (absoluteDifference(
                            neighbour,
                            center) <= threshold)
                        {
                            sum += neighbour;
                            ++count;
                        }
                    }
                }

                destination[centerIndex] =
                    static_cast<std::uint16_t>(
                        (sum + count / 2u) /
                        count);
            }
    }
}