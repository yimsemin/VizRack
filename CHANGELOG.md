# Changelog

All notable changes to VizRack are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). VizRack is pre-1.0, so
a minor bump (`0.X.0`) carries new visualizers or features and a patch bump
(`0.0.X`) carries fixes only.

Every released section below is also published, unchanged, as a
[GitHub Release](https://github.com/yimsemin/VizRack/releases) with the portable
ZIP attached. How these notes are written and cut: `docs/RELEASE_NOTES_STYLE.md`.

## [Unreleased]

## [0.5.0] - 2026-10-21

_Rhythm Ripple, a rain-on-water visualizer that lands on the beat, and a freely rotating 3D cascade._

### Added

- **Rhythm Ripple.** Rain falling on a still, dark puddle in time with the
  music. Kicks, snares and hi-hats each drop a ripple within a few
  milliseconds of the hit — large for a kick, small for a hi-hat. Left-click
  to drop one yourself. The right-click menu has Sensitivity and Sync delay
  (0–300 ms), which holds ripples back if your speakers or Bluetooth device
  play the sound late.
- **Classic Cascade rotates freely.** Drag with the left mouse button to turn
  the surface in any direction; the mouse wheel changes the perspective. The
  right-click menu has ready-made angles (Default view, Front, Side, Back,
  Looking up), and the view is saved.
- **View ▸ Show overlay text** hides every built-in visualizer's title,
  tagline and hint at once — handy for recording or streaming.
- **Properties...** in a VST3 entry's submenu shows its version, edition and
  module path.
- **Oscilloscope 120 FPS** joins 15, 30 and 60.
- **Star Guitar silence marker.** A full second of silence sends one oversized
  building through the near layer to mark a track change.

### Changed

- **Simpler menus.** The menu bar is File · View · Plug-in · Help. A built-in
  visualizer is chosen with one click on its name, and the Plug-in label no
  longer shows live status text.
- **Settings live in the right-click menu.** A left click no longer changes a
  setting: Art Visualizer's scene and the cascade's palette no longer cycle
  on click. Art Visualizer's keyboard shortcuts still work.
- **Every built-in overlay is in Korean**, with one shared font (Segoe UI,
  slightly larger). The "Built-in" prefix is gone from names, "3D Spectrum" is
  now "Classic Cascade", and a few Korean phrases are reworded.
- **No dialog plays a sound.** Error boxes no longer trigger the Windows alert
  beep while you listen.
- **Star Guitar's sky bands** moved to 6–10 kHz and 10–16 kHz, so dense
  hi-hat passages no longer spawn in clumps. The upper band's object is now a
  diagonal star instead of a UFO, pine and tree have more distinct colors,
  and each building gets its own share of lit windows.

### Fixed

- **A hidden window border can be restored** by double-clicking the top
  strip, which now also moves the window on the built-in visualizers.
- **View menu check marks are always current** when the menu opens.
- **Campfire:** the glowing coals sit at the base of the fire, and stars no
  longer glow below the fire line.
- **Prism Tunnel's outermost ring** fades out instead of popping.
- **Star Guitar's near layer** falls back to a pine tree instead of a plain
  building.
- **Selecting a fixed-size plug-in (e.g. mvMeter2) while maximized** no longer
  breaks the window.

## [0.4.0] - 2026-09-15

_A rhythmic pixel-art landscape that reacts to the music's frequency
spectrum, and a handful of polish fixes._

### Added

- **Star Guitar.** A new built-in visualizer (Inspired by The Chemical
  Brothers / Michel Gondry's "Star Guitar"): a horizontally-scrolling,
  blocky pixel-art landscape of streetlights, pine trees, trees, telephone
  poles, building silhouettes, a starry sky and the occasional passing UFO,
  in four parallax depth layers that scroll at different speeds. Audio
  splits into eight frequency sub-bands, and each spawns its own object
  into its own depth layer — under 300Hz (streetlight/pine tree) into the
  near layer, 300Hz-4kHz (tree/pole) into the mid layer, 4-8kHz (building
  silhouettes) into the far layer, and above 8kHz (star/UFO) into the sky —
  whenever that sub-band rises sharply *relative to its own recent level*.
  A harder hit spawns a visibly bigger object, and every spawn grows up
  from ground level the same way, so the landscape reads as the music's
  rhythm rather than scenery that happens to move. Right-click exposes four
  **sensitivity** settings (Low/Mid/Treble/Air) to tune how easily each
  frequency group reacts.

### Changed

- **Shorter plug-in menu names.** The redundant "Built-in" / "내장" prefix is
  gone from the visualizer list, and the "Inspired by …" homage credit moved
  off the menu bar onto the visualizer's own on-screen caption (Joy Division,
  Star Guitar).
- **Art Visualizer's "Neon" palette renamed to "Hatsune Miku."** Its teal and
  pink duo reads as the character's colors, so the name says so.

### Fixed

- **Joy Division ridges no longer jitter during silence.** The per-ridge wiggle
  is now scaled by the band's own amplitude, so a silent signal draws a flat
  line instead of a constant tremor.
- **The window border can always be brought back.** Hiding it via Settings ▸
  Hide window border can be undone with `F10` as before, or now by tapping
  `Alt` alone — no separate hotkey to remember.

## [0.3.0] - 2026-09-03

_A bilingual interface, a rack of 3D spectrum cascades, and visualizers that name what inspired them._

### Added

- **English and Korean interface.** Every menu, dialog, overlay and the window
  title is now translated. Settings ▸ Language switches between Automatic,
  English and 한국어 without a restart; Automatic follows the Windows display
  language and falls back to English. The choice is remembered between runs.
- **3D spectrum cascade.** Two new built-ins render the spectrum over time as a
  surface that recedes into the distance: _Classic Cascade_, a shaded
  Winamp-style spectrogram, and _Joy Division_ (Inspired by Joy Division), the
  same history drawn as sharp white ridges on black. Both use a quarter-view
  projection with edge-pinned motion and expose tuning knobs in the settings menu.
- **Homage credits.** Built-in visualizers that take after a designer, artist or
  era now show an "Inspired by …" line when they start, and are collected on a
  new Help ▸ Credits screen and in the README. The names and works stay with
  their owners, outside VizRack's MIT license.

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

[Unreleased]: https://github.com/yimsemin/VizRack/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/yimsemin/VizRack/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/yimsemin/VizRack/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/yimsemin/VizRack/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/yimsemin/VizRack/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/yimsemin/VizRack/releases/tag/v0.1.0
