# Changelog

All notable changes to VizRack are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). VizRack is pre-1.0, so
a minor bump (`0.X.0`) carries new visualizers or features and a patch bump
(`0.0.X`) carries fixes only.

Every released section below is also published, unchanged, as a
[GitHub Release](https://github.com/yimsemin/VizRack/releases) with the portable
ZIP attached. How these notes are written and cut: `docs/RELEASE_NOTES_STYLE.md`.

## [Unreleased]

_Homage visualizers that name who inspired them, and a rack of 3D spectrum cascades._

### Added

- **Homage credits.** Built-in visualizers that take after a designer, artist or
  era now show an "Inspired by …" line when they start, and are collected on a
  new Help ▸ Credits screen and in the README. The names and works stay with
  their owners, outside VizRack's MIT license.
- **3D spectrum cascade.** Two new built-ins render the spectrum over time as a
  surface that recedes into the distance: _Classic Cascade_, a shaded
  Winamp-style spectrogram, and _Joy Division_ (Inspired by Joy Division), the
  same history drawn as sharp white ridges on black. Both use a quarter-view
  projection with edge-pinned motion and expose tuning knobs in the settings menu.

### Changed

- The classic cascade draws as a shaded surface rather than stacked lines, and
  the Joy Division ridges are sharper and no longer fade with depth.

### Fixed

- Campfire flames sit down in the fire pit instead of hovering just above it.

## [0.2.0] - 2026-08-28

_A campfire that breathes with the music, and a proper Help menu._

### Added

- **Campfire visualizer.** A calm fire whose embers lift on transients and whose
  flames lean with the stereo image — made for leaving in a corner of the screen.
- **Help menu.** An About dialog with the version number and links back to the
  project.

## [0.1.0] - 2026-07-22

_First public release._

### Added

- **Art visualizer** — six audio-reactive scenes and six palettes that follow the
  low, mid and high bands and the stereo movement of whatever is playing.
- **Oscilloscope** — left and right channel waveforms with roughly the last 15
  seconds of loudness.
- **Window controls for desk use** — always-on-top, borderless, adjustable
  opacity and a 15 / 30 / 60 FPS switch.
- **Optional external visualizers** — load TBProAudio mvMeter2 or Voxengo AnSpec
  (Windows x64 VST3) as the picture instead of a built-in. Their audio output is
  discarded; VizRack only ever monitors.

[Unreleased]: https://github.com/yimsemin/VizRack/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/yimsemin/VizRack/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/yimsemin/VizRack/releases/tag/v0.1.0
