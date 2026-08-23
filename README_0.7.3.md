# OpenScope 0.7.3 — Incremental Release Notes / Changelog

## Release changelog

OpenScope 0.7.3 is a focused usability and presentation update on top of the 0.7.2 measurement,
calibration and renderer work.

The main application now has a clean application-wide fullscreen mode. `F11` toggles OpenScope into
a borderless fullscreen presentation while preserving the currently active matrix or maximized
instrument view. `Esc` exits this fullscreen mode as well. The implementation is integrated into the
existing global keyboard event handling rather than adding a second shortcut mechanism.

The PC vectorscope presentation has also been refined. Its three grey information cards are no longer
grouped as a compact stack at the top-left of the instrument. They are distributed vertically over
the available height: source/input/standard at the top, line/targets/matrix in the middle, and
processing information at the bottom. The native PAL/Spout vectorscope layout is unchanged.

---

# 0.7.3 detailed changes

## Application fullscreen

- Added application-wide `F11` fullscreen toggle.
- Fullscreen uses the complete current monitor area.
- Normal Windows title bar and window frame are removed while fullscreen.
- The OpenScope menu presentation is hidden while fullscreen.
- The active workspace state is preserved:
  - matrix view remains matrix view;
  - video fullscreen remains video;
  - waveform fullscreen remains waveform;
  - vectorscope fullscreen remains vectorscope.
- Existing workspace double-click/maximized-view behaviour remains independent of application
  fullscreen.
- Pressing `F11` again exits application fullscreen.
- Pressing `Esc` while application fullscreen is active also exits fullscreen.
- `Esc` outside F11 fullscreen retains its previous behaviour.
- F11 handling is integrated into the existing application-wide `eventFilter()` keyboard path.
- Key auto-repeat is ignored for the fullscreen toggle.
- Previous window geometry/state is restored when leaving fullscreen.

## Vectorscope PC presentation

- Reworked the left-side PC vectorscope information-card layout.
- The three cards are vertically distributed instead of grouped at the top:
  - top: `SOURCE / INPUT / STANDARD`;
  - middle: `LINE / TARGETS / MATRIX`;
  - bottom: `PROCESSING`.
- Left/right alignment and card width remain consistent.
- The surrounding dark information panel follows the full available vertical card area.
- Card contents, typography, colours and signal metadata are unchanged.
- The native PAL/Spout vectorscope presentation is unchanged.
- No vectorscope signal-processing, target, gamut or trace-rendering behaviour is changed by this
  layout update.

## Scope of 0.7.3

0.7.3 deliberately remains a small delta over 0.7.2. It does not introduce a new processing stage or
change the established screen/video renderer architecture. The release concentrates on making the
existing instrument easier to use on a dedicated display and making better use of the available
vectorscope screen area.

---

# Previous release history

# OpenScope 0.7.2 — Incremental Release Notes / Changelog

## Release changelog

OpenScope 0.7.2 develops the application from the 0.7.x renderer architecture into a more complete
measurement instrument. The release concentrates on vectorscope presentation and legality checks,
calibration support, noise reduction, performance visibility, source handling and independent
screen/video output paths.

The vectorscope now has a substantially more complete broadcast-instrument presentation. Its
graticule includes calibrated 75% and 100% colour references, refined target geometry, I/Q context,
chroma magnitude indication and a PAL composite gamut warning. The same instrument state is
rendered independently for the PC viewport and native PAL/Spout output, so neither presentation has
to be rescaled from the other.

The calibration path now exposes Blackmagic composite luma and chroma gain where supported and adds
an OpenScope-side high-frequency luma response correction. Noise reduction is integrated into the
video processing path and can be controlled without changing the instrument architecture.

Performance work in 0.7.2 makes the rendering pipeline easier to understand while running. Screen
and video render paths are measured separately, expensive stages have detailed timing information,
and fullscreen/demand-driven rendering avoids spending CPU time on invisible PC consumers while
keeping enabled external outputs alive.

Together these changes make 0.7.2 less of a scope demonstration and more of a practical PAL
measurement and calibration environment.

---

# 0.7.2 detailed changes

## Vectorscope

### Graticule and colour references

- Refined BT.601 / PAL vectorscope target geometry.
- 75% and 100% target positions are presented simultaneously as calibrated references.
- Reworked 75% / 100% reference rings and their scaling so the graticule follows the same chroma
  geometry as the actual trace.
