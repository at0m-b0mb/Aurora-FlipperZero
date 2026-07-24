# Changelog

All notable changes to Aurora are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1] — 2026-07-24

### Added

- **Measurement marker.** Hold **↑** to drop a marker at the cursor; the status strip then reads the
  live **Δ frequency and Δ dB** between the cursor and the marker, so channel spacing, harmonic
  offsets and level differences read straight off the screen. Hold **↑** again on the marker to
  clear it. The marker is stored as a frequency, so it survives zoom and pan, and is drawn as a
  tab-topped dashed guide distinct from the cursor. A band change clears it.
- **Persistent settings.** Band, detail, range, peak-trace mode, sound, LED and the view you leave
  on are saved to `/ext/apps_data/aurora/aurora.conf` (FlipperFormat) and restored on launch. The
  file is human-readable, and every field is range-clamped on load, so a hand-edited or
  version-skewed file can never push a setting out of bounds.

### Changed

- The transient control legend gained a third line for the marker gesture.
- `helpers/aur_scale.c` now also formats the signed marker delta (`aur_fmt_delta_hz`), covered by the
  host tests — **3330 checks** total.

## [1.0] — 2026-07-18

First release.

### Added

- **Sweeping receiver.** Walks the internal CC1101 across a Sub-GHz band in 64 steps — two pixels
  per bin across the screen — reading RSSI at each stop. Roughly 14 sweeps per second at Normal
  detail. Listen-only; never transmits.
- **Three views**, cycled with OK:
  - **Split** — live spectrum over a scrolling waterfall.
  - **Waterfall** — full height, with one-second ticks derived from the *measured* sweep rate.
  - **Spectrum** — full height, plus an activity map of every bin that has been busy since you
    tuned there.
- **Ordered Bayer dither** for the waterfall, so signal strength reads as brightness on a one-bit
  screen instead of a binary smear.
- **Self-calibrating noise floor**, taken as the lower quartile of each sweep's own 64 readings, so
  the display adapts to the local RF environment and a few loud carriers cannot drag it.
- **Cursor** with live frequency (MHz) and level (dBm) in the header. Hold a direction key to
  scrub; push past an edge and the window pans.
- **Cursor-centred zoom**, halving the span per step from the whole band down to 2 MHz, so a peak
  can be walked down without being lost.
- **Snap to peak** on OK-hold; **freeze** the display on Back-hold.
- **Peak trace** with Off / Decay / Hold modes, and a **round-frequency gridline** axis that picks
  a 1-2-5 step to suit the span.
- **Settings** for band, dwell (Fast / Normal / Fine), display range (30 / 50 / 70 dB), peak-trace
  mode, chirp-on-activity and LED.
- **Controls** and **About** pages on the device, including the honest limits.
- Host-tested scale engine (`helpers/aur_scale.c`) — 3319 checks over bin mapping, zoom clamping,
  the noise-floor percentile, pixel scaling and the dither ramp. Run with `make -C test`.
- CI building against both the **release** and **dev** SDK channels.

### Notes

- Zoom is capped at a 2 MHz span. The CC1101's receive filter is 270–650 kHz wide, so finer bins
  would imply a resolution the radio does not have.
- Because it sweeps rather than watches, a burst shorter than one sweep can be missed. For
  continuous single-frequency alerting, see
  [Cerberus](https://github.com/at0m-b0mb/flipper-cerberus).
