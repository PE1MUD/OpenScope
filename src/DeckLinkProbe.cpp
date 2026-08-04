#include "DeckLinkProbe.h"
#include "VideoEngine.h"
#include <DeckLinkAPI_h.h>
#include "DeckLinkInputCallback.h"

#include <QDebug>
#include <QString>

static void dumpConnections(const char* label, int64_t value)
{
    qDebug() << label;

    if (value & bmdVideoConnectionSDI)
        qDebug() << "    SDI";

    if (value & bmdVideoConnectionHDMI)
        qDebug() << "    HDMI";

    if (value & bmdVideoConnectionOpticalSDI)
        qDebug() << "    Optical SDI";

    if (value & bmdVideoConnectionComponent)
        qDebug() << "    Component";

    if (value & bmdVideoConnectionComposite)
        qDebug() << "    Composite";

    if (value & bmdVideoConnectionSVideo)
        qDebug() << "    S-Video";
}

static void dumpDisplayModes(IDeckLink* deckLink)
{
    IDeckLinkInput* input = nullptr;

    if (deckLink->QueryInterface(
        IID_IDeckLinkInput,
        reinterpret_cast<void**>(&input)) != S_OK)
    {
        qDebug() << "  No capture interface";
        return;
    }

    IDeckLinkDisplayModeIterator* modeIterator = nullptr;

    if (input->GetDisplayModeIterator(&modeIterator) != S_OK)
    {
        qDebug() << "  Cannot enumerate display modes";
        input->Release();
        return;
    }

    qDebug() << "  Input modes:";

    IDeckLinkDisplayMode* mode = nullptr;

    while (modeIterator->Next(&mode) == S_OK)
    {
        BSTR name = nullptr;

        if (mode->GetName(&name) == S_OK)
        {
            BMDTimeValue frameDuration = 0;
            BMDTimeScale timeScale = 0;

            mode->GetFrameRate(&frameDuration, &timeScale);

            const double fps =
                frameDuration != 0
                ? static_cast<double>(timeScale) /
                static_cast<double>(frameDuration)
                : 0.0;

            qDebug().noquote()
                << QString("    %1 - %2x%3 - %4 fps")
                .arg(QString::fromWCharArray(name))
                .arg(mode->GetWidth())
                .arg(mode->GetHeight())
                .arg(fps, 0, 'f', 3);

            SysFreeString(name);
        }

        mode->Release();
    }

    modeIterator->Release();
    input->Release();
}

static IDeckLinkInput* activeInput = nullptr;
static DeckLinkInputCallback* activeCallback = nullptr;

static void testPalInput(
    IDeckLink* deckLink,
    VideoEngine* videoEngine)
{
    IDeckLinkInput* input = nullptr;

    if (deckLink->QueryInterface(
        IID_IDeckLinkInput,
        reinterpret_cast<void**>(&input)) != S_OK)
    {
        qDebug() << "  No capture interface";
        return;
    }

    activeInput = input;
    activeCallback = new DeckLinkInputCallback(videoEngine);

    HRESULT result = input->SetCallback(activeCallback);

    if (FAILED(result))
    {
        qDebug() << "  Failed to set input callback";

        activeCallback->Release();
        activeCallback = nullptr;

        input->Release();
        activeInput = nullptr;
        return;
    }

    result = input->EnableVideoInput(
        bmdModePAL,
        bmdFormat8BitYUV,
        bmdVideoInputFlagDefault);

    if (FAILED(result))
    {
        qDebug() << "  Failed to enable PAL input";

        input->SetCallback(nullptr);

        activeCallback->Release();
        activeCallback = nullptr;

        input->Release();
        activeInput = nullptr;
        return;
    }

    result = input->StartStreams();

    if (SUCCEEDED(result))
    {
        qDebug() << "  PAL capture started";
    }
    else
    {
        qDebug() << "  Failed to start PAL capture";

        input->DisableVideoInput();
        input->SetCallback(nullptr);

        activeCallback->Release();
        activeCallback = nullptr;

        input->Release();
        activeInput = nullptr;
    }
}

static void dumpDevice(
    IDeckLink* deckLink,
    int index,
    VideoEngine* videoEngine)
{
    BSTR name = nullptr;

    if (deckLink->GetDisplayName(&name) == S_OK)
    {
        qDebug() << index << ":"
            << QString::fromWCharArray(name);

        SysFreeString(name);
    }

    IDeckLinkProfileAttributes* attributes = nullptr;

    if (deckLink->QueryInterface(
        IID_IDeckLinkProfileAttributes,
        reinterpret_cast<void**>(&attributes)) == S_OK)
    {
        int64_t value = 0;

        if (attributes->GetInt(
            BMDDeckLinkVideoInputConnections,
            &value) == S_OK)
        {
            dumpConnections("  Video inputs:", value);
        }

        if (attributes->GetInt(
            BMDDeckLinkVideoOutputConnections,
            &value) == S_OK)
        {
            dumpConnections("  Video outputs:", value);
        }

        attributes->Release();
    }

    dumpDisplayModes(deckLink);
    testPalInput(deckLink, videoEngine);
}

void deckLinkProbe(VideoEngine* videoEngine)
{
    IDeckLinkIterator* iterator = nullptr;

    const HRESULT result = CoCreateInstance(
        CLSID_CDeckLinkIterator,
        nullptr,
        CLSCTX_ALL,
        IID_IDeckLinkIterator,
        reinterpret_cast<void**>(&iterator));

    if (FAILED(result))
    {
        qDebug() << "No DeckLink driver found. HRESULT:"
            << QString::number(
                static_cast<unsigned long>(result),
                16);
        return;
    }

    IDeckLink* deckLink = nullptr;
    int index = 0;

    while (iterator->Next(&deckLink) == S_OK)
    {
        dumpDevice(deckLink, index, videoEngine);

        deckLink->Release();
        deckLink = nullptr;
        ++index;
    }

    iterator->Release();

    if (index == 0)
        qDebug() << "No DeckLink devices found.";
}

void deckLinkStop()
{
    if (activeInput != nullptr)
    {
        activeInput->SetCallback(nullptr);
        activeInput->StopStreams();
        activeInput->DisableVideoInput();

        activeInput->Release();
        activeInput = nullptr;
    }

    if (activeCallback != nullptr)
    {
        activeCallback->Release();
        activeCallback = nullptr;
    }

    qDebug() << "DeckLink capture stopped";
}