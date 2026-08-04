#include "DisplayConverter.h"

#include <QtGlobal>

QImage DisplayConverter::convert(const Yuv444Frame& frame) const
{
    if (frame.width <= 0 ||
        frame.height <= 0 ||
        frame.y.empty() ||
        frame.u.empty() ||
        frame.v.empty())
    {
        return {};
    }

    QImage image(
        frame.width,
        frame.height,
        QImage::Format_RGB32);

    for (int y = 0; y < frame.height; ++y)
    {
        auto* dst =
            reinterpret_cast<QRgb*>(image.scanLine(y));

        const std::size_t lineOffset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(frame.width);

        const auto* srcY = frame.y.data() + lineOffset;
        const auto* srcU = frame.u.data() + lineOffset;
        const auto* srcV = frame.v.data() + lineOffset;

        for (int x = 0; x < frame.width; ++x)
        {
            const int yy = static_cast<int>(srcY[x] >> 8) - 16;
            const int u = static_cast<int>(srcU[x] >> 8) - 128;
            const int v = static_cast<int>(srcV[x] >> 8) - 128;

            const int c = 298 * yy;
            const int r = (c + 409 * v + 128) >> 8;
            const int g = (c - 100 * u - 208 * v + 128) >> 8;
            const int b = (c + 516 * u + 128) >> 8;

            dst[x] = qRgb(
                qBound(0, r, 255),
                qBound(0, g, 255),
                qBound(0, b, 255));
        }
    }

    return image;
}