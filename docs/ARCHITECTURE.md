# VizRack Architecture

## Invariants

VizRack is a monitoring-only host. It reads a Windows render endpoint through
WASAPI shared loopback and feeds a copy to one selected built-in visualization
or whitelisted visualization VST3. VST output is discarded. It never creates
a render client, changes the default endpoint, inserts itself into the playback graph or requires a
virtual audio driver.

The shared visualization contract is stereo float32 input. Built-in views are
GDI/GDI+ child HWNDs; external plug-ins require a Windows x64 VST3 Audio Effect and
a Win32 `IPlugView`. Multichannel capture selects only Front Left/Front Right.
The application intentionally has no generic VST scanner.

## Components

| Component | Responsibility |
| --- | --- |
| `App` | Lifetime, selected plug-in transition, settings and shutdown |
| `MainWindow` | Win32 frame, title/menu state and visualization child parent |
| `WasapiCapture` | Loopback capture, format conversion, device events and reconnect |
| `StereoFrameRing` | Fixed-capacity SPSC audio transfer |
| `builtin::OscilloscopeEngine` | Platform-neutral L/R waveform and circular history geometry |
| `builtin::ArtVisualizerEngine` | Platform-neutral analysis and six-scene/six-palette geometry |
| `builtin::CampfireEngine` | Platform-neutral beat analysis and procedural flame, ember, log and night-sky geometry |
| `builtin::DrawList` | Minimal renderer-neutral primitive and point buffer contract |
| Built-in `*View` adapters | Win32 input, timers, menus and text overlays |
| `GdiDrawListRenderer` / `GdiBackBuffer` | GDI+ command translation and size-stable back buffering |
| `VstHost` | VST3 factory, processing, state and editor lifetime |
| `plugin_catalog` | Stable IDs, matching rules, search locations, labels and official URLs |
| `plugin_discovery` | Definition-scoped enumeration and compatibility validation |
| `plugin_storage` | Per-ID location and state paths |
| `atomic_file` | Shared write-through replacement for persistent files |
| `ReconnectPolicy` | Bounded retry state and delays |
| `Logger` | Diagnostics and size-based rotation |

## Data flow and threads

```text
Windows audio engine
       │ WASAPI shared loopback (read only)
       ▼
Capture thread
  event wait → mix-format conversion → Front L/R
       │ non-blocking SPSC ring
       ├─ built-in selected → Win32 UI timer → latest 4,096 samples
       │       → portable built-in engine → DrawList → reusable GDI/GDI+ back buffer
       │
       └─ VST3 selected → VST processing thread
            newest backlog → preallocated 2,048-frame buffers
                 │ selected VST3 process(); output discarded
                 ▼
            Win32 UI thread → IPlugView/OpenGL child → controller edits
```

The capture thread performs no file I/O or UI work. If the consumer falls
behind, old monitoring frames are dropped instead of blocking capture or
accumulating latency. Only the selected visualization consumes the ring. The
VST processing thread sends periodic silence while idle so meters can preserve
their decay behavior; the built-in view needs no additional worker thread.

## Built-in portability boundary

`src/builtin` is a standard C++20 target with no Windows, graphics-API, VST3, I/O or
application-lifetime dependency. It owns the behavior that must remain identical in a
future browser build: sample analysis, histories, animation, scenes, palettes, option
normalization and geometry. The Win32 views only feed samples and render the resulting
`DrawList`; scene formulas must not be duplicated in an adapter.

The top-level build declares `vizrack_builtin_core` before native dependencies and can
configure that target alone on non-Windows platforms. A future web build should compile
this target to WebAssembly and add a Canvas renderer rather than porting the scene code
to JavaScript. The web product and adapter are not implemented yet. Detailed ownership,
performance constraints and change rules are in
[`BUILTIN_VISUALIZER_CORE.md`](BUILTIN_VISUALIZER_CORE.md).

## Audio capture and recovery

`IAudioClient` uses shared loopback, event callbacks and `NOPERSIST`. Endpoint
mix formats supported by the converter are PCM16, packed PCM24,
valid-bits-in-PCM32 and float32. `WAVE_FORMAT_EXTENSIBLE` channel masks locate
Front L/R; without a mask, channels 0 and 1 are used.

`IMMNotificationClient`, capture failures and `WM_POWERBROADCAST` request
reinitialization. Retry delay grows from 500 ms to 16 seconds and resets after
a device event or successful connection. The producer stays non-blocking while
a plug-in is switched or audio is reconnecting.

## Plug-in model

`PluginDefinition` is the only plug-in-specific configuration surface. Its kind
selects the built-in or VST3 activation path, and it contains a filesystem-safe
immutable ID, display/vendor/class metadata,
optional binary marker, dedicated search files/directories, edition label and
official URL. Discovery checks only the selected definition's saved location
and search locations, then validates AMD64 PE, marker and VST3 factory data.

