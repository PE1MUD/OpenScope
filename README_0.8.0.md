# OpenScope 0.8.0

OpenScope 0.8.0 is a major waveform-rendering and presentation release. The main focus of this version is visual fidelity, deterministic geometry, HiDPI correctness, stable line interaction, and a much more consistent relationship between the on-screen waveform and the 576-line Spout/video output.

The waveform renderer now uses the graticule-defined physical plot area as its geometric authority. This removes hidden scaling mismatches and makes the renderer behave consistently across large HiDPI windows, small viewports, and fixed PAL/Spout targets.

---

## Release notes

### Waveform rendering

- Reworked the single-line waveform core into the Q-less **CatWuzle** renderer: a hybrid of Catmull-Rom trace reconstruction and Wu-inspired / analytic subpixel antialiasing.
- Improved subpixel antialiasing and round-join coverage while keeping a thin, scope-like beam.
- Added a light pre-ScopePhor resolve to reduce pearling without smearing the trace.
- Tuned beam width so fullscreen/high-resolution rendering stays fine while small views remain readable.
- Restored trace body after mini-view thinning so small windows no longer look starved.
- Kept the signal trace visually in front of the graticule.


### Performance / CPU efficiency

0.8.0 also contains a substantial waveform-rendering performance pass. The visual quality work was deliberately kept at physical-pixel resolution, but the renderer itself was restructured to remove several expensive paths and unnecessary full-frame operations.

- Replaced the earlier heavy QPainter/blur-oriented waveform path with the Q-less CatWuzle rasterizer.
- Reduced worst-case waveform rendering from the old hundreds-of-milliseconds class to practical tens-of-milliseconds operation on the current high-resolution path.
- Limited ScopePhor and glow work to the active beam bounding box instead of repeatedly processing the full image where possible.
- Kept ScopePhor in raw 16-bit energy space and accelerated its feedback loop with AVX2.
- Moved chroma rendering to direct spans rather than expensive intermediate image compositing.
- Avoided re-rendering historical beam geometry for persistence; ScopePhor now feeds back the already-rasterized energy surface.
- Removed selected-line display resets and synchronous settings writes from the interactive line-change path.
- Preserved high-resolution physical-pixel rendering rather than trading image quality for a low-resolution shortcut.
- Kept Spout/video waveform rendering demand-driven so disabled output does not perform unnecessary render work.

Representative development measurements varied with viewport size and signal complexity, but the key architectural change is that the renderer now operates in the tens-of-milliseconds range where earlier experimental QPainter/blur implementations could exceed 600 ms and in some cases approach or exceed one second. The remaining CPU work after 0.8.0 is therefore optimization of an already-correct high-quality renderer, not another visual-quality compromise.

### HiDPI and physical-pixel presentation

- Fixed the waveform HiDPI presentation path.
- High-resolution waveform images are now tagged with the correct device-pixel ratio before Qt presentation.
- Removed unintended Qt resampling between the physical waveform raster and the logical widget rectangle.
- Eliminated the stationary one-pixel presentation artifact / “black hole” caused by the old scaling path.
- Preserved native physical-pixel rendering quality on 150%/HiDPI displays.

### Graticule-driven plot geometry

- The waveform graticule/layout now defines the exact physical plot rectangle used by the renderer.
- Beam/core/glow scaling is based on the actual plot area rather than the outer canvas size.
- PC and Spout/video rendering now follow the same geometry model.
- This substantially improves consistency across viewport sizes and output targets.

### ScopePhor and Beam Glow

- Slowed ScopePhor decay for a more convincing phosphor persistence effect.
- Preserved ScopePhor without reintroducing visible jaggies.
- Reworked waveform Beam Glow control scaling so the useful range occupies the full UI slider.
- Waveform Beam Glow UI remains 0..100, internally mapped to an effective 0..20 range.
- Default Beam Glow is centered at UI 50, corresponding to effective glow 10.

### Color carrier rendering

- Improved the usable Color Carrier Intensity range.
- UI value 0 disables the color carrier trace completely.
- UI values 1..100 map onto the previously useful effective range 20..100.
- Default Color Carrier Intensity is UI 50.
- Avoids the old near-black, visually useless lower range before the color trace disappeared.

### Spout / PAL waveform output

- Kept Spout/video waveform rendering on the same `WaveformRenderer` implementation as the PC waveform.
- Uses its own fixed 576-line target and target-specific tuning rather than a separate rendering algorithm.
- Spout waveform trace peak white is limited to approximately 75% to reduce the overfilled “paint roller” look on PAL/CRT output.
- Spout ScopePhor and Beam Glow are internally scaled to 60% of the UI setting:
  - UI 50 -> Spout effective 30
  - UI 100 -> Spout effective 60
- Core intensity, color-carrier behavior, and general beam geometry remain shared with the main renderer.
- PAL/Spout graticule remains independent from trace brightness limiting.

### Waveform zoom and navigation

