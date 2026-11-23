# VizRack

Portable Windows visualizer. It reads the current render endpoint through WASAPI
shared loopback (monitoring only — never a render client, never changes the
default device) and feeds stereo float32 to one selected built-in visualizer or
one whitelisted visualization VST3. VST audio output is discarded.

Windows x64 only. GDI/GDI+ for rendering. No AppData/registry — all state lives in
the `data/` folder next to the EXE.

`mvmeter2` is a **plug-in catalog id** (TBProAudio mvMeter2) — never rename a
released id.

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
  `v3.8.1_build_84`), not FetchContent.
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

The current list of engines and what each owns is the "Code ownership" table in
`docs/BUILTIN_VISUALIZER_CORE.md` — that table is the one place it's written
down; don't re-enumerate it here or anywhere else, just link to it. Broader
component context: `docs/ARCHITECTURE.md`.

## Localization

English (default) and Korean. Every user-facing string in `src/ui/`, `src/app.cpp`
and `src/main.cpp` resolves through `core/i18n` — add a
`VIZRACK_STR(Id, "English", "한국어")` row to `src/core/i18n_strings.inc` and use
`trw(Str::Id)` / `tr`. Never write a raw user-facing `L"..."` in a menu, dialog,
overlay or the window title — this includes each built-in visualizer's overlay
caption (its title, tagline and control hint), which are equally in scope: reuse
`Str::HintRightClickOptions` ("RIGHT CLICK: OPTIONS") for that hint instead of
inventing a new string per engine — every built-in visualizer's own state
(scene, palette, style, sensitivity, …) is set through its right-click menu, so
this is the one hint every engine needs. Diagnostics
(logger and `error`/status strings from `src/core`, `src/platform`, `src/vst`)
stay English. The one exception is scene/palette names sourced from `src/builtin/`
engine code (e.g. art visualizer scene names, spectrum3d style/palette names) —
those stay English because they live in the portable core, which must not depend
on `core/i18n` (see the portability boundary above). Mechanism:
`docs/ARCHITECTURE.md` ▸ Localization.

`README.md` is canonical. `README.ko.md` mirrors only the sections its own header
lists — when you change one of those sections, update `README.ko.md` in the same
commit.

## UI conventions for a new built-in visualizer

When adding a new built-in visualizer, follow the pattern the existing seven
share (`docs/ARCHITECTURE.md` ▸ UI conventions for the details):

- **Menu**: add one `PluginKind::builtIn` catalog entry. It appears as a single
  clickable item directly under Plug-in — no submenu, no confirmation step, since
  a built-in needs no file/folder to select first. That behavior is generic in
  `MainWindow::createMenus`; a new built-in gets it for free.
- **Overlay font**: use `overlayTitleFont()` / `overlaySmallFont()` from
  `src/ui/overlay_font.h` — never a locally declared `Gdiplus::Font`. Wrap a
  stylized title (a plugin display name drawn in caps) with `overlayCaps()`.
- **Overlay strings**: every caption goes through `core/i18n` (see Localization
  above) — reuse the shared hint strings where the control scheme matches.
- **Overlay visibility**: keep a `showOverlay_` member (default `true`), a
  `setShowOverlay(bool)` setter, and skip the `drawOverlay` call in `paint()`
  when it is false, so `Settings::showOverlayText` (View ▸ Show overlay text)
  can hide it. Wire the setter into `App::applyOverlayVisibility`.
- **Right-click sets visualizer state**: every per-visualizer setting (scene,
  palette, sensitivity, view angle, …) is reachable through its right-click
  menu. A direct-manipulation gesture (e.g. dragging to rotate) is an extra way
  to change the same value, never the only way.
- **Register the engine**: add one row to the "Code ownership" table in
  `docs/BUILTIN_VISUALIZER_CORE.md` — that table is the single place the
  engine list is written down (see "The portability boundary" above), so this
  is the only doc edit adding an engine requires. Also add a bullet to that
  doc's "shared surface" list if the new engine owns any option/animation
  rule worth calling out, mirroring the existing entries' style.

## Dialogs never make noise

Every `MessageBoxW` call passes `MB_OK` / `MB_YESNOCANCEL` alone, with **no**
`MB_ICON*` flag — an icon flag makes Windows play its system alert sound, which
is jarring while listening to music. This applies to every dialog, present and
future: error, warning and info alike.

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

Deeper design docs: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md),
[`docs/BUILTIN_VISUALIZER_CORE.md`](docs/BUILTIN_VISUALIZER_CORE.md),
[`docs/RELEASE_PROCESS.md`](docs/RELEASE_PROCESS.md).

<!-- ysm-baseline:start -->
## 공통 작업 규칙 (ysm baseline)

이 구역은 ysm-skills 베이스라인이 관리한다. 프로젝트 고유 규칙은 이 구역 밖에 쓰고, 충돌하면 프로젝트 규칙이 우선한다. 소통·판단·검증 같은 공통 진행 규칙은 전역 지침(ysm-global)이 맡는다.

### 문서

