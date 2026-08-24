# OpenScope 0.8.0 — Release Notes

OpenScope 0.8.0 substantially improves waveform rendering quality and consistency.

Highlights:

- New high-quality **CatWuzle** waveform renderer: Catmull-Rom trace reconstruction combined with Wu-inspired / analytic subpixel antialiasing (jokingly named after Catweazle's "electrickery").
- Major waveform CPU/render-time reduction: the old heavy QPainter/blur paths were replaced by the Q-less CatWuzle pipeline, active-area processing, direct chroma spans and accelerated ScopePhor feedback.
- Correct physical-pixel HiDPI presentation with no unintended Qt resampling.
- Graticule-derived plot geometry shared by PC and Spout waveform rendering.
- Improved thin-beam antialiasing, ScopePhor persistence and Beam Glow behavior.
- Better small-window rendering while preserving full-resolution detail.
- Spout/PAL waveform tuning for 576-line output, including ~75% peak trace white and reduced persistence/glow.
- Improved color-carrier intensity control.
- Fixed selected-line interaction stalls.
- Added X5/X10 waveform navigator.
- Fixed slider endpoint clipping and value-handle behavior.

This release establishes the current waveform rendering as the visual reference baseline for future optimization work.
