#pragma once

#include <QString>

class VideoEngine;

struct DeckLinkCompositeGainState
{
    bool lumaAvailable = false;
    bool chromaAvailable = false;

    double minimumDb = 0.0;
    double maximumDb = 0.0;
    double lumaDb = 0.0;
    double chromaDb = 0.0;
};

QString deckLinkProbe(VideoEngine* videoEngine);
void deckLinkStop();

DeckLinkCompositeGainState deckLinkCompositeGainState();
bool deckLinkSetCompositeLumaGain(double gainDb);
bool deckLinkSetCompositeChromaGain(double gainDb);
bool deckLinkCommitConfiguration();