- 루트에는 `README.md`·`AGENTS.md`·`CLAUDE.md`·`LICENSE`·`CHANGELOG.md`처럼 관례상 필요한 파일만 두고, 기획·인수인계·결정 기록·가이드 같은 나머지 문서는 `docs/`에 둔다. 사용자가 폴더에 넣어 둔 입력 문서도 `docs/`로 옮긴다.
- 같은 규칙·사실은 한 문서에만 쓰고 나머지는 링크한다. 문서 종류마다 자리를 하나로 정한다(README는 개요·사용법, 로드맵은 앞으로 할 일만이며 끝난 항목은 지운다). 문서를 늘리기보다 합친다. 날짜를 박은 "현재 수준" 표를 두지 않는다.
- 방향이 바뀌거나 큰 변경이 끝나면 문서 간 모순·중복·낡은 내용을 점검한다.

### UI가 있는 프로젝트

- 첫 방문자 기준으로 설계한다. 지금 무엇이 진행 중인지(진행도·완료·결과 없음 안내)가 보여야 하고, 용어와 단위가 화면 전체에서 같아야 하며, 내부 용어를 노출하지 않는다.
- 영역 구분은 박스를 남발하지 말고 글자 크기·굵기와 일관된 간격으로 한다. 글자는 12pt 이상, 한국어는 어절 단위로 줄바꿈하고, 외부 폰트·CDN 없이 로컬 자산을 쓴다.

### Git

- stage·commit·push·PR·merge는 사용자가 요청할 때만 한다. 승인은 그 요청에만 유효하고 다음 작업으로 이어지지 않는다. 이 파일의 프로젝트 구역이 명시한 상시 승인만 예외다.
- force push, 푸시한 이력의 수정, 브랜치·태그 강제 삭제, 공유 브랜치의 `reset --hard`는 사용자가 그 대화에서 직접 요청했을 때만 한다. 푸시 전 로컬 커밋의 amend·fixup 정리는 이력 재작성이 아니다. `--no-verify`로 훅을 우회하지 않는다.
- 첫 커밋 이후의 작업은 `main`이 아닌 작업 브랜치(`feat/` `fix/` `docs/` `chore/` + kebab-case)에서 한다. 프로젝트가 `main` 직행을 명시하면 예외다. 병렬 작업은 브랜치마다 `git worktree`로 분리한다.
- 커밋은 의미·작업 단위 하나씩 만든다. 같은 목적의 후속 수정(오타·누락·리뷰 반영)은 새 커밋으로 쌓지 말고 푸시 전에 amend·fixup으로 합쳐 원격 이력을 깔끔하게 유지한다.
- 커밋 제목은 `<type>(<선택 scope>): <요약>`이다. type은 `feat` `fix` `perf` `refactor` `docs` `test` `build` `ci` `chore` `style` `revert`, 72자 이내, 끝에 마침표 없음. 형식은 `.githooks/commit-msg`가 검사한다. 이유가 필요하면 본문에 쓴다. 제목 언어는 프로젝트가 정한다.
- 커밋 이메일은 GitHub noreply 주소를 쓴다(프로젝트에 설정되어 있으면 `.githooks/pre-commit`이 검사한다).
- PR 본문은 요약(무엇을·왜), 확인한 방법(실제 실행한 것만), 남은 결정 순서로 짧게 쓴다(`.github/pull_request_template.md`).
- 커밋 메시지와 PR 본문에 AI 생성 표식이나 co-author 트레일러(`Co-Authored-By: Claude …`, `🤖 Generated with …` 등)를 넣지 않는다. 커밋은 훅이 제거하지만 PR 본문은 직접 지킨다.
- 병합이 끝난 브랜치는 로컬·원격 모두 지운다. 푸시·병합까지 요청받은 작업은 로컬과 원격이 일치하고 미커밋 변경이 없는 상태로 마친다.
- 새 클론에서는 한 번 `git config core.hooksPath .githooks`를 실행한다(Claude Code는 세션 시작 때 자동 실행).

### 의존성과 환경

- 의존성·가상환경·캐시·데이터·설정·로그는 프로젝트 폴더 안에만 두어, 폴더를 지우면 함께 사라지게 한다. 설치물과 결과물을 프로젝트 밖(스크래치패드, 전역, 에이전트 메모리)에 두지 않는다. 폴더 하나만 받아도 다른 컴퓨터에서 이어갈 수 있어야 한다. 폴더 밖 저장은 사용자가 명시적으로 고른 경우만 허용한다.
  - Node: `package.json`의 `devDependencies`와 `npx`.
  - Python: `uv`로 프로젝트 `.venv`만 쓴다(`uv sync`, `uv add`).
  - 런타임 버전은 `.node-version`, `.python-version`에만 고정한다. `mise.toml`은 두지 않는다.
- macOS와 Windows에서 같은 동작을 하도록 플랫폼 중립 경로와 LF 줄바꿈을 쓴다.

### 안전

- 저장소는 언제 공개되어도 문제가 없는 상태로 유지한다. `.env`, 키, 토큰, 개인정보, 개인 절대경로, 개인 취향·운영 데이터, 실제 업무 문서를 저장소에 넣거나 읽어 출력하지 않는다. 환경변수 예시는 값이 없는 `.env.example`만 둔다.
- 비밀값으로 보이는 파일·문자열이 staged에 있으면 `.githooks/pre-commit`이 커밋을 막는다.
<!-- ysm-baseline:end -->
