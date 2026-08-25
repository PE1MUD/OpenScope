# OpenScope 0.8.2

OpenScope 0.8.2 is primarily a waveform-rendering quality and performance release.

The accepted 0.8.0 **CatWuzle** waveform renderer is restored as the visual baseline, then extended with the **MUD** dense-trace optimization. Dense high-frequency regions can bypass expensive antialiasing while preserving the exact Catmull/CatWuzle waveform geometry. Normal and isolated trace regions continue to use the full CatWuzle quality path.

0.8.2 also adds finite non-blocking renderer diagnostics and makes vectorscope gamut errors directly visible at the offending chroma locations.

---

## Release notes

### CatWuzle waveform renderer

- Restored the accepted 0.8.0 CatWuzle waveform appearance as the production visual baseline.
- Preserves Catmull-Rom waveform reconstruction.
- Preserves analytic subpixel antialiasing with 2x2 coverage sampling.
- Preserves round-join correction and target-aware beam scaling.
- Preserves the existing Beam Glow and ScopePhor presentation.
- Keeps the small common pre-ScopePhor resolve used by the accepted CatWuzle path.
- Dense-trace optimization does not filter, smooth, average, or otherwise alter the source waveform geometry.

### MUD dense-trace optimization

Added the **MUD** detector for dense, steep, high-frequency waveform regions where full subpixel AA is expensive but contributes little useful visual information at the target pixel resolution.

Production policy:

- Normal / isolated waveform regions use the full CatWuzle 2x2 analytic-AA path.
- Accepted dense MUD regions use the same fitted Catmull/CatWuzle geometry but rasterize directly without AA.
- The intermediate experimental “cheap AA” path was removed; MUD means AA is genuinely off.
- A short full-AA stitch zone is retained around MUD/non-MUD boundaries to avoid visible transition artifacts.

The detector uses local trace geometry rather than modifying signal samples.

### MUD white-latch detector

The MUD detector was optimized without intentionally changing the accepted dense-packet semantics.

- Detection is evaluated per monotone waveform flank.
- `abs(dY) >= 10 px` is an entry qualification for changing from normal/full-AA state to MUD state, not a continuous requirement.
- While a flank is still normal, nearby dense-trace evidence continues to be tested.
- Once a monotone flank is proven dense/MUD, that result is latched for the remainder of the flank.
- The latch resets at the next `dY` sign change, i.e. around a waveform top or bottom.
- Neighbour lookup is local in X and stops beyond approximately 8 px.
- Existing dense-packet qualification remains based on sustained local density, packet length/width, and Y overlap.

This substantially reduces detector work while preserving the accepted visual classification.

### Renderer diagnostics

Added a finite, non-blocking `TraceLogger` path for detailed renderer timing and event analysis.

The waveform diagnostics include:

- waveform render begin/end;
- MUD detector time and neighbour-probe count;
- MUD run/packet statistics;
- MUD/no-AA segment count;
- waveform raster/core timing;
- glow timing;
- resolve timing;
- persistence/ScopePhor timing;
- chroma compose timing;
- overlay timing.

Logging is designed so producer/render threads do not perform file I/O directly.

- Debug builds can write the finite diagnostic trace to `log.txt`.
- Release builds compile trace call sites to no-ops through the central build configuration and do not start the logging writer/file path.

### Debug / production cleanup

Experimental waveform-development controls and overlays were removed from the production UI.

Removed from production:

- red/white MUD detector visualization;
- POLYGRAPH detector truth bars;
- old AA activity strip;
- experimental `Adaptive width` control;
- experimental `Y supersampling` control.

Debug-only facilities:

- waveform RAW capture;
- waveform Core width tuning.

Release keeps the production waveform core width fixed at **1.0 px**.

A central `BuildConfig.h` compile-time configuration is used instead of scattering preprocessor conditionals throughout the renderer.

### Vectorscope gamut offender overlay

The existing gamut decision remains the single source of truth for both the `GAMUT ERROR` indication and the new vectorscope offender markers.

- Existing PAL composite gamut thresholds and stability rules are preserved.
- When an outside-gamut run qualifies, all samples belonging to that confirmed offender run are retained for display.
- When the run first qualifies, the preceding seven outside samples are included as well.
- Subsequent outside samples remain marked until that run ends.
- Offenders are drawn as small red points at their actual vectorscope U/V positions.
- The normal vectorscope trace remains visible underneath.
- The existing `GAMUT ERROR` label remains unchanged.
- Selected-line zoom/windowing follows the same sample range as the gamut detector.
- All-lines mode resets run state at raster line boundaries as before.

The vectorscope marker is intentionally a chroma-position indication only: the gamut decision itself also depends on luminance, so a point visually near a nominal legal color target is not by itself sufficient to determine legality.

---

## Technical notes

### CatWuzle baseline

0.8.2 keeps the accepted 0.8.0 CatWuzle rendering model:

1. reconstructed luma samples define the waveform source geometry;
2. Catmull-Rom reconstruction produces the smooth trace geometry;
3. the trace is rasterized directly into the actual physical target plot resolution;
4. normal regions use analytic point-to-segment coverage with 2x2 subpixel sampling and round-join correction;
5. the small common resolve is applied before ScopePhor;
6. ScopePhor feeds back rasterized beam energy;
7. Beam Glow remains a separate local presentation effect.

The 2880 reconstructed samples per line are source geometry, not a fixed renderer output resolution.

### MUD rendering rule

The production MUD rule can be summarized as:

> Only bypass full AA after a sustained steep trace has convincing nearby trace on both sides. Once a monotone flank is accepted as dense, keep it in MUD mode until the derivative changes sign.

The key distinction is that MUD changes only the **pixel coverage method**:

```text
normal region:
Catmull geometry -> full CatWuzle analytic 2x2 AA

MUD region:
Catmull geometry -> direct no-AA raster
```

No source-domain filter is introduced in either path.

### MUD detector cost

Development profiling showed the final white-latch detector itself to be very small compared with waveform raster and resolve cost. This makes the detector suitable as a rendering-path selector; the expensive work remains the actual CatWuzle raster and post-raster resolve.

### Gamut offender semantics

The vectorscope overlay uses the same sustained offender decision as the existing gamut warning. It does not introduce a second looser visual-only test.

Because the decision includes luminance as well as chroma magnitude, two samples with similar U/V coordinates can differ in legality when their Y level differs.

---

## Development experiments intentionally not retained

Several 0.8.2 development experiments were useful for comparison but are not part of the production renderer:

- centripetal Catmull-Rom;
- heavy multi-substep AA;
- full-trace supersampling variants;
- adaptive width experiments;
- Y trace supersampling controls;
- cheap analytic AA inside MUD regions;
- red/white detector debug traces and POLYGRAPH bars.

The accepted production result remains the original CatWuzle character, with MUD used only as a selective dense-region AA bypass.

---

## Release baseline

OpenScope 0.8.2 should be treated as the safe baseline before 0.8.3 worker/parallelization work.

Preserve during future optimization:

- exact reconstructed waveform source data;
- accepted Catmull/CatWuzle geometry;
- full CatWuzle AA in non-MUD regions;
- direct no-AA rendering only in qualified MUD regions;
- transition stitching between both paths;
- the common resolve / ScopePhor / Beam Glow relationship;
- gamut warning and vectorscope offender overlay sharing one gamut decision.

Future CPU optimization should parallelize or restructure this work without changing the accepted waveform image.
