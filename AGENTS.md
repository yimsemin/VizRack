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

<!-- ysm:commit-lang=en -->
`main` is the only long-lived branch and is protected: work goes on short-lived
`<type>/<slug>` branches (one git worktree each), rebased on `origin/main`, and
lands through a squash-merged PR. Commit once `cmake --build` + `ctest` +
`--smoke-test` pass; one logical change per commit. A commit that changes what a
user sees also edits `## [Unreleased]` in `CHANGELOG.md`.

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

이 구역은 ysm-skills 베이스라인이 관리한다. 프로젝트 고유 규칙은 이 구역 밖에 쓴다. 충돌하면 베이스라인을 따르는 것이 기본이고, 프로젝트 규칙에 유지할 만한 특별한 이점이 있으면 프로젝트 규칙을 유지하며, 이점이 분명하지 않거나 애매하면 사용자에게 묻는다. 단 Git 구역은 예외 선언 항목 외에는 프로젝트 규칙이 대체하지 못한다. 소통·판단·검증 같은 공통 진행 규칙은 전역 지침(ysm-global)이 맡는다.

### 문서

- 루트에는 `README.md`·`AGENTS.md`·`CLAUDE.md`·`LICENSE`·`CHANGELOG.md`처럼 관례상 필요한 파일만 두고, 기획·인수인계·결정 기록·가이드 같은 나머지 문서는 `docs/`에 둔다. 사용자가 폴더에 넣어 둔 입력 문서도 `docs/`로 옮긴다.
- 같은 규칙·사실은 한 문서에만 쓰고 나머지는 링크한다. 문서 종류마다 자리를 하나로 정한다(README는 개요·사용법, 로드맵은 앞으로 할 일만이며 끝난 항목은 지운다). 문서를 늘리기보다 합친다. 날짜를 박은 "현재 수준" 표를 두지 않는다.
- 방향이 바뀌거나 큰 변경·커밋 묶음·릴리즈가 끝나면 요청이 없어도 낡은 정보·중복·모순·사족을 점검하고 무엇을 고쳤는지 보고한다(스킬 `audit-docs`).
- `CHANGELOG.md`는 Added·Changed·Fixed·Removed로 나누고, 같은 주제는 한 줄로 합쳐 간결하게 쓴다.
- 작업 끝에 다음에도 쓸모 있는 시행착오만 문서나 스킬에 사족 없이 남긴다.

### UI가 있는 프로젝트

- 첫 방문자 기준으로 설계한다. 지금 무엇이 진행 중인지(진행도·완료·결과 없음 안내)가 보여야 하고, 용어와 단위가 화면 전체에서 같아야 하며, 내부 용어를 노출하지 않는다.
- 영역 구분은 박스를 남발하지 말고 글자 크기·굵기와 일관된 간격으로 한다. 글자는 12pt 이상, 한국어는 어절 단위로 줄바꿈하고, 외부 폰트·CDN 없이 로컬 자산을 쓴다.

### 사용자 문구가 있는 프로젝트

UI·CLI·오류 안내처럼 사용자나 에이전트에게 보이는 문구가 여러 개 있거나 다른 언어가 추가될 가능성이 있는 프로젝트에 적용한다. 단일 언어여도 같다.

- 문구는 코드에 직접 쓰지 않고 한 곳의 카탈로그에서 키로 가져온다. 번역 파일·로케일 라이브러리는 두 번째 언어가 실제로 생길 때 도입한다.
- 키는 문장이 아니라 의미로 짓는다(`login.error.expired`). 모든 언어에 같은 키와 같은 변수가 있어야 한다.
- 문장 조각을 이어 붙이지 않고, 변수가 든 완성 문장 하나를 키로 둔다.
- 오류는 문구가 아니라 안정적인 영문 종류 코드로 구분한다. 분기·재시도·화면 선택이 문구를 보고 동작하면 안 된다. 종류 목록은 `docs/`에 둔다.
- 용어는 `docs/`의 용어집 한 파일에 정하고 한 개념에 한 단어만 쓴다. 개발자용 로그·진단 문구는 사용자 문구와 분리해 카탈로그에 넣지 않는다.
- 에이전트용 문구(요청문, CLI 안내)도 같은 카탈로그를 쓰되 언어는 받는 에이전트 기준으로 정한다.
- 기존 문구는 스킬 `audit-messages`로 점검한다. 큰 문구 변경이나 언어 추가 전후에 요청이 없어도 한다.

