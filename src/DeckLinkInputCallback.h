#pragma once

#include <DeckLinkAPI_h.h>
#include "video/Uyvy422ToYuv444Converter.h"
#include "video/Yuv444Frame.h"

class VideoWidget;

class DeckLinkInputCallback final : public IDeckLinkInputCallback
{
public:
    explicit DeckLinkInputCallback(VideoWidget* videoWidget);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
        BMDVideoInputFormatChangedEvents notificationEvents,
        IDeckLinkDisplayMode* newDisplayMode,
        BMDDetectedVideoInputFormatFlags detectedSignalFlags) override;

    HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(
        IDeckLinkVideoInputFrame* videoFrame,
        IDeckLinkAudioInputPacket* audioPacket) override;

private:
    ULONG refCount_ = 1;
    unsigned int frameCount_ = 0;
    VideoWidget* videoWidget_ = nullptr;
    Uyvy422ToYuv444Converter converter_;
    Yuv444Frame convertedFrame_;
};