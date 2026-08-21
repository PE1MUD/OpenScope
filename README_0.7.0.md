# OpenScope 0.7.0 — Rolling README / Changelog

## Release changelog

OpenScope 0.7.0 improves live-input status handling and substantially develops the vectorscope as both an on-screen instrument and a native PAL/Spout output.

All main instrument views now share source-independent `NO VIDEO` handling. The vectorscope gains a more complete instrument presentation, with source and signal context on the PC view and a compact PAL-oriented presentation for Spout output. The PAL vectorscope is rendered natively for its output format and observes the configured broadcast safe area.

The vectorscope rendering architecture is also brought in line with the waveform renderer. Screen and video outputs now have dedicated renderers owned by `VideoEngine`; widgets consume completed images rather than owning output rendering. This keeps screen presentation, PAL output and resize behaviour independent while preserving a common instrument state.

## Functional changes

### Source-independent `NO VIDEO`

- Video, waveform and vectorscope can display a common centered `NO VIDEO` indication when the active live source is not delivering valid video.
- Signal validity is treated as source state rather than Blackmagic-specific UI state, allowing the same behaviour to be used by future live sources.
- The status presentation follows the existing OpenScope measurement-overlay style: dark grey panel, light border and white text.
- Spout output uses the corresponding video-output presentation rather than inheriting PC-view sizing.

### Vectorscope PC presentation

- The vectorscope PC view includes grouped instrument information alongside the scope.
- Displayed context includes source, input, video standard, selected line, target level, colour matrix and processing format.
- The scope and information area are laid out as one instrument presentation rather than independent overlays.
- PC presentation remains optimized for readability at normal desktop viewport sizes.

### PAL / Spout vectorscope presentation

- Vectorscope Spout output is enabled as a native video-output path.
- PAL output is rendered at the video-standard raster rather than being derived from a scaled PC vectorscope image.
- The PAL presentation observes the established broadcast safe area:
  - 80% horizontal content width, corresponding to 10% margin on each side;
  - 90% vertical content height, corresponding to 5% margin at top and bottom.
- PAL metadata is arranged around the circular vectorscope to make efficient use of limited SD raster space:
  - top left: source and input values;
  - top right: video standard and colour matrix;
  - bottom left: selected line;
  - bottom right: processing format.
- PAL metadata uses the same relative text scale as the waveform video-output presentation.
- PAL information cards are individually sized to their contents rather than forced to a common width.

## Rendering architecture

### Waveform-style vectorscope renderer model

OpenScope now uses the same high-level rendering contract for waveform and vectorscope:

`frame -> dedicated renderer -> complete QImage -> consumer`

`VideoEngine` owns separate vectorscope renderers for the two output domains:

- `vectorscopeScreenRenderer_` for the PC instrument view;
- `vectorscopeVideoRenderer_` for native video/Spout output.

This mirrors the existing waveform structure:

- `waveformScreenRenderer_`
- `waveformVideoRenderer_`

Each vectorscope renderer owns the geometry and composition for its final destination, including trace analysis, graticule and presentation overlays. Screen output is therefore rendered for the actual screen scope canvas, while PAL output is rendered directly for its own video-output geometry.

### Widget responsibility

`VectorscopeWidget` is now a presentation consumer rather than an instrument renderer. It receives the latest completed screen image and displays it in the same manner as the waveform/video presentation path.

This gives the vectorscope the same resize behaviour as the waveform: the last valid rendered image remains available while a renderer catches up with a new viewport size, rather than coupling display validity to an exact analyzer-buffer size at every intermediate resize event.

### Independent screen and video rendering

Screen and PAL/Spout vectorscope images are generated independently. One rendered trace is not resampled to create the other output.

This separation allows each destination to use its own:

- final raster size;
- scope geometry;
- safe-area rules;
- typography and metadata layout;
- graticule presentation.

The underlying selected line, persistence, glow, horizontal window and signal metadata remain common instrument state.

## Vectorscope presentation state

Vectorscope presentation information is maintained centrally in `VideoEngine` so that both the screen and video renderers consume the same state.

Current presentation state includes:

- source name;
- input type;
- video standard;
- selected line;
- target level;
- colour matrix;
- processing format.

Source changes such as Blackmagic capture or Philips Pattern ROM therefore update one canonical vectorscope state rather than maintaining separate widget and video-output copies.

## Current PAL presentation

For the current Blackmagic composite PAL path, the vectorscope presentation uses values equivalent to:

- source: `BMD IP 4K`
- input: `Composite`
- standard: `PAL 625i`
- matrix: `BT.601`
- processing: `YUV 4:2:2 10 bit`
- line: selected line number or `ALL`

The PC view retains the more descriptive grouped presentation, while the PAL/Spout view uses the compact value-oriented corner layout.
