#pragma once

enum class VideoColorStandard
{
    Rec601_625,
    Rec601_525,
    Rec709,
    Rec2020
};

enum class VideoRange
{
    Legal,
    Full
};

struct VideoStandard
{
    VideoColorStandard colorStandard;
    VideoRange range;

    static constexpr VideoStandard pal625()
    {
        return
        {
            VideoColorStandard::Rec601_625,
            VideoRange::Legal
        };
    }

    static constexpr VideoStandard ntsc525()
    {
        return
        {
            VideoColorStandard::Rec601_525,
            VideoRange::Legal
        };
    }
};

struct YuvCoefficients
{
    double kr;
    double kb;
};

constexpr YuvCoefficients coefficients(
    VideoColorStandard standard)
{
    switch (standard)
    {
    case VideoColorStandard::Rec601_625:
    case VideoColorStandard::Rec601_525:
        return
        {
            0.299,
            0.114
        };

    case VideoColorStandard::Rec709:
        return
        {
            0.2126,
            0.0722
        };

    case VideoColorStandard::Rec2020:
        return
        {
            0.2627,
            0.0593
        };
    }

    return
    {
        0.299,
        0.114
    };
}