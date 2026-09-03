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
scenes + palettes), `campfire_engine` and `spectrum3d_engine` (FFT time-depth
cascade; one engine backs both the `builtin-spectrum3d` and `builtin-joydivision`
catalog entries via a fixed `style`). See `docs/ARCHITECTURE.md` and
`docs/BUILTIN_VISUALIZER_CORE.md`.

## Localization

The Windows UI ships in English (default) and Korean. Every user-facing string in
`src/ui/`, `src/app.cpp` and `src/main.cpp` goes through `core/i18n` — add a
`VIZRACK_STR(Id, "English", "한국어")` row to `src/core/i18n_strings.inc` and use
`trw(Str::Id)` (or `tr` for UTF-8). Never write a raw `L"..."` user-facing literal
in a menu, dialog, overlay or the window title. The X-macro makes a half-translated
row a compile error; a `test_main.cpp` case checks both languages are non-empty.
Diagnostics (logger output and `error`/status strings from `src/core`,
`src/platform`, `src/vst`) stay English only; scene/palette names and stylized
overlay captions stay English by design. See `docs/ARCHITECTURE.md` ▸ Localization.

`README.md` is the canonical English readme. `README.ko.md` mirrors only its
Introduction and "What you can watch" sections (the file heads say so); when you
change those sections in `README.md`, update `README.ko.md` in the **same commit**.
Everything else stays English-only in `README.md`.

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
`feat` / `fix` / `perf` (user-facing) and `refactor` / `docs` / `test` /
`build` / `chore` (internal).

If a commit changes what a user sees, that same commit updates the
`## [Unreleased]` section of `CHANGELOG.md` — the changelog is curated by hand,
not generated from commit subjects. Wording rules and the release flow:
`docs/RELEASE_NOTES_STYLE.md`.

Do **not** add AI-attribution trailers to commit messages or PR descriptions —
no `Co-Authored-By: Claude`, no "Generated with Claude Code" line, no equivalent.
The `.githooks/commit-msg` hook strips them; enable it once per clone with
`git config core.hooksPath .githooks`.

Do **not** bump `project(VizRack VERSION …)`, create tags, or run
`gh release` during normal development. Full runbook: `docs/RELEASE_PROCESS.md`,
run only on the user's explicit approval of a version.
