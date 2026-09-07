<!-- README.ko.md mirrors the user-facing core of this file: the Introduction,
     "What you can watch", "Getting started", "Common controls", and "Portable
     storage and audio handling". When you change one of those sections here,
     update README.ko.md in the same commit. Everything else is English-only. -->

# VizRack

**English · [한국어](README.ko.md)**

**The music keeps playing; only the screen comes a little more alive.**

VizRack is a lightweight portable visualizer that puts a window on screen which
moves along with whatever music is playing on Windows. Park it in a small window
on the corner of your desk or a second monitor, for when you want something to
stare at while you listen.

It works right away — no separate audio driver, no external plug-in required.
Your music player and the Windows playback path are left untouched, so the music
keeps going even after you close VizRack.

## What you can watch

- **Art visualizer** — six scenes and six palettes that react to the low / mid /
  high bands and the stereo movement of the music
- **Oscilloscope** — left/right channel waveforms and the last ~15 seconds of the
  level history
- **Optional external visualizers** — install mvMeter2 or AnSpec to switch to the
  metering screen you prefer
- **Window settings for listening** — always on top, hidden border, opacity and a
  15 / 30 / 60 FPS control

## Getting started

VizRack runs on an **x64 PC with Windows 10 version 1703 or newer, or Windows
11**.

1. Extract `VizRack-win-x64.zip` completely into a local folder of your choice.
2. Run `VizRack.exe` from the extracted folder.
3. Play music or video as usual.
4. Choose `Plug-in > Built-in Art Visualizer > Use` and click the screen to
   change scenes.

To keep your settings, do not run it from inside the ZIP or place it in a
write-restricted folder such as `C:\Program Files`.

There is no code signing, so Microsoft Defender SmartScreen may show a warning.
Choose `More info > Run anyway` only when you trust where the file came from.

## Common controls

| Action | Result |
| --- | --- |
| Click the screen or `Space` | Next art scene |
| Arrow keys or number `1`–`6` | Select a scene |
| `C` | Next palette |
| Right-click | Current screen and window settings |
| `F10` | Open the menu while the border is hidden |

The `Settings` menu changes always-on-top, hidden border, opacity, the output
device and the UI language. By default VizRack follows the Windows default
output device automatically.

## Optional external visualizers

VizRack's VST 3 plug-in support is limited to the Windows x64 builds of the two
products below. It is not a general-purpose host that loads arbitrary VST 3
plug-ins.

| Product | Supported build | Official install page |
| --- | --- | --- |
| mvMeter2 | x64 VST 3, GPU or noGPU | [TBProAudio](https://www.tbproaudio.de/products/mvmeter2) |
| AnSpec | Windows x64 VST 3 | [Voxengo](https://www.voxengo.com/product/anspec/) |

After installing the product you need, choose
`Plug-in > product name > Auto-detect and use`. If it is not found, you can point
VizRack at the VST 3 file or bundle folder directly from the same menu. External
plug-ins are not bundled with VizRack.

## When something goes wrong

| Problem | What to check |
| --- | --- |
| The screen does not react to the music | Confirm the music is actually playing, then pick the right device under `Settings > Output device`. |
| Settings are not saved | Extract VizRack completely into a writable local folder and run it from there. |
| An external screen is not found | Confirm you installed the Windows **64-bit VST 3** build of the plug-in, then use auto-detect or pick it manually. |
| The mvMeter2 GPU screen does not open | Update your graphics driver or use the official noGPU build. |
| You want to start from a clean state | Quit VizRack, rename the `data` folder next to the executable, and start again. |

If the problem persists, check `data\logs\vizrack.log`. A crash may also leave a
`data\crash-*.dmp` file next to it.

## Portable storage and audio handling

Settings, plug-in locations and logs are all stored in the `data` folder next to
`VizRack.exe`. No AppData, no registry — so the whole folder is easy to move or
delete.

VizRack only reads the sound Windows is playing; it never stores audio samples or
writes them to the log.

<details>
<summary>Scope and limitations</summary>

- The built-in art visualizer is for enjoyment, not a precision metering tool.
- On multichannel output it uses the Front Left / Front Right channels.
- 32-bit, native ARM64, VST 2, AAX, ASIO and WASAPI Exclusive are not supported.
- There is no DAW transport, MIDI, automation or audio re-output.
- Virtual audio devices and external plug-ins may behave differently per setup.

</details>

<details>
<summary>Building and packaging</summary>

You need the **Desktop development with C++** workload of Visual Studio Build
Tools 2026, the Windows SDK, CMake 4.2 or newer and Git, plus a local Steinberg
VST 3 SDK checkout. From a Developer PowerShell:

```powershell
cmake --preset vs2026-x64
cmake --build --preset release
ctest --preset release
out\build\vs2026-x64\Release\VizRack.exe --smoke-test
```

Build the portable ZIP with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

The platform boundary and change rules for the built-in visualizers are in
[`docs/BUILTIN_VISUALIZER_CORE.md`](docs/BUILTIN_VISUALIZER_CORE.md); the overall
structure, including how localization works, is in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

</details>

## Credits

Some built-in visualizers take after the work below.

- **Built-in Joy Division** — Inspired by Joy Division

These names credit the source of inspiration only. The names and works stay with
their owners and are not covered by VizRack's MIT License.

## License

VizRack's code and its own app icon are released under the
[MIT License](LICENSE), Copyright (c) 2026 subProject. For the Steinberg VST 3
SDK included in the build and other third-party notices, see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

The name "VST" is used only to describe compatibility and is not relicensed under
VizRack's MIT License. VizRack is not affiliated with or endorsed by Steinberg.

VST is a registered trademark of Steinberg Media Technologies GmbH.