- Added a bottom navigator for X5/X10 waveform modes.
- Navigator shows the full line and current visible window.
- Dragging the navigator pans the waveform; clicking jumps the visible window.
- Existing keyboard and wheel navigation remains available.
- Added `LINE / X5 / X10` status information at the top-right of the waveform.

### Selected-line interaction

- Fixed video/display stalls while changing the selected line.
- Selected-line changes no longer reset the full display presentation path.
- INI persistence of line selection is debounced instead of being written synchronously for every step.
- Held-arrow stepping and mouse-wheel line stepping remain responsive.
- Line changes still correctly clear line-specific phosphor/history state.

### Controls and UI

- Normalized Core Intensity to a 0..100 UI range while preserving the established internal brightness scale.
- Core Intensity UI 100 corresponds to the previous effective internal level 200.
- Fixed custom slider endpoint clipping.
- Slider value handles now remain fully visible and usable at both 0 and 100.
- Mouse mapping uses the same safe travel range as the painted slider handle.

### Output and integration

- Spout enable/disable remains structural: disabled senders cannot be reopened by stale submissions.
- Video, waveform, and vectorscope Spout paths remain separately gated.
- High-resolution PNG export remains video-only and is not generated from waveform phosphor/glow surfaces.

---

## Technical notes

### Waveform geometry

The renderer no longer treats the outer canvas as the authoritative beam geometry. The final plot rectangle is derived from the waveform graticule/layout, and that physical pixel rectangle drives:

- horizontal sample-to-pixel mapping;
- vertical volts-to-pixels mapping;
- beam/core scaling;
- glow scaling;
- ScopePhor raster state.

This makes the renderer behave consistently when the same waveform is shown in a small viewport, a large HiDPI viewport, or a fixed PAL/Spout output.

### HiDPI presentation

On HiDPI systems, the waveform renderer produces a physical-pixel image. The image is presented with the matching device-pixel ratio so Qt maps physical image pixels to physical screen pixels rather than performing a second image resample.

Example at 150% scaling:

```text
logical destination: 1779 x 1334
physical render:      2669 x 2001
DPR:                  1.5
```

The physical render resolution is intentional and should not be replaced by a low-resolution logical-pixel render as a performance shortcut.

### CatWuzle core

**CatWuzle** is OpenScope's custom Q-less waveform rasterizer. The name is a deliberately silly contraction of **Catmull-Rom** and **Wu-style** antialiasing, with a nod to Catweazle's "electrickery". It is not an external library or a literal implementation of Xiaolin Wu's line algorithm; it combines those ideas into an OpenScope-owned target-resolution renderer.

The signal path is split conceptually into two jobs:

1. **Catmull-Rom reconstruction** supplies the smooth waveform geometry between reconstructed luma samples.
2. **Wu-inspired / analytic coverage** maps that geometry onto the target pixel grid with subpixel-aware antialiasing rather than relying on QPainter for the signal trace.

The current quality path uses:

- Catmull-Rom midpoint interpolation between reconstructed samples;
- analytic point-to-segment coverage;
- 2x2 subpixel area coverage;
- round-join correction at interior vertices;
- target-aware beam scaling;
- a small separable pre-ScopePhor resolve;
- separate local Beam Glow;
- raw 16-bit ScopePhor feedback before final display conversion.

Chroma remains current-frame only and is not accumulated into ScopePhor history.

### ScopePhor

ScopePhor uses a one-buffer feedback model in raw beam-energy space. The current decay curve is intentionally slower than the earlier 0.8.0 development versions so persistence remains visible without requiring excessive slider values.

### Target-specific Spout tuning

The Spout/video renderer is a second instance of the same `WaveformRenderer`, not a separate implementation. Target-specific tuning is applied only where the smaller PAL raster benefits from it:

```text
ScopePhor = UI value x 0.60
Beam Glow = UI value x 0.60
trace peak white ~= 75%
```

The application renderer continues to use the UI values directly.

---

## Known follow-up items

These items are intentionally not part of the 0.8.0 release scope and can be addressed after the release baseline is frozen:

- Add a stable 25-frame averaged waveform hover measurement that reads the actual trace Y value at mouse X.
- Further optimize waveform CPU cost without reducing physical render resolution or visual fidelity.
- Further optimize vectorscope CPU usage.
- Investigate/finish automatic SNR presentation on suitably flat lines.
- Restore/improve waveform All Lines mode where needed.
- Fix remaining vectorscope card visibility/resize polish.
- Keep future CRT personality / overdrive effects for a later major release.

---

## Release baseline

OpenScope 0.8.0 should be treated as the visual baseline for the waveform renderer:

- preserve physical-pixel HiDPI rendering;
- preserve graticule-derived plot geometry;
- preserve the accepted CatWuzle beam character;
- preserve the current ScopePhor/Glow relationship;
- preserve the target-specific Spout tuning.

Future performance work should optimize this output rather than reduce its render resolution or silently reintroduce post-scaling.
