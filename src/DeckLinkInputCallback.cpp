#include <QImage>
#include <QMetaObject>
#include "DeckLinkInputCallback.h"
#include "VideoWidget.h"
#include <QDebug>
#include <QPointer>

DeckLinkInputCallback::DeckLinkInputCallback(VideoWidget* videoWidget)
    : videoWidget_(videoWidget)
{}

HRESULT STDMETHODCALLTYPE DeckLinkInputCallback::QueryInterface(
    REFIID iid,
    LPVOID* ppv)
{
    if (ppv == nullptr)
        return E_POINTER;

    *ppv = nullptr;

    if (iid == IID_IUnknown ||
        iid == IID_IDeckLinkInputCallback)
    {
        *ppv = static_cast<IDeckLinkInputCallback*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE DeckLinkInputCallback::AddRef()
{
    return ++refCount_;
}

ULONG STDMETHODCALLTYPE DeckLinkInputCallback::Release()
{
    const ULONG count = --refCount_;
    //qDebug() << "Release =" << count;

    if (count == 0)
        delete this;

    return count;
}

HRESULT STDMETHODCALLTYPE DeckLinkInputCallback::VideoInputFormatChanged(
    BMDVideoInputFormatChangedEvents,
    IDeckLinkDisplayMode*,
    BMDDetectedVideoInputFormatFlags)
{
    qDebug() << "DeckLink input format changed";
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeckLinkInputCallback::VideoInputFrameArrived(
    IDeckLinkVideoInputFrame* videoFrame,
    IDeckLinkAudioInputPacket*)
{
    static int counter = 0;
    //qDebug() << "Frame" << ++counter;

    if (videoFrame == nullptr || videoWidget_ == nullptr)
        return S_OK;

    IDeckLinkVideoBuffer* videoBuffer = nullptr;

    if (videoFrame->QueryInterface(
        IID_IDeckLinkVideoBuffer,
        reinterpret_cast<void**>(&videoBuffer)) != S_OK)
    {
        return S_OK;
    }

    if (videoBuffer->StartAccess(bmdBufferAccessRead) != S_OK)
    {
        videoBuffer->Release();
        return S_OK;
    }

    void* bytes = nullptr;

    if (videoBuffer->GetBytes(&bytes) != S_OK || bytes == nullptr)
    {
        videoBuffer->EndAccess(bmdBufferAccessRead);
        videoBuffer->Release();
        return S_OK;
    }

    const int width = static_cast<int>(videoFrame->GetWidth());
    const int height = static_cast<int>(videoFrame->GetHeight());
    const int rowBytes = static_cast<int>(videoFrame->GetRowBytes());

    //QImage image(width, height, QImage::Format_RGB32);

    const auto* source = static_cast<const std::uint8_t*>(bytes);

    if (!converter_.convert(
        source,
        rowBytes,
        width,
        height,
        convertedFrame_))
    {
        videoBuffer->EndAccess(bmdBufferAccessRead);
        videoBuffer->Release();
        return S_OK;
    }

    QImage image(width, height, QImage::Format_RGB32);

    for (int y = 0; y < height; ++y)
    {
        auto* dst = reinterpret_cast<QRgb*>(image.scanLine(y));

        const std::size_t lineOffset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width);

        const auto* srcY = convertedFrame_.y.data() + lineOffset;
        const auto* srcU = convertedFrame_.u.data() + lineOffset;
        const auto* srcV = convertedFrame_.v.data() + lineOffset;

        for (int x = 0; x < width; ++x)
        {
            // Temporarily reduce the internal 16-bit samples back to their
            // original 8-bit code values for the existing RGB conversion.
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

    videoBuffer->EndAccess(bmdBufferAccessRead);
    videoBuffer->Release();

    QPointer<VideoWidget> widget = videoWidget_;

    QMetaObject::invokeMethod(
        videoWidget_,
        [widget, image = std::move(image)]() mutable
        {
            if (widget)
                widget->setImage(image);
        },
        Qt::QueuedConnection);

    return S_OK;
}