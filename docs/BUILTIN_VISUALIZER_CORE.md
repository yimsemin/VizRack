# Built-in Visualizer Core

## Purpose and current scope

The goal of this design is for the portable Windows EXE and a future browser
build to share a single copy of the built-in visualizers' signal processing and
scene code. The current work does not include a web page, WebAssembly bindings, a
Canvas renderer or web deployment configuration. The boundary was fixed first so
that the EXE actually consumes the shared core.

Widening the shared surface into a general-purpose framework is not the goal.
Only the following, which must behave identically in both products, is shared:

- Oscilloscope waveform and history computation.
- Art-visualizer low/mid/high-band and stereo response computation.
- Campfire beat/frequency response, ten-second silence decay, rising turbulence,
  lifetime-based embers, star trails around one shared celestial rotation axis, a
  shooting star roughly once per minute, and the dense black-log and stone-ring
  silhouettes.
- 3D spectrum: the hand-written 1024-point FFT, 40 logarithmic bands, the 72-slot
  time-history buffer, frame-time-based slice advance, the quarter-view
  perspective surface (`CLASSIC CASCADE`) and the non-fading hidden-line ridge
  stack (margins, edge clamping, contrast curve and pen jitter — `JOY DIVISION`),
  plus the rotation/tilt/depth/height 0–100 options (50 is the reference value;
  each style has a different response curve) and six palettes.
- Shape layout and animation state for the six art scenes.
- Six palettes and scene names.
- Option defaults and correction of invalid values.
- The platform-neutral minimal drawing commands.

WASAPI, Win32 windows and menus, GDI+, VST3 and file storage stay Windows-only. A
future web build's audio input, Canvas, browser storage and offline cache belong
to the web adapter.

## Code ownership

| Location | Responsibility | Forbidden dependencies |
| --- | --- | --- |
| `src/builtin/draw_list.*` | Colour/point and minimal render-command contract | OS, graphics API, DOM |
| `src/builtin/art_visualizer_engine.*` | Analysis, scenes, palettes, animation | Win32/GDI+, WASAPI, VST3, I/O |
| `src/builtin/campfire_engine.*` | Beat analysis, flame/log/ember/smoke/star geometry and animation | Win32/GDI+, WASAPI, VST3, I/O |
| `src/builtin/oscilloscope_engine.*` | Waveform, circular history, option correction | Win32/GDI+, WASAPI, VST3, I/O |
| `src/builtin/spectrum3d_engine.*` | FFT, logarithmic bands, time-axis history, two shapes and palettes | Win32/GDI+, WASAPI, VST3, I/O |
| `src/ui/gdi_draw_list_renderer.*` | Translate shared commands into GDI+ calls | Scene/DSP formulas |
| `src/ui/gdi_back_buffer.*` | Win32 back buffer rebuilt only on resize | Scene/DSP formulas |
| `src/ui/*_view.*` | Ring input, timers, menus, keyboard, text overlays | Scene duplication |

The `vizrack_builtin_core` CMake target is declared before the VST3 and Windows
targets. Configuring the top-level CMake on a non-Windows platform currently
produces only this core target, so a platform header that leaks in by mistake is
caught early. The Windows EXE links the same static library.

## Execution flow

```text
WASAPI capture thread
    │  StereoFrameRing (float32 L/R, non-blocking)
    ▼
Win32 built-in view timer
    │  copies up to the latest 4,096 frames straight into the engine input span
    ▼
Shared C++ engine
    │  analysis and state update → DrawList
    ▼
GDI+ command translator
    │  reused back buffer
    ▼
Win32 child HWND
```

In a future web build the middle `shared C++ engine → DrawList` does not change.
The front is replaced with user-permitted browser audio input and the back with
Canvas 2D or another browser renderer. A new scene or response formula edited
once in `src/builtin` is therefore used by both products.

## The `DrawList` contract

The current commands are a vertical gradient, a radial ellipse gradient, a line,
a polyline, an arc, a filled or outlined ellipse, a filled or outlined polygon
and a filled rectangle. Commands carry only coordinate and colour data and hold
no GDI+ or Canvas objects. For the radial ellipse gradient, `primary` is the
centre colour and `secondary` is the edge colour. Text stays in the adapter
because it is close to menu and status display and to localization.

When building a new scene, prefer combining existing commands. If a new command
is genuinely required, that is a renderer-contract change, not a simple scene
edit: every renderer and its tests must change in the same commit. This rule
prevents editing one adapter and breaking another.

Shared source alone does not automatically guarantee pixel-identical output.
GDI+ and Canvas can differ in anti-aliasing, line caps and alpha compositing, so
once a web renderer exists, add per-command contract tests and
representative-scene image comparisons.

## Performance design

To keep runtime cost down, this split follows these principles:

- The view reads from the ring straight into the engine's fixed arrays instead of
  building a separate temporary sample vector.
- The oscilloscope's latest waveform and roughly 15-second history use a
  fixed-size circular buffer rather than `memmove`.
- Scene scratch vectors and the `DrawList` are reserved once to their maximum,
  then cleared each frame to reuse the capacity.
- Rendered point counts are capped at 2,048 per oscilloscope channel and 720 per
  art-scene path.
- The Win32 compatible DC and bitmap are created only when the window size
  changes, not every frame.
- GDI+ solid pens and brushes are reused within a frame rather than created per
  command.
- Art-scene decay and phase advance use the measured frame interval, so behaviour
  differs less across refresh rates.
- Campfire's natural motion and decay also use the measured frame interval, and
  the flame polygon and ember counts have fixed ceilings. Embers update lifetime
  and velocity in a fixed array; the stars share one deterministic elliptical
  rotation transform and delayed pulses held in a fixed array. The shooting star
  is likewise reserved as a single fixed state, so no new container is allocated
  per frame.

This core does not run in the audio callback; it runs at UI refresh time. Because
both the sample count and the shape count are bounded, CPU and memory use do not
grow without limit as the window grows or the process runs longer. The heaviest
current scene is `PULSE MATRIX`, which emits roughly 1,000 shape commands; it is
tested to stay within the reserved capacity even in a large window.

Further optimization proceeds only against a measurement. A GPU framework, a
general-purpose scene graph, a separate DSP library or a UI framework would grow
the EXE, the web bundle and the maintenance surface together, so none fits the
current requirements.

## Changing a built-in feature

1. Edit scenes, analysis, palettes or options in `src/builtin`.
2. If only existing `DrawPrimitive`s were used, the Win32 adapter normally needs
   no change.
3. If the command contract changed, update every renderer in the same commit.
4. In `VizRackTests`, verify every scene and palette, valid coordinates and point
   ranges, option correction, and capacity reuse across repeated frames.
5. Run the Release build and `--smoke-test`, and verify visual changes by hand
   with real audio.
6. Once a web target exists, bind the native and web builds to the same CI change
   conditions.

The field names and meanings of persisted settings must also be shared by both
adapters. The browser storage format itself may differ, but the option structs
and defaults passed to the engine must follow the shared headers. Campfire's
flame response, star travel speed, star idle brightness, star music response and
ember amount and intensity are all 0–100 values in `CampfireOptions`,
range-corrected by the engine. The flame-response default is 20; the previous
strong response corresponds to about 80.

## Minimum work for a future web build

Starting web work needs a thin connection layer, not a rewrite or a translation
of the shared engine.

1. Compile `vizrack_builtin_core` to Emscripten/WebAssembly.
2. Add a small binding that exposes the PCM input span, `update`, `buildFrame` and
   option read/write.
3. Write a renderer that translates `DrawList` commands into Canvas 2D calls.
4. Implement the permission-gated audio input and the UI and storage in the
   browser adapter.
5. Decide static-asset caching, offline start and deployment configuration in the
   web adapter.

VST3 hosting is not part of this path. Browser security policy also means you
cannot assume unconditional system-wide audio capture the way the Windows EXE
has; that is a product constraint of the web input adapter, not of the shared
core.