### 로컬 서버가 있는 프로젝트

- 비개발자가 주소·명령어 없이 쓰도록 루트에 더블클릭 실행 파일을 둔다(Windows `.cmd`, macOS `.command`). 서버는 그 창에서 직접 실행하고(백그라운드·분리 금지), 창이 열려 있는 동안만 켜지고 닫으면 꺼진다. 창 제목에 이 사실을 적는다.
- 주소와 포트는 용도에 맞게 고른다. 본인 컴퓨터에서만 쓰면 외부 접속이 안 되는 주소로, 다른 기기에서 접속해야 하면 그 범위에만 연다. 포트는 다른 프로젝트와 겹치지 않는 값을 고르고, 사용 중이면 다음 포트를 쓴다. 브라우저는 자동으로 연다.
- 같은 프로젝트의 서버는 하나만 켠다. 이미 켜져 있으면 새로 띄우지 않고 기존 주소를 브라우저로 열며, 켜진 서버가 있다는 안내를 보여 준다.
- 런타임(uv, node)이 없으면 설치 안내를 출력하고 멈춘다. 종료 때는 꺼졌다는 안내를 출력하고 멈춘다.

### Git

- stage·commit·push·PR·merge는 사용자가 요청할 때만 한다. 승인은 그 요청에만 유효하고 다음 작업으로 이어지지 않는다. 이 파일의 프로젝트 구역이 명시한 상시 승인만 예외다.
- Git 규칙은 프로젝트 규칙이 대체하지 못한다. 프로젝트가 바꿀 수 있는 것은 이 구역이 "선언"이라고 적은 항목뿐이고, 프로젝트 구역에 한 줄씩 쓴다.
- 보호 브랜치는 `main`과 있으면 `develop`이다. 여기에는 직접 커밋·푸시하지 않고, 작업 브랜치(`feat/` `fix/` `docs/` `chore/` + kebab-case)에서 PR로 병합한다. 첫 커밋만 `main`에 직접 한다. 병렬 작업은 브랜치마다 `git worktree`로 분리한다. `.githooks/pre-commit`·`pre-push`가 막고(프로젝트 전용 푸시 검사는 훅을 고치지 말고 `.githooks/pre-push.local`에 둔다), `baseline/repo-settings.sh`가 GitHub의 병합 방식과 브랜치 보호를 맞춘다(비공개 저장소는 무료 플랜이라 훅만 작동한다).
- 선언 `main` 직행: 프로젝트 구역에 `<!-- ysm:direct-main -->` 줄을 쓰고 이유를 옆에 적으면 `main`에 직접 커밋·푸시할 수 있다. `develop`과 force push·삭제는 그대로 막힌다.
- 선언 커밋 제목 언어: 모든 프로젝트가 프로젝트 구역에 `<!-- ysm:commit-lang=ko -->` 또는 `<!-- ysm:commit-lang=en -->` 줄을 쓰고, `.githooks/commit-msg`가 검사한다.
- force push, 푸시한 이력의 수정, 브랜치·태그 강제 삭제, 공유 브랜치의 `reset --hard`는 사용자가 그 대화에서 직접 요청했을 때만 한다. 보호 브랜치와 태그에는 `YSM_ALLOW_FORCE=1`을 붙여야 훅을 통과한다. 이미 푸시한 작업 브랜치의 재작성은 `--force-with-lease`로만 한다. 푸시 전 로컬 커밋의 amend·fixup 정리는 이력 재작성이 아니다. `--no-verify`로 훅을 우회하지 않는다.
- 커밋은 의미·작업 단위 하나씩 만든다. 같은 목적의 후속 수정(오타·누락·리뷰 반영)은 새 커밋으로 쌓지 말고 푸시 전에 amend·fixup으로 합쳐 원격 이력을 깔끔하게 유지한다.
- `develop`은 아래 순서로 판단한다. 1) Cloudflare Pages로 배포하면 둔다. 2) 사용자가 요청하면 둔다. 3) 그 외에는 두지 않고 `main`만 쓴다. 버전·릴리스 여부는 기준이 아니다. `develop`을 두면 이유를 프로젝트 구역에 한 줄 쓰고, 작업 브랜치는 `develop`에서 따서 `develop`으로 PR하며(미리보기), `main`은 릴리스 PR(`develop` → `main`)로만 갱신한다(정식).
- 병합 방식: `develop`이 없으면 squash만 쓴다. 있으면 작업 브랜치 → `develop`은 squash, `develop` → `main` 릴리스는 merge commit이다. rebase 병합은 쓰지 않는다. squash 커밋의 메시지는 PR 제목과 본문이다. 병합 요청은 릴리스를 뜻하지 않는다.
- 릴리스 병합 커밋 때문에 `develop`이 `main`보다 뒤처져 보여도 맞추지 않는다. `main`→`develop` 동기화 PR은 병합 커밋을 다시 만들어 뒤처짐이 반복되므로 만들지 않는다. `main`에 `develop`을 거치지 않은 커밋이 생긴 경우에만 그 PR로 되병합한다.
- 병합과 릴리스는 별개다. 병합할 때마다 CHANGELOG의 `[Unreleased]`에 쌓아 두고, 버전은 사용자가 올리라고 지시한 시점에만 올린다. 릴리스 한 번에 매니페스트 버전·CHANGELOG·annotated 태그 `vX.Y.Z`·GitHub Release를 함께 만든다(스킬 `release-project`). 버전을 쓰지 않는 프로젝트에는 태그·CHANGELOG·Release를 두지 않는다.
- GitHub 기본 브랜치는 항상 `main`이다. `develop`을 쓰는 프로젝트도 `develop`은 기본 브랜치로 두지 않는다. Dependabot 설정은 기본 브랜치에 있어야 적용되므로 `develop`에서 바꾼 설정은 릴리스 뒤에 적용된다.
- 커밋 제목은 `<type>(<선택 scope>): <요약>`이다. type은 `feat` `fix` `perf` `refactor` `docs` `test` `build` `ci` `chore` `style` `revert`, 72자 이내, 끝에 마침표 없음. 형식과 언어는 `.githooks/commit-msg`가 검사한다. 이유가 필요하면 본문에 쓴다.
- 커밋 이메일은 GitHub noreply 주소를 쓴다(프로젝트에 설정되어 있으면 `.githooks/pre-commit`이 검사한다).
- PR 본문은 요약(무엇을·왜), 확인한 방법(실제 실행한 것만), 남은 결정 순서로 짧게 쓴다(`.github/pull_request_template.md`).
- 커밋 메시지와 PR 본문에 AI 생성 표식이나 co-author 트레일러(`Co-Authored-By: Claude …`, `🤖 Generated with …` 등)를 넣지 않는다. 커밋은 훅이, PR 본문 생성은 `.claude/settings.json`의 `attribution` 설정이 막고, PR을 만든 뒤 본문을 한 번 확인한다.
- 작업이 끝나 사용하지 않는 브랜치는 로컬·원격 모두 지운다(`main`·`develop` 같은 장기 브랜치 제외). GitHub의 "Automatically delete head branches"는 켜지 않는다. 푸시·병합까지 요청받은 작업은 로컬과 원격이 일치하고 미커밋 변경이 없는 상태로 마친다.
- 새 클론에서는 한 번 `git config core.hooksPath .githooks`를 실행한다(Claude Code는 세션 시작 때 자동 실행).

