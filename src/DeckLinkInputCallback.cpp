#include <QImage>
#include <QMetaObject>
#include "DeckLinkInputCallback.h"
#include "VideoWidget.h"
#include <QDebug>

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

    QImage image(width, height, QImage::Format_RGB32);

    const auto* source = static_cast<const unsigned char*>(bytes);

    for (int y = 0; y < height; ++y)
    {
        const auto* src = source + y * rowBytes;
        auto* dst = reinterpret_cast<QRgb*>(image.scanLine(y));

        for (int x = 0; x < width; x += 2)
        {
            const int u = src[0] - 128;
            const int y0 = src[1] - 16;
            const int v = src[2] - 128;
            const int y1 = src[3] - 16;

            auto convert = [u, v](int yy)
                {
                    const int c = 298 * yy;
                    const int r = (c + 409 * v + 128) >> 8;
                    const int g = (c - 100 * u - 208 * v + 128) >> 8;
                    const int b = (c + 516 * u + 128) >> 8;

                    return qRgb(
                        qBound(0, r, 255),
                        qBound(0, g, 255),
                        qBound(0, b, 255));
                };

            dst[x] = convert(y0);

            if (x + 1 < width)
                dst[x + 1] = convert(y1);

            src += 4;
        }
    }

    videoBuffer->EndAccess(bmdBufferAccessRead);
    videoBuffer->Release();

    QMetaObject::invokeMethod(
        videoWidget_,
        [widget = videoWidget_, image = std::move(image)]() mutable
        {
            widget->setImage(image);
        },
        Qt::QueuedConnection);

    return S_OK;
}