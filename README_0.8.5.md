# OpenScope 0.8.5 — rolling changelog

## Baseline

- Release baseline created from 0.8.4 Delta82 plus the final vectorscope information-card resize fix.
- PC vectorscope SOURCE and LINE/TARGETS/MATRIX cards remain present through normal resize transitions; PROCESSING is dropped first when vertical space is tight.
- Release builds use version `0.8.5` without a `Delta 0` suffix in the window title.

## Human-readable release notes

OpenScope 0.8.5 consolidates the performance, startup, display, profiler and usability work completed during the 0.8.4 delta cycle. The final release polish improves vectorscope information-panel stability during resize. WSS decoding is planned as separate follow-up work after this baseline.

## Delta log

### Delta 1 — vectorscope centering

- Restored horizontal centering of the PC vectorscope inside the remaining scope area after the left information column is reserved.
- Fixes the 16:9 regression where the vectorscope circle was pinned against the right edge.
- Information-card layout and quad workspace behavior are otherwise unchanged.

## Delta 2 — Quad F2 state fix

- F2 now acts only when a single Video/Waveform/Vectorscope viewport is maximized.
- In Matrix/quad mode F2 is intentionally ignored, both windowed and F11 fullscreen.
- F11 no longer forces Config to detach from quadrant 4.
- The normal `Config (F2)` menu action remains available outside F11.