### 코드와 테스트

- 테스트는 중요한 동작과 회귀 위험만 둔다. 극단 상황, 다른 테스트와 중복, 구현 세부 확인은 쓰지 않고, 빨리 끝난다는 이유만으로 남기지 않는다.
- 기존 테스트도 검증한다. 단언이 없거나 항상 통과하는 것, 건너뛴 것, 불안정한 것을 찾고, 핵심 테스트는 대상 동작을 임시로 깨 보아 실패하는지 확인한다. 통과시키려고 기대값을 현재 결과에 맞추지 않는다.
- 코드를 바꾸면 쓰지 않게 된 코드·스타일·의존성·테스트를 같은 변경에서 지운다. 큰 변경이 끝나면 구조·중복·미사용을 점검한다(스킬 `tidy-code`).

### 의존성과 환경

- 의존성·가상환경·캐시·데이터·설정·로그는 프로젝트 폴더 안에만 두어, 폴더를 지우면 함께 사라지게 한다. 설치물과 결과물을 프로젝트 밖(스크래치패드, 전역, 에이전트 메모리)에 두지 않는다. 폴더 하나만 받아도 다른 컴퓨터에서 이어갈 수 있어야 한다. 폴더 밖 저장은 사용자가 명시적으로 고른 경우만 허용한다.
  - Node: `package.json`의 `devDependencies`와 `npx`.
  - Python: `uv`로 프로젝트 `.venv`만 쓴다(`uv sync`, `uv add`).
  - 런타임 버전은 `.node-version`, `.python-version`에만 고정한다. `mise.toml`은 두지 않는다.
  - Python 코드를 직접 짜지 않고 외부 Python 도구를 고정 버전으로 쓰기만 하면 위 Python 항목과 `.python-version` 대신 아래 계약을 쓴다.
    - 핀: 루트 `runtime-pins.json` 하나에 도구별 정확한 버전(범위 금지).
    - 격리: `UV_CACHE_DIR`·`UV_TOOL_DIR`·`UV_PYTHON_INSTALL_DIR`을 프로젝트 안 `.uv-cache`·`.uv-tools`·`.uv-python`으로 지정하고(`.gitignore`에 추가), 정의는 한 곳에만 둔다.
    - 복원: 한 명령으로 되게 하고 그 명령을 `AGENTS.md`에 적는다.
    - `pyproject.toml`, `.venv`, `.python-version`은 두지 않는다. 이 중 하나가 필요해지면(Python 코드를 직접 짜게 되면) 계약을 걷어내고 위 Python 항목으로 옮긴다.
