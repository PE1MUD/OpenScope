# OpenScope 0.8.3

OpenScope 0.8.3 consolidates the current settings, waveform, vectorscope, branding and UI work into the 0.8.3 release baseline.

## Highlights

- Waveform vertical range extended to 1.3 V.
- Illegal luminance colorization added. The luma trace is rendered red below 298 mV and above 1.02 V.
- `Colorize Illegal Luminance` added under **Settings > Instrument**; enabled by default and restored by **Defaults**.
- `Colorize Gamut Errors` added under **Settings > Instrument**; enabled by default and restored by **Defaults**.
- Vectorscope gamut-error rendering now marks **all confirmed illegal samples** in red and also renders the connecting segments of each contiguous illegal run in red. Separate runs are not bridged.
- Waveform anti-aliasing can be enabled/disabled from **Settings > Instrument** and controls the actual waveform AA path.
- Waveform zoom navigator handle enlarged for easier operation.
- Display settings added for line-selector visibility and 90% / 80% safety guides. Safety guides use inverted-video rendering.
- Settings window geometry restoration improved.
- Calibration tab moved before Help and the input-level section renamed **Blackmagic Level Controls**.
- About tab added with OpenScope branding and BT.656 scope/limitations text.
- OpenScope application/taskbar icon embedded into the executable; the small icon uses the light-blue asymmetric vectorscope design.
- The About tab uses the separate full OpenScope hero logo with wordmark and green waveform; it no longer reuses the small taskbar/favicon artwork.

## Default instrument settings

- Anti Aliasing: On
- Vintage Look: existing default retained
- Colorize Illegal Luminance: On
- Colorize Gamut Errors: On

## Display settings

- Line Selector Visible: On
- Safety area 90%: Off
- Text safety area 80%: Off

## Branding / build

- Qt resources are compiled with CMake AUTORCC.
- The Windows application icon is embedded through the generated `.rc` resource.
- OpenScope version promoted to **0.8.3** in CMake.

## Notes

OpenScope operates on a BT.656 digital representation of analog video. This means some analog-domain measurements are outside its scope, including burst-phase checking, an 8fs indicator and direct VITS-line viewing.

### ScopePhor legality color fix
- Illegal luminance colourization is now applied when ScopePhor energy is composed to RGB.
- Retained phosphor outside the legal luminance range therefore remains red instead of leaving a white persistence trail.
- Thresholds remain `< 298 mV` and `> 1.02 V`.


### Quad-view polish
- About logo is now right-aligned and responsively scaled; it hides only when the controls viewport is too small.
- Screen vectorscope is centred in the usable area between the left information cards and the right edge while retaining the largest possible square size.


- Matrix layout follow-up: force a true equal 2x2 viewport grid.
- Branding follow-up: show the OpenScope logo top-right on non-About settings tabs when space permits; About keeps its large hero logo.

- Branding layout fix: the top-right OpenScope logo now gets a reserved right-hand layout strip, so it can never cover sliders or other controls; it disappears when the control viewport is too small.