The first catalog entry remains `builtin-oscilloscope`, so a fresh portable data
folder always starts without an external dependency. `builtin-art-visualizer`
adds six switchable scenes and six palettes. `builtin-campfire` adds a dedicated
procedural flame whose natural motion continues without audio. Beat transients change
flame height without widening its base, high frequencies emit rising embers, and stars
share one celestial rotation axis and leave fading elliptical trails in the same
direction. A beat schedules pulses for different subsets of stars with short delays
instead of flashing the whole sky at once. A deterministic random timer emits one brief
shooting star roughly once per minute. After ten seconds without an audible signal the
fire and its surrounding glow settle to a small idle state. The scene has no horizon or
ground plane; a compact single-layer stone ring and solid log silhouettes anchor the
fire directly in the dark background.
Right-click options control flame response, star speed/brightness/response and ember
amount/intensity on a normalized 0–100 scale; those values are additive fields in the
portable settings schema.

`builtin-spectrum3d` and `builtin-joydivision` share one `Spectrum3dEngine`: a
hand-written 1024-point radix-2 FFT feeds 40 log-spaced bands whose newest spectrum is
pushed to the front of a fixed 72-slice history buffer while older slices recede. The
slice cadence is driven by measured frame time so 15/30/60 FPS scroll alike.
`builtin-spectrum3d` renders a quarter-view perspective **surface** — filled strips
between consecutive time slices plus rib outlines (`CLASSIC CASCADE`).
`builtin-joydivision` stacks hidden-line ridge curves at full strength (no depth fade)
inside a centred plot box, with the outer band edges held flat on the baseline, a
crest-to-trough contrast curve and a deterministic per-slice pen tremble (`JOY
DIVISION`). Style is fixed per catalog entry. Each style reads the same right-click
knobs — `rotation`, `tilt`, `depth`, `heightScale` (normalized 0–100, `50` is the
reference look) and the six-palette choice — through its own response curve; each is an
additive `spectrum3d*` / `joyDivision*` field in the settings schema.

The `mvmeter2` definition uses
TBProAudio/mvMeter2 factory metadata and accepts both the GPU and noGPU x64 modules.
Renderer-specific binary markers are intentionally not required because both editions
expose compatible factory metadata.
`mvmeter2` is a plug-in ID, not an application ID.

Switching follows one transaction:

1. Retain the current kind, descriptor and definition.
2. Stop the current consumer and save VST3 state when applicable.
3. Detach the built-in view or VST3 editor and unload the module when applicable.
4. Attach and start the target kind.
5. Commit selection and VST3 location only after success.
6. If activation fails, restore the previous built-in view or VST3 instance.

Adding a plug-in that satisfies the existing processing contract normally
requires only one catalog entry. A different bus, sample format or editor
contract must be represented as a host capability and implemented centrally;
do not add parallel plug-in-specific menu, discovery or storage paths.

## Editor sizing

The view is hosted in a clipped child HWND. `IPlugFrame::resizeView`,
`IPlugViewContentScaleSupport`, `WM_DPICHANGED` and `onSize` cover plug-in and
host sizing. When `canResize()` is false, the frame removes its external resize
border and maximize button, then follows the view's requested size exactly.
This prevents clipped content and unused margins while preserving a plug-in's
own resize control.

All built-in views always fill the same clipped parent and accept any positive
host size, so they share window/menu behavior without VST3 sizing code.

## Portable state

All paths derive from `GetModuleFileNameW`:

```text
data/
├─ settings.json
├─ plugins/<plugin-id>/
│  ├─ location.txt
│  ├─ plugin-state.bin
│  └─ plugin-state.corrupt.bin
├─ logs/vizrack[.N].log
└─ crash-*.dmp
```

Settings, location and state writes share the atomic-file helper. The state
envelope stores a version, VST class ID, separate component/controller blobs
and CRC32. Invalid state is quarantined and the plug-in starts with defaults.
Missing state on first use is normal and silent. Restore diagnostics for a
successfully opened plug-in are written only to the log, not the window status.
Stable IDs isolate state across plug-ins; never rename a released ID without an
explicit migration. Unsupported settings schema versions are rejected instead
of being partially interpreted. There is no AppData or registry fallback.

## Shutdown order

1. Stop and join WASAPI capture.
2. Stop and join VST processing (`setProcessing(false)`, then `setActive(false)`).
3. Save VST3 state when portable storage is writable.
4. Remove `IPlugView` and clear its frame, or destroy the built-in child view.
5. Release VST objects, unload the module and save window settings.

This prevents callbacks into destroyed UI or plug-in objects.