- 활성 저장소는 Dependabot 보안 알림·보안 업데이트를 켜고, 버전 업데이트는 ysm-skills의 `baseline/dependabot.yml` 표준(매주 월요일, minor·patch 묶음, major 개별)을 복사해 쓴다. 보관했거나 1년 넘게 커밋이 없는 저장소는 설정 파일 없이 보안 알림만 둔다. Dependabot PR은 자동 병합하지 않고, 병합은 사용자가 요청할 때만 하며 병합 뒤 `dependabot/` 브랜치를 지운다.
- macOS와 Windows에서 같은 동작을 하도록 플랫폼 중립 경로와 LF 줄바꿈을 쓴다(`.cmd`만 CRLF).

### 안전

- 저장소는 언제 공개되어도 문제가 없는 상태로 유지한다. `.env`, 키, 토큰, 개인정보, 개인 절대경로, 개인 취향·운영 데이터, 실제 업무 문서를 저장소에 넣거나 읽어 출력하지 않는다. 환경변수 예시는 값이 없는 `.env.example`만 둔다.
- 비밀값으로 보이는 파일·문자열이 staged에 있으면 `.githooks/pre-commit`이 커밋을 막는다.
- GitHub Actions 워크플로는 `permissions: contents: read`로 시작하고 `pull_request_target`은 쓰지 않으며, 포크 PR 실행에 시크릿을 노출하지 않는다.
<!-- ysm-baseline:end -->
