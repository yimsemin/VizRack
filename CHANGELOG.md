# Changelog

All notable changes to VizRack are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). VizRack is pre-1.0, so
a minor bump (`0.X.0`) carries new visualizers or features and a patch bump
(`0.0.X`) carries fixes only.

Every released section below is also published, unchanged, as a
[GitHub Release](https://github.com/yimsemin/VizRack/releases) with the portable
ZIP attached. How these notes are written and cut: `docs/RELEASE_NOTES_STYLE.md`.

## [Unreleased]

### Added

- **Rhythm Ripple.** A new built-in visualizer: rain falling on a still, dark
  puddle in time with the music. A broadband onset detector drops one
  expanding-ring raindrop-ripple at a fully random spot the instant a beat is
  accepted — several can land in close succession during a busy passage, and
  a sensitivity option controls how easily a beat is detected. A second,
  independent long-note sensitivity (0 turns it off) grows a drifting "long
  note" ripple whenever a hit's level keeps holding instead of decaying like
  an ordinary beat — it travels a smooth constant-speed arc, marks only its
  start and end with a ripple (never re-bursting while held), lasts up to 5
  seconds, and several can be held at once.
- **View ▸ Show overlay text** (화면 안내 문구 표시) toggles every built-in
  visualizer's title, tagline and right-click hint off at once — useful for
  recording or streaming without the on-screen text. Persisted in settings.
- **Properties...** in a VST3 entry's submenu shows its detected version,
  edition (e.g. mvMeter2's GPU/noGPU build) and module path on demand.
- **Oscilloscope 120 FPS.** A fourth frame-rate option alongside 15/30/60, for
  displays that can show it smoother than this machine's own monitor might.
- **Star Guitar marks a silence gap with one big building.** A full second of
  silence sends one oversized (2x) building through the near layer as a
  visual cue for a track/section change — the only place a building appears
  in that layer.

### Changed

- **Menu bar** is now File (Exit) · View (output device, always-on-top,
  borderless, opacity, language) · Plug-in · Help, matching conventional
  Windows layout instead of mixing Exit into a settings catch-all. The
  borderless right-click menu still reaches Exit.
- **Built-in visualizer selection** is now one click on the plug-in's name —
  no more opening its submenu and choosing "Use" first. VST3 entries keep
  their submenu, since picking a file/folder is a real action.
- Every built-in visualizer's overlay caption (title, tagline, right-click
  hint) is now localized into Korean, and all five GDI+ engines share one
  overlay font rule (Segoe UI, 16px bold title / 12px regular hint, bumped up
  from 12/9 for readability) instead of each hardcoding its own size.
- Dropped the redundant "Built-in " prefix from every built-in visualizer's
  display name (plug-in menu, activation status, overlay taglines); "내장" is
  gone from the Korean equivalents the same way.
- Renamed the `builtin-spectrum3d` catalog entry from "3D Spectrum" to
  "Classic Cascade" so its menu name matches the on-screen title it shares
  with its sibling `builtin-joydivision` ("Joy Division").
- Retranslated an awkward Korean loanword: oscilloscope's history mode is now
  "시간 기록" (was "시간 히스토리").
- **Left click on the canvas no longer changes anything.** Art Visualizer
  (scene) and Classic Cascade/Joy Division (palette) used to cycle on click;
  every built-in visualizer's own state now lives behind the right-click menu
  only, matching the four built-ins that never had a click action. Art
  Visualizer's keyboard shortcuts (`Space`, arrows, `1`–`6`, `C`) still work.
- The `Plug-in` menu-bar label is a fixed name again instead of live status
  text ("Plug-in: searching" / the active plug-in's name) — the overlay text
  already shows the active visualizer, so the label was redundant chatter.
- **No dialog plays a sound anymore.** Every `MessageBoxW` in the app relied
  on an icon flag that also triggers Windows' alert beep; all of them
  (errors included) are silent now, since a beep while listening to music
  is exactly what this app should not do.
- Reworded five built-in visualizers' bottom-left tagline into a softer, more
  conversational Korean sentence instead of a stiff noun phrase, and updated
  four of the five English taglines to match that Korean meaning instead of
  an unrelated phrase left over from before the Korean was reworded
  (Rhythm Ripple's English was already equivalent, so it's unchanged):
  Art Visualizer "음악에 맞춰 움직여요" / "MOVES WITH THE MUSIC", Campfire
  "잔잔히 타오르는 모닥불" / "A QUIETLY BURNING CAMPFIRE", Rhythm Ripple
  "박자마다 번지는 물결" / "ONE BEAT, ONE RIPPLE", Star Guitar "리듬을 타고
  흐르는 풍경" / "A LANDSCAPE FLOWING WITH THE RHYTHM", Classic
  Cascade/Joy Division "시간을 타고 흐르는 소리" / "SOUND FLOWING THROUGH
  TIME".
- **Star Guitar's two sky sub-bands moved from 6-8/8-12kHz to 6-10/10-16kHz**
  (the higher one from >12kHz to >16kHz) and their cooldowns lengthened
  slightly, after both were observed spawning in visible clumps during dense
  hi-hat/cymbal passages. The higher band's object is now a diagonal
  (45-degree-rotated) variant of the star instead of a UFO — it was hard to
  tune down to a reasonable spawn rate and wasn't going to be reused
  elsewhere. Pine and tree also got more distinct canopy colors and a
  sharper pine taper so the two silhouettes read apart at a glance, and each
  building now gets its own lit-window ratio instead of sharing one fixed
  42%.

### Fixed

- Campfire's glowing coal bed sat at the back-rim reference line instead of
  the fuel bed the flame itself is anchored to, so it visibly floated just
  above where the fire actually starts. It now anchors to the same point as
  the flame.
- **Campfire's stars no longer glow below the fire line.** All 48 stars share
  one rotation around a pole below the sky, so without a per-star phase
  offset they swept past the horizon as one synchronized clump — visibly
  lighting up the ground while the sky briefly looked empty. Each star now
  starts at its own random point along its orbit, and anything that would
  still dip below the horizon fades out instead of rendering there.
- **Prism Tunnel's outermost ring no longer pops.** It was at peak
  brightness right before wrapping back to a small, dim ring each lap; it now
  fades out over the last stretch of its travel instead of cutting off.
- **Star Guitar's near layer no longer spawns a plain building as ambient
  filler.** It now falls back to a pine tree, matching the layer's two real
  object types (signal marker, pine) — see
  `docs/STAR_GUITAR_FREQUENCY_BANDS.md`.
- **Selecting a fixed-size plug-in (e.g. mvMeter2) while maximized no longer
  breaks the window.** The window now returns to its normal state first, so it
  resizes cleanly, draws correctly and can be dragged and resized afterwards.

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

[Unreleased]: https://github.com/yimsemin/VizRack/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/yimsemin/VizRack/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/yimsemin/VizRack/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/yimsemin/VizRack/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/yimsemin/VizRack/releases/tag/v0.1.0
