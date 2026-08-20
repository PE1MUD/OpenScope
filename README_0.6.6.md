# OpenScope 0.6.6 — Rolling README / Changelog

## Release changelog

OpenScope 0.6.6 expands the experimental Y Spectrum view into a more useful video-noise measurement tool. It now reports both unweighted and CCIR 567 / ITU-T J.61 weighted SNR, adds an 80% safety-area selection, and introduces continuous flat-field analysis that averages the power spectra of 575 usable PAL lines per frame and can additionally average across multiple frames. Blackmagic Intensity Pro 4K reference-noise curves are shown for normal and flat-field measurements to make the practical capture-card noise floor visible.

## Technical delta log

- Added **CCIR 567 / ITU-T J.61 weighted SNR** alongside the existing unweighted SNR measurement.
- Main SNR label now explicitly reads **`SNR xx.x dB Unweighted`**.
- Added measurement-box note: **`Blackmagic card SNR ~58 dB UNWTD`**, matching the typical ADV7180 flat-field CVBS SNR limit.
- Added **Safety area (80%)** spectrum selection: center 80% of the active line, excluding 10% at each side.
- Added **Flat Field** mode.
  - Runs continuously rather than taking a frozen snapshot.
  - Computes an FFT/power spectrum per usable PAL line.
  - Averages **575 lines per frame**; the final line is skipped because it contains the next sync transition in the captured active data.
  - Existing AVG 1/4/16/64 setting remains active and averages successive flat-field frame spectra.
  - Power spectra are averaged rather than source samples, so averaging reduces estimator variance without artificially improving the measured noise power.
- Added an empirical **BMD IP4K reference noise** trace across the full displayed spectrum.
- Added a separate **BMD IP4K flat-field reference noise** trace that automatically replaces the normal reference in Flat Field mode.
- Smoothed the small ~350 kHz board-related spur out of the flat-field reference curve so the reference represents the underlying broadband floor.
- Corrected the Blackmagic reference-point array length to avoid the spurious diagonal line caused by an implicit zero-initialized final point.
- Enlarged/adapted the SNR measurement box so `Unweighted` is fully visible.
- Removed the detached static help footer below the spectrum; the recovered space is returned to the plot.
- Brightened the Y-spectrum axis labels and the left/bottom plot axes for better readability.
- Added application-wide **F** shortcut handling via the Qt application event path, so pressing F also works while focus is inside separate instrument/tool windows; Escape still closes the spectrum window.
- Changed the default waveform **Vintage look** setting to **off** for newly created/default settings.

## Measurement notes

- The Intensity Pro 4K used for capture contains an ADV7180 analog video decoder. Its datasheet specifies about **58 dB typical unweighted SNR for a luma flat field**, which closely matches the best OpenScope measurements from the cleaner tested card (~57–58 dB unweighted).
- A second Intensity Pro 4K sample showed a noticeably higher low-frequency spur around 330–350 kHz and a lower practical SNR. The spur amplitude also changed with card temperature. This is therefore treated as capture-hardware behaviour rather than source noise.
- Weighted SNR normally reads several dB higher than unweighted SNR because the CCIR/J.61 weighting de-emphasizes higher-frequency noise.
- The SNR integration bandwidth remains **0.10–5.00 MHz** even though the displayed spectrum and Blackmagic reference trace extend to the PAL capture Nyquist limit (~6.75 MHz).