- Improved target labels, centering, marker sizes and target corner/tolerance presentation.
- Refined outer-circle rendering and major/minor perimeter ticks.
- Added I/Q-axis context and tuned label size/presentation so it remains subordinate to the main
  U/V vectorscope axes.
- Added a chroma-magnitude reference/meter with calibrated percentage markers.
- Added explicit 75% and 100% visual references to the chroma magnitude presentation.
- Refined target/meter alignment around the magenta, yellow, cyan and other colour directions.
- Trace remains visually dominant over the graticule rather than being obscured by presentation
  graphics.

### PAL composite gamut indication

- Added PAL composite gamut checking to the vectorscope.
- The check derives the composite excursion from BT.601 YCbCr data and PAL U/V weighting.
- Nominal 100% PAL bars remain valid reference signals; transient decoder/filter ringing is excluded
  from the final gamut decision by requiring a locally settled signal region.
- A `GAMUT ERROR` indicator is shown when a sustained settled signal exceeds the PAL composite
  envelope.
- PC and video/Spout presentations use independent placement.
- In the PAL/Spout presentation the gamut indication is positioned directly above the processing
  status card so it remains visible without colliding with metadata.

### Presentation

- Continued refinement of the PC vectorscope information layout.
- Compact PAL/Spout metadata presentation retained for native 720×576 output.
- Source, input, standard, matrix, line/zoom state and processing format remain available as
  instrument context.
- The PC and video vectorscope renderers consume the same canonical instrument state but compose
  independently for their destination geometry.

---

## Waveform

- Continued refinement of single-line and all-lines waveform rendering.
- X1/X5/X10 presentation and selected-line status are carried through to native waveform output.
- Waveform video output retains its compact status badge.
- Colour/chroma waveform presentation and trace/glow behaviour were refined as part of the 0.7.x
  renderer work.
- Flat-field, spectrum and SNR workflows remain integrated with the waveform/video analysis path.
- High-frequency luma correction is applied before measurement/display where enabled so the
  correction is part of the signal path rather than a cosmetic display transform.

---

## Calibration

### Blackmagic composite input gain

- Added control of Blackmagic composite luma gain where the active DeckLink device exposes the
  corresponding hardware setting.
- Added control of Blackmagic composite chroma gain where supported.
- Controls are presented as calibration functions rather than general picture controls.
- Unsupported hardware can leave the corresponding control unavailable instead of pretending the
  feature exists.
- The calibration controls make it possible to distinguish source-level errors from capture-path
  gain errors while using OpenScope as the reference instrument.

### Luma high-frequency response correction

- Added `LumaHighFrequencyCompensator`.
- Added adjustable high-frequency luma compensation referenced at 5.8 MHz.
- The correction is designed around a smooth frequency response rather than a single arbitrary
  pixel-domain gain.
- Black level remains fixed while the upper luma range is corrected.
- Edge regions where the reconstruction/correction kernel is not fully available are not forced
  through an invalid partial correction.
- The compensation can be enabled/disabled from the calibration controls.
- Calibration state is persisted in OpenScope settings.

---

## Noise reduction

- Noise reduction is integrated into the processing pipeline.
- Added user control for enabling the filter and setting its intensity.
- AVX2 processing is used where applicable.
- Filtering can be disabled for measurements where source noise itself is the quantity of interest.
- The processing path is shared by the instrument consumers rather than implemented separately in
  each widget.

---

## Performance and renderer diagnostics

- Expanded timing visibility for waveform and vectorscope screen/video renderers.
- Detailed renderer stages are available through hover information instead of continuously adding
  more permanent performance bars.
- Timing categories include the expensive preparation, trace, persistence/clear, compose, glow,
  graticule/overlay and residual work stages as applicable.
- Screen and video render paths are measured independently.
- Performance statistics include the vectorscope glow workload/tile information used to understand
  sparse glow cost.
- Fullscreen operation is demand-driven: invisible PC consumers do not need to keep rendering merely
  because another output path exists.
- Enabled Spout/video outputs continue independently of PC viewport visibility.
- HiDPI rendering continues to use the physical screen resolution implied by the Qt device pixel
  ratio so scopes remain sharp on high-resolution displays.

---

## Spout / external output

- Separate Spout streams are supported for Video, Waveform and Vectorscope.
- Waveform and vectorscope Spout images are native instrument renders rather than captures of a Qt
  widget.
