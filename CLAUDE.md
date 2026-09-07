# VizRack

Portable Windows visualizer. It reads the current render endpoint through WASAPI
shared loopback (monitoring only — never a render client, never changes the
default device) and feeds stereo float32 to one selected built-in visualizer or
one whitelisted visualization VST3. VST audio output is discarded.

Windows x64 only. GDI/GDI+ for rendering. No AppData/registry — all state lives in
the `data/` folder next to the EXE.

The project was previously named `mvmeter2-standalone-host`; that name is gone.
`mvmeter2` survives only as a **plug-in catalog id** (TBProAudio mvMeter2) — never
rename a released id.

## Build, test, run

`cmake` / `ctest` are not on `PATH`; use the copy bundled with VS Build Tools:
`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\`

```powershell
cmake --preset vs2026-x64
cmake --build --preset release
ctest --preset release
out\build\vs2026-x64\Release\VizRack.exe --smoke-test   # opens, self-closes, exit 0
```

- One-time per clone: `git config core.hooksPath .githooks` (commit-message hook).
- The VST3 SDK builds from the local `external/vst3sdk` checkout (pinned
  `v3.8.0_build_66`), not FetchContent.
- Tests are one file, `tests/test_main.cpp`, with a hand-rolled `CHECK`. Add a case
  as a function and call it from `main()`.
- No CI, no clang-tidy/clang-format. Warnings are `/W4 /permissive-` — keep the
  build warning-clean.

## The portability boundary — the one rule that matters

`src/builtin/` is the **only** source of truth for signal analysis, histories,
animation state, scene geometry, scene/palette names and option normalization. It
is standard C++20 with **no** Windows, GDI/GDI+, WASAPI, VST3, DOM, filesystem,
logging or app-lifetime dependency — a future web build compiles it to WebAssembly,
so never add a JS/TS copy of an engine.

- `src/ui/` owns Win32 input, menus, timers, overlay text and GDI+ translation. It
  feeds samples and renders the resulting `DrawList`; it must not re-implement a
  scene formula.
- Changing a `DrawPrimitive` is a cross-renderer contract change: `draw_list.*`,
  the GDI+ renderer and the core tests move in one commit.
- Normalize invalid persisted options inside `setOptions` so every adapter behaves
  the same. Engines take bounded sample batches, do no I/O, and reuse their
  buffers — no steady-state per-frame heap growth (there are regression tests).

Engines: `oscilloscope_engine`, `art_visualizer_engine` (six scenes + palettes),
`campfire_engine`, `spectrum3d_engine` (one engine backs `builtin-spectrum3d` and
`builtin-joydivision` via a fixed `style`). Detail:
`docs/BUILTIN_VISUALIZER_CORE.md`, `docs/ARCHITECTURE.md`.

## Localization

English (default) and Korean. Every user-facing string in `src/ui/`, `src/app.cpp`
and `src/main.cpp` resolves through `core/i18n` — add a
`VIZRACK_STR(Id, "English", "한국어")` row to `src/core/i18n_strings.inc` and use
`trw(Str::Id)` / `tr`. Never write a raw user-facing `L"..."` in a menu, dialog,
overlay or the window title. Diagnostics (logger and `error`/status strings from
`src/core`, `src/platform`, `src/vst`) stay English; scene/palette names and
stylized overlay captions stay English by design. Mechanism:
`docs/ARCHITECTURE.md` ▸ Localization.

`README.md` is canonical. `README.ko.md` mirrors only the sections its own header
lists — when you change one of those sections, update `README.ko.md` in the same
commit.

## Keep the EXE small

No UI, DSP or serialization framework for what the standard library and Win32
already cover. Settings are hand-written JSON in `core/settings.cpp`. Optimize only
against a measurement.

## Branching, commits, releases

`main` = released + tagged code only. `develop` = the integration branch; only
`--no-ff` merges and release-prep commits land there. Feature work goes on
short-lived `<type>/<slug>` branches (one git worktree each), rebased on
`origin/develop`. Commit once `cmake --build` + `ctest` + `--smoke-test` pass; one
logical change per commit. A commit that changes what a user sees also edits
`## [Unreleased]` in `CHANGELOG.md`.

Full rules — parallel work, the merge procedure, commit conventions, the release
runbook — are in **`docs/RELEASE_PROCESS.md`**; changelog wording is in
`docs/RELEASE_NOTES_STYLE.md`. Do **not** bump `project(VizRack VERSION …)`, tag,
or run `gh release` during normal development — release runbook only, on the
user's explicit approval of a version.
