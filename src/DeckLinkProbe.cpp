#include "DeckLinkProbe.h"
#include "VideoEngine.h"
#include <DeckLinkAPI_h.h>
#include "DeckLinkInputCallback.h"
#include "Video/Uyvy422ToYuv444Converter.h"
#include "Video/V210ToYuv444Converter.h"

#include <algorithm>
#include <QDebug>
#include <QMessageBox>
#include <QString>

static Uyvy422ToYuv444Converter uyvyConverter;
static V210ToYuv444Converter v210Converter;

static QString shortDeckLinkName(const QString& apiName)
{
    QString name = apiName.simplified();

    if (name.contains(
            QStringLiteral("Intensity Pro 4K"),
            Qt::CaseInsensitive))
    {
        return QStringLiteral("BMD IP 4K");
    }

    if (name.contains(
            QStringLiteral("Intensity Pro"),
            Qt::CaseInsensitive))
    {
        return QStringLiteral("BMD IP");
    }

    name.replace(
        QStringLiteral("Blackmagic Design"),
        QString(),
        Qt::CaseInsensitive);

    name.replace(
        QStringLiteral("Blackmagic"),
        QString(),
        Qt::CaseInsensitive);

    name.replace(
        QStringLiteral("DeckLink"),
        QString(),
        Qt::CaseInsensitive);

    name = name.simplified();

    if (name.isEmpty())
    {
        return QStringLiteral("BMD");
    }

    return QStringLiteral("BMD %1").arg(name);
}

static QString deckLinkApiName(IDeckLink* deckLink)
{
    BSTR name = nullptr;

    if (deckLink->GetModelName(&name) == S_OK &&
        name != nullptr)
    {
        const QString result =
            QString::fromWCharArray(name);

        SysFreeString(name);
        return result;
    }

    name = nullptr;

    if (deckLink->GetDisplayName(&name) == S_OK &&
        name != nullptr)
    {
        const QString result =
            QString::fromWCharArray(name);

        SysFreeString(name);
        return result;
    }

    return {};
}

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
static IDeckLinkConfiguration* activeConfiguration = nullptr;
static DeckLinkCompositeGainState activeCompositeGainState;
static bool activeConfigurationDirty = false;

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

    if (activeConfiguration != nullptr)
    {
        if (activeConfigurationDirty)
        {
            activeConfiguration->WriteConfigurationToPreferences();
        }

        activeConfiguration->Release();
        activeConfiguration = nullptr;
    }

    activeCompositeGainState = {};
    activeConfigurationDirty = false;

    IDeckLinkConfiguration* configuration = nullptr;

    if (deckLink->QueryInterface(
            IID_IDeckLinkConfiguration,
            reinterpret_cast<void**>(&configuration)) == S_OK)
    {
        activeConfiguration = configuration;

        IDeckLinkProfileAttributes* attributes = nullptr;

        if (deckLink->QueryInterface(
                IID_IDeckLinkProfileAttributes,
                reinterpret_cast<void**>(&attributes)) == S_OK)
        {
            double minimumDb = 0.0;
            double maximumDb = 0.0;

            if (attributes->GetFloat(
                    BMDDeckLinkVideoInputGainMinimum,
                    &minimumDb) == S_OK &&
                attributes->GetFloat(
                    BMDDeckLinkVideoInputGainMaximum,
                    &maximumDb) == S_OK)
            {
                activeCompositeGainState.minimumDb = minimumDb;
                activeCompositeGainState.maximumDb = maximumDb;
            }

            attributes->Release();
        }

        double value = 0.0;

        if (activeConfiguration->GetFloat(
                bmdDeckLinkConfigVideoInputCompositeLumaGain,
                &value) == S_OK)
        {
            activeCompositeGainState.lumaAvailable = true;
            activeCompositeGainState.lumaDb = value;
        }

        value = 0.0;

        if (activeConfiguration->GetFloat(
                bmdDeckLinkConfigVideoInputCompositeChromaGain,
                &value) == S_OK)
        {
            activeCompositeGainState.chromaAvailable = true;
            activeCompositeGainState.chromaDb = value;
        }
    }

    activeInput = input;
    activeCallback = new DeckLinkInputCallback(
        videoEngine,
        &v210Converter);

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
        bmdFormat10BitYUV,
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
    const QString apiName =
        deckLinkApiName(deckLink);

    qDebug() << index << ":" << apiName;

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

QString deckLinkProbe(VideoEngine* videoEngine)
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

        QMessageBox::warning(
            nullptr,
            "Blackmagic Desktop Video not found",
            "OpenScope requires Blackmagic Desktop Video 16.2 or later "
            "for DeckLink video capture.\n\n"
            "Please install Desktop Video 16.2 or later.\n\n"
            "OpenScope will continue without video capture.");

        return {};
    }

    IDeckLink* deckLink = nullptr;
    int index = 0;
    QString activeDeviceName;

    while (iterator->Next(&deckLink) == S_OK)
    {
        activeDeviceName =
            shortDeckLinkName(
                deckLinkApiName(deckLink));

        dumpDevice(deckLink, index, videoEngine);

        deckLink->Release();
        deckLink = nullptr;
        ++index;
    }

    iterator->Release();

    if (index == 0)
    {
        qDebug() << "No DeckLink devices found.";

        QMessageBox::warning(
            nullptr,
            "No Blackmagic DeckLink device found",
            "Blackmagic Desktop Video is installed, but no compatible "
            "DeckLink capture device was detected.\n\n"
            "OpenScope will continue without video capture.");
    }

    return activeDeviceName;
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

    if (activeConfiguration != nullptr)
    {
        if (activeConfigurationDirty)
        {
            activeConfiguration->WriteConfigurationToPreferences();
        }

        activeConfiguration->Release();
        activeConfiguration = nullptr;
    }

    activeCompositeGainState = {};
    activeConfigurationDirty = false;

    qDebug() << "DeckLink capture stopped";
}

DeckLinkCompositeGainState deckLinkCompositeGainState()
{
    return activeCompositeGainState;
}

bool deckLinkSetCompositeLumaGain(double gainDb)
{
    if (activeConfiguration == nullptr ||
        !activeCompositeGainState.lumaAvailable)
    {
        return false;
    }

    gainDb = std::clamp(
        gainDb,
        activeCompositeGainState.minimumDb,
        activeCompositeGainState.maximumDb);

    if (activeConfiguration->SetFloat(
            bmdDeckLinkConfigVideoInputCompositeLumaGain,
            gainDb) != S_OK)
    {
        return false;
    }

    activeCompositeGainState.lumaDb = gainDb;
    activeConfigurationDirty = true;
    return true;
}

bool deckLinkSetCompositeChromaGain(double gainDb)
{
    if (activeConfiguration == nullptr ||
        !activeCompositeGainState.chromaAvailable)
    {
        return false;
    }

    gainDb = std::clamp(
        gainDb,
        activeCompositeGainState.minimumDb,
        activeCompositeGainState.maximumDb);

    if (activeConfiguration->SetFloat(
            bmdDeckLinkConfigVideoInputCompositeChromaGain,
            gainDb) != S_OK)
    {
        return false;
    }

    activeCompositeGainState.chromaDb = gainDb;
    activeConfigurationDirty = true;
    return true;
}

bool deckLinkCommitConfiguration()
{
    if (activeConfiguration == nullptr)
    {
        return false;
    }

    if (!activeConfigurationDirty)
    {
        return true;
    }

    if (activeConfiguration->WriteConfigurationToPreferences() != S_OK)
    {
        return false;
    }

    activeConfigurationDirty = false;
    return true;
}