- PAL output remains native 720×576.
- Screen and video renderers retain independent geometry, overlay and safe-area decisions.
- Vectorscope metadata remains arranged around the scope for SD raster efficiency.
- Waveform output carries selected-line and zoom context.
- Vectorscope output carries source, standard/matrix, line/zoom and processing information.
- Gamut status participates in the video renderer directly rather than being overlaid by the PC
  widget.

---

## Source and capture handling

- Blackmagic DeckLink remains the primary live capture path.
- Philips Pattern ROM is available as an alternative source through the shared source pipeline.
- Source-specific decode/capture details remain below the common `Yuv444Frame` processing boundary.
- Instrument metadata follows the active source instead of assuming every frame is Blackmagic input.
- `NO VIDEO` state remains source-independent for live sources.
- DeckLink capability probing was extended alongside calibration-control support.

---

## Settings and controls

- New calibration and processing controls are persisted through the existing OpenScope settings
  service.
- Instrument presentation state remains centralized rather than duplicated between widgets and
  output renderers.
- Control-panel organization continues to separate display, calibration, instrument and miscellaneous
  functions.
- Local UI preferences remain separate from signal/source data such as Philips ROM INI files.

---

## Internal architecture relevant to 0.7.2

The common internal video representation remains planar 16-bit `Yuv444Frame` with neutral chroma at
32768. Source-specific v210, UYVY and ROM formats are converted below this boundary so analysis,
processing and rendering operate on the same representation.

The high-level rendering contract remains:

`frame -> processing/analysis -> dedicated renderer -> complete QImage -> consumer`

`VideoEngine` coordinates independent PC and video-output renderers. Widgets consume finished screen
images. Spout consumes the dedicated video-output images. This allows screen resizing, fullscreen
operation and PAL raster output to evolve independently.

---

# Historical release trail

## 0.7.1 — consolidation and operational instrument baseline

0.7.1 served as the working baseline after the 0.7.0 renderer architecture change. By this point the
application had matured into a multi-source PAL measurement environment with the following
capabilities available together:

- Matrix and fullscreen instrument operation.
- Video point-and-measure / selected-line workflow.
- Waveform X1, X5 and X10 views.
- All-lines waveform density view.
- Colour waveform presentation.
- Scope-phosphor style persistence/glow.
- Hover voltage/percentage probe.
- Area/reference measurement workflow on reconstructed luma.
- Frequency and amplitude/dB measurement support.
- Y spectrum, SNR and flat-field analysis.
- Noise-reduction controls.
- Separate Video, Waveform and Vectorscope Spout streams.
- Native PAL 720×576 instrument output.
- PC and PAL vectorscope presentations.
- Philips Pattern ROM as an alternative source.
- Persistent application/settings state.
- High-resolution PNG export.
- Performance/fullscreen render gating and HiDPI-aware rendering.

The 0.7.1 codebase was also the point at which source diversity and the growing analysis feature set
made the existing architectural boundaries explicit: capture/conversion, common `Yuv444Frame`,
processing, analysis, rendering, output and widgets.

---

## 0.7.0 — renderer/output architecture milestone

OpenScope 0.7.0 improved live-input status handling and substantially developed the vectorscope as
both an on-screen instrument and a native PAL/Spout output.

### Source-independent `NO VIDEO`

- Video, waveform and vectorscope gained a common centered `NO VIDEO` indication for live sources
  without valid video.
- Signal validity became source state rather than Blackmagic-specific UI state.
- The presentation uses the existing dark measurement-overlay style.
- Spout/video output uses its own native presentation instead of inheriting PC-view sizing.

### Vectorscope PC presentation

- The PC vectorscope gained grouped instrument information alongside the scope.
- Displayed context includes source, input, video standard, selected line, target level, colour matrix
  and processing format.
- Scope and information area became one coherent instrument presentation.

### PAL / Spout vectorscope presentation

- Vectorscope Spout output became a native video-output path.
- PAL vectorscope output is rendered directly at the video-standard raster instead of being scaled
  from a PC image.
- The PAL presentation follows the configured broadcast safe area:
  - 80% horizontal content width / 10% margin each side;
  - 90% vertical content height / 5% top and bottom margin.
- PAL metadata is distributed around the circle:
  - top left: source/input;
  - top right: video standard/matrix;
  - bottom left: selected line;
  - bottom right: processing format.
