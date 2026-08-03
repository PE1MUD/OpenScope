#include "DeckLinkInputCallback.h"

#include <QDebug>

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
    if (videoFrame != nullptr)
    {
        ++frameCount_;

        if ((frameCount_ % 25) == 0)
        {
            qDebug() << "Frames:"
                << frameCount_
                << "-"
                << videoFrame->GetWidth()
                << "x"
                << videoFrame->GetHeight();
        }
    }

    return S_OK;
}