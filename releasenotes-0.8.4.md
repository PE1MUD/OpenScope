# OpenScope 0.8.4 -- Test Release

OpenScope 0.8.4 richt zich vooral op **performance, timing en stabiliteit**. De interne verwerking van video, waveform en vectorscope is flink geoptimaliseerd, met name voor systemen met minder CPU-reserves.

## Wat is verbeterd

### Aanzienlijk betere CPU-efficiëntie

-   Zware waveform-bewerkingen worden beter over beschikbare CPU-cores verdeeld.
-   Workers kunnen elkaar helpen wanneer daar capaciteit voor beschikbaar is.
-   Onnodige geheugenallocaties en beeldkopieën zijn verminderd.
-   Tussenresultaten worden waar mogelijk hergebruikt.

### Verbeterde scheduling

-   Tijdkritische videoverwerking krijgt voorrang wanneer dat nodig is.
-   Vrijgekomen CPU-capaciteit wordt daarna gebruikt voor waveform- en vectorscope-verwerking.
-   De scheduling is zo ingericht dat het besturingssysteem voldoende ruimte houdt voor andere latency-gevoelige taken.

### Snellere waveform-rendering

-   Zowel de waveform op het scherm als de Spout-output zijn geoptimaliseerd.
-   Zware rasterbewerkingen kunnen parallel worden uitgevoerd.
-   Verschillende renderstappen gebruiken efficiënter vooraf gereserveerde buffers.
-   Dubbele verwerking van tussenresultaten is verminderd.

### Verbeterde Spout-output

-   Efficiëntere waveform-output.
-   Betere samenwerking tussen de verschillende render-workers.
-   Minder dubbele verwerking en onnodige kopieeracties.

### Fullscreen-verbeteringen

-   Wijzigingen in aspectratio worden nu correct verwerkt terwijl OpenScope in F11-fullscreen staat.

### Uitgebreide performance-diagnostiek

-   De interne performance-monitor toont nauwkeuriger waar verwerkingstijd wordt besteed.
-   Workeractiviteit, parallel werk en scheduling zijn beter zichtbaar.
-   De timingweergave maakt de critical path en beschikbare CPU-capaciteit beter inzichtelijk.
-   Het uitgebreide timingrapport kan worden gescrold.

## Voor testers

Let tijdens het testen vooral op:

-   vloeiende video en audio tijdens langdurig gebruik;
-   waveform en vectorscope die blijven doorlopen zonder haperingen;
-   schakelen tussen Quad en fullscreen;
-   F11-fullscreen en het wijzigen van de aspectratio;
-   Spout Video, Waveform en Vectorscope in OBS;
-   gedrag op tragere of oudere CPU's;
-   onverwachte CPU-pieken, freezes of korte zwarte beelden.

Bij een probleem zijn een **screenshot van het Performance-venster**, de gebruikte videobron en een korte beschrijving van wat je op dat moment deed bijzonder welkom.

## Bekend aandachtspunt

De optimalisatie van de waveform-renderer is nog niet afgerond. Enkele renderstappen kunnen nog relatief veel verwerkingstijd gebruiken. Deze worden in een volgende versie verder onderzocht.

------------------------------------------------------------------------

**OpenScope 0.8.4 is een testversie.** Feedback over beeldkwaliteit, stabiliteit en performance is zeer welkom.