- Metadata cards size to their own content.

### Rendering architecture

0.7.0 established the common waveform/vectorscope rendering model:

`frame -> dedicated renderer -> complete QImage -> consumer`

`VideoEngine` owns independent renderers for screen and video-output domains:

- `waveformScreenRenderer_`
- `waveformVideoRenderer_`
- `vectorscopeScreenRenderer_`
- `vectorscopeVideoRenderer_`

The widget is a presentation consumer. It does not own the final instrument rendering pipeline.

This separation allows screen and PAL/Spout outputs to use independent:

- raster sizes;
- scope geometry;
- safe areas;
- typography;
- metadata layout;
- graticules.

Selected line, persistence/glow, horizontal window and signal metadata remain shared instrument state.

---

## 0.6.x — measurement and processing foundation

The 0.6.x line established much of the measurement functionality that later 0.7.x versions reorganized
and presented more cleanly.

### Waveform reconstruction and measurement

- Fixed high-resolution luma reconstruction became a common source for waveform/display measurement.
- Windowed-sinc luma reconstruction supported bandwidth-sensitive waveform inspection.
- Single-line waveform persistence and all-lines density rendering were separated into distinct
  presentation modes.
- X10 waveform zoom/pan was developed for detailed inspection.
- Hover probing reports voltage and percentage level.
- Area measurements provide signal amplitude/frequency analysis.
- Reference measurements allow relative dB measurements.
- Measurement lines and labels can be refined interactively.
- Multiburst-oriented measurement work established the basis for automated frequency-zone analysis.

### Vectorscope development

- The vectorscope moved from interpolated spline traces toward density-aware straight-segment
  rendering.
- Long vectors contribute less density than slowly moving local signal regions, improving the
  analogue-looking trace.
- Gamma/density shaping exposes infrequent events instead of turning all-lines views into a solid
  mass.
- Rec.601 625/525 and Rec.709 target calculations were introduced.
- 75%/100% target context and tolerance presentation developed incrementally.
- Vectorscope work moved to an independent worker path so a slow scope does not have to stall video
  display.

### Performance and threading

- Luma reconstruction was parallelized over worker threads.
- Display, waveform and vectorscope timing became explicit enough to identify expensive reconstruction,
  compose and render stages.
- The processing model increasingly adopted latest-frame-wins behaviour for independent instruments,
  appropriate for a real-time measurement application.

### Spectrum / SNR

- Y spectrum and SNR analysis were added and iterated.
- Weighted SNR was added in the 0.6.6 development line.
- Flat-field analysis supports identification of fixed-pattern and periodic interference.
- Frequency-domain diagnostics became a separate measurement workflow rather than part of the normal
  waveform overlay.

### Output and presentation groundwork

- Waveform/video PAL output geometry and underscan/safe-area handling were established.
- The decision was made to keep separate screen-native and PAL-native rendering paths rather than
  resize one into the other.
- Spout became the preferred external real-time output mechanism for OBS and other consumers.

---

# Current 0.7.2 instrument summary

OpenScope 0.7.2 currently combines:

- Blackmagic DeckLink live PAL capture.
- Philips Pattern ROM source support.
- 16-bit planar YUV444 common processing.
- High-resolution reconstructed luma.
- Video display with selected-line interaction.
- Waveform monitor:
  - X1 / X5 / X10;
  - all-lines density;
  - colour/chroma presentation;
  - persistence/glow;
  - voltage/% hover;
  - area/reference/frequency/dB measurements.
- Vectorscope:
  - PC and native PAL presentations;
  - BT.601 target geometry;
  - 75% / 100% references;
  - I/Q context;
  - chroma magnitude reference;
  - PAL gamut warning;
  - glow/persistence presentation.
- Y spectrum / SNR / flat-field analysis.
- Noise reduction.
- Composite luma/chroma capture gain calibration.
- Adjustable high-frequency luma compensation.
- Separate Video / Waveform / Vectorscope Spout outputs.
- Native PAL 720×576 instrument outputs.
- Demand-driven screen rendering.
- Detailed performance instrumentation.
- Persistent control/settings state.

---

# Notes for the next version

This file is the accumulated 0.7.2 release history. At the next version bump it should be frozen as the
permanent 0.7.2 README/changelog, after which a new rolling README should start for the next version.
