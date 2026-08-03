#include "DeckLinkProbe.h"

#include <DeckLinkAPI_h.h>

#include <QDebug>
#include <QString>

void deckLinkProbe()
{
    IDeckLinkIterator* iterator = nullptr;

    const HRESULT result = CoCreateInstance(
        CLSID_CDeckLinkIterator,
        nullptr,
        CLSCTX_ALL,
        IID_IDeckLinkIterator,
        reinterpret_cast<void**>(&iterator)
    );

    if (FAILED(result))
    {
        qDebug() << "No DeckLink driver found. HRESULT:"
            << QString::number(static_cast<unsigned long>(result), 16);
        return;
    }

    IDeckLink* deckLink = nullptr;
    int index = 0;

    while (iterator->Next(&deckLink) == S_OK)
    {
        BSTR name = nullptr;

        if (deckLink->GetDisplayName(&name) == S_OK)
        {
            qDebug() << index << ":"
                << QString::fromWCharArray(name);

            SysFreeString(name);
        }

        deckLink->Release();
        ++index;
    }

    iterator->Release();

    if (index == 0)
        qDebug() << "No DeckLink devices found.";
}