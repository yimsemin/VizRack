# VizRack

Portable Windows visualizer. It reads the current render endpoint through WASAPI
shared loopback (monitoring only — never a render client, never changes the
default device) and feeds stereo float32 to one selected built-in visualizer or
one whitelisted visualization VST3. VST audio output is discarded.

Windows x64 only. GDI/GDI+ for rendering. No AppData/registry — all state lives in
`data/` next to the EXE.

The project was previously named `mvmeter2-standalone-host`; that name is gone.
`mvmeter2` still exists as a **plug-in catalog id** (TBProAudio mvMeter2) and must
not be renamed.

## Build, test, run

`cmake` / `ctest` are not on `PATH`; use the copy bundled with VS Build Tools:
`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\`

```powershell
cmake --preset vs2026-x64
cmake --build out/build/vs2026-x64 --config Release
ctest --test-dir out/build/vs2026-x64 -C Release --output-on-failure
out/build/vs2026-x64/Release/VizRack.exe --smoke-test   # opens, self-closes, exit 0
```

- The VST3 SDK builds from the local `external/vst3sdk` checkout (no FetchContent
  download), pinned to `v3.8.0_build_66`.
- Tests are one file, `tests/test_main.cpp`, with a hand-rolled `CHECK` macro.
  Add a case as a function and call it from `main()`.
- No CI, no clang-tidy/clang-format config. Warnings are `/W4 /permissive-`; keep
  the build warning-clean.

## Architecture boundary — the one rule that matters

`src/builtin/` is the **only** source of truth for signal analysis, histories,
animation state, scene geometry, scene names, palettes and option normalization.
It is standard C++20: **no** Windows, GDI/GDI+, WASAPI, VST3, DOM, filesystem,
logging or app-lifetime dependencies. A future web build compiles this target to
WebAssembly; the web product does not exist yet — do not add a JS/TS copy of the
engines.

- `src/ui/` owns Win32 input, menus, timers, overlay text and GDI+ translation.
  It feeds samples and renders the resulting `DrawList` — it must not re-implement
  a scene formula.
- Adding or changing a `DrawPrimitive` is a cross-renderer contract change: update
  `draw_list.*`, the GDI+ renderer, and the core tests together.
- Options exposed to settings use the structs in the engine headers; normalize
  invalid persisted values inside `setOptions` so every adapter behaves the same.
- Engines take bounded sample batches and never do I/O. Reuse `DrawList`, scratch
  vectors and circular buffers — no steady-state per-frame heap growth (there are
  regression tests for this). Use measured frame time for decay/animation so
  15/30/60 FPS behave alike.

Built-in visualizers: `oscilloscope_engine`, `art_visualizer_engine` (six shared
scenes + palettes) and `campfire_engine`. See `docs/ARCHITECTURE.md` and
`docs/BUILTIN_VISUALIZER_CORE.md`.

## Keep the EXE small

No UI framework, DSP framework or serialization library for things the standard
library and Win32 already cover. Settings are hand-written JSON in
`core/settings.cpp`. Optimize only against a measurement.

## Branching and commits

- `main` — released code only, tagged `vX.Y.Z`; never commit directly.
- `develop` — the default working branch; long-lived, **pushed to `origin` after
  every step**.
- `topic/<slug>` — optional short-lived branch off `develop` for risky work.

Commit at each meaningful step, once `cmake --build` + `ctest` + `--smoke-test`
pass. One logical change per commit; never fold in unrelated changes or `out/`
artifacts. Conventional Commit subjects — imperative, English, ≤ ~72 chars:
`feat` / `fix` / `perf` (user-facing, become the changelog) and `refactor` /
`docs` / `test` / `build` / `chore` (internal). Keep the `Co-Authored-By: Claude`
trailer on AI-assisted commits.

Do **not** bump `project(VizRack VERSION …)`, create tags, or run
`gh release` during normal development. Full runbook: `docs/RELEASE_PROCESS.md`,
run only on the user's explicit approval of a version.
