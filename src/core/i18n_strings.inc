// -*- mode: c++ -*-
// Single source of truth for every user-facing string in the Windows UI layer.
//
//   VIZRACK_STR(Id, "English", "한국어")
//
// The macro is expanded three times (enum, English table, Korean table), so a
// row that is missing a column does not compile. Add a row here and fill BOTH
// languages; never write a raw L"..." literal in a menu, dialog or overlay.
//
// "{}" markers are std::format placeholders filled by the caller. Percent signs
// are literal. Keep entries grouped by the screen they belong to.

// ── Application / generic ────────────────────────────────────────────────────
VIZRACK_STR(AppName,                    "VizRack", "VizRack")
VIZRACK_STR(DialogTitleUnhandledError,  "Unhandled error", "처리되지 않은 오류")
VIZRACK_STR(DialogTitleStartupFailed,   "VizRack failed to start", "VizRack 시작 실패")
VIZRACK_STR(MsgComInitFailed,           "COM initialization failed.", "COM 초기화에 실패했습니다.")
VIZRACK_STR(MsgUnknownError,            "An unknown error occurred.", "알 수 없는 오류가 발생했습니다.")

// ── Capture state (shown in the window title) ────────────────────────────────
VIZRACK_STR(CaptureStopped,       "Stopped", "중지")
VIZRACK_STR(CaptureDisconnected,  "Disconnected", "연결 끊김")
VIZRACK_STR(CaptureConnecting,    "Connecting", "연결 중")
VIZRACK_STR(CaptureRunning,       "Capturing", "캡처 중")
VIZRACK_STR(CaptureBackoff,       "Waiting to reconnect", "재연결 대기")
VIZRACK_STR(CaptureUnknown,       "Unknown", "알 수 없음")

// ── Window title ────────────────────────────────────────────────────────────
VIZRACK_STR(TitleCheckingDevice,   "Checking output device", "출력 장치 확인 중")
VIZRACK_STR(TitleFrontLrMetering,  "Front L/R metering", "Front L/R 측정")

// ── Menu bar ────────────────────────────────────────────────────────────────
VIZRACK_STR(MenuSettings,          "Settings", "설정")
VIZRACK_STR(MenuHelp,              "Help", "도움말")
VIZRACK_STR(MenuPluginBarFmt,      "Plug-in: {}", "플러그인: {}")
VIZRACK_STR(PluginStatusSearching, "searching", "찾는 중")

// ── Settings menu ───────────────────────────────────────────────────────────
VIZRACK_STR(MenuOutputDevice,      "Output device", "출력 장치")
VIZRACK_STR(MenuFollowDefault,     "Follow the Windows default output", "Windows 기본 출력 자동 추적")
VIZRACK_STR(MenuAlwaysOnTop,       "Always on top", "항상 위")
VIZRACK_STR(MenuBorderless,        "Hide window border", "창 테두리 숨김")
VIZRACK_STR(MenuOpacity,           "Opacity", "투명도")
VIZRACK_STR(MenuLanguage,          "Language", "언어")
VIZRACK_STR(MenuLanguageAuto,      "Automatic", "자동")
VIZRACK_STR(MenuLanguageEnglish,   "English", "English")
VIZRACK_STR(MenuLanguageKorean,    "한국어", "한국어")
VIZRACK_STR(MenuExit,              "Exit", "종료")
VIZRACK_STR(DeviceDefaultSuffix,   " (default)", " (기본)")

// ── Plug-in menu ────────────────────────────────────────────────────────────
VIZRACK_STR(MenuPluginUse,           "Use", "사용")
VIZRACK_STR(MenuPluginUseAutoDetect, "Auto-detect and use", "자동 검색하여 사용")
VIZRACK_STR(MenuPluginPickFile,      "Choose VST3 file...", "VST3 파일 선택...")
VIZRACK_STR(MenuPluginPickFolder,    "Choose VST3 bundle folder...", "VST3 번들 폴더 선택...")
VIZRACK_STR(MenuPluginInstallPage,   "Open the official install page...", "공식 설치 페이지 열기...")
VIZRACK_STR(MenuHelpGithub,          "Open the GitHub repository", "GitHub 저장소 열기")
VIZRACK_STR(MenuAbout,               "About VizRack", "VizRack 정보")
VIZRACK_STR(MenuCredits,             "Credits", "크레딧")

// ── Built-in plug-in display names ──────────────────────────────────────────
VIZRACK_STR(PluginNameOscilloscope,  "Built-in Oscilloscope", "내장 오실로스코프")
VIZRACK_STR(PluginNameArtVisualizer, "Built-in Art Visualizer", "내장 아트 비주얼라이저")
VIZRACK_STR(PluginNameCampfire,      "Built-in Campfire", "내장 캠프파이어")
VIZRACK_STR(PluginNameSpectrum3d,    "Built-in 3D Spectrum", "내장 3D 스펙트럼")
VIZRACK_STR(PluginNameJoyDivision,   "Built-in Joy Division", "내장 Joy Division")

// ── VST3 file picker ────────────────────────────────────────────────────────
VIZRACK_STR(PickerFolderTitleFmt,  "Select the {} .vst3 bundle folder", "{} .vst3 번들 폴더 선택")
VIZRACK_STR(PickerFileTitleFmt,    "Select {} VST3", "{} VST3 선택")
VIZRACK_STR(PickerFilterVst3,      "VST3 module (*.vst3)", "VST3 모듈 (*.vst3)")
VIZRACK_STR(PickerFilterAllFiles,  "All files", "모든 파일")

// ── About / Credits dialogs ─────────────────────────────────────────────────
VIZRACK_STR(AboutDescription,
    "A lightweight portable visualizer that reacts to whatever is playing on Windows.\n"
    "It only reads sound through WASAPI shared loopback and never touches the playback path.",
    "Windows에서 재생 중인 음악에 반응하는 가벼운 포터블 비주얼라이저입니다.\n"
    "WASAPI 공유 loopback으로 소리를 읽기만 하며 재생 경로에 개입하지 않습니다.")
VIZRACK_STR(CreditsIntro,
    "Some built-in visualizers take after a designer, artist or era. The names and works\n"
    "below stay with their owners and are not covered by VizRack's MIT License.",
    "일부 내장 비주얼라이저는 특정 디자이너·아티스트·시대를 모티브로 합니다. 아래의 이름과\n"
    "작품은 각 권리자에게 귀속되며 VizRack의 MIT License 적용 대상이 아닙니다.")

// ── MessageBox: opening a browser / URL ─────────────────────────────────────
VIZRACK_STR(DialogTitleBrowserFailed,     "Could not open browser", "브라우저 열기 실패")
VIZRACK_STR(MsgInstallPageOpenFailed,     "Could not open the official install page.", "공식 설치 페이지를 열지 못했습니다.")
VIZRACK_STR(MsgDefaultBrowserOpenFailed,  "Could not open the default browser.", "기본 브라우저를 열지 못했습니다.")

// ── MessageBox: plug-in errors (titles only; detail text stays English) ─────
VIZRACK_STR(DialogTitlePortableStorage,     "Portable storage is not writable", "포터블 저장소 쓰기 불가")
VIZRACK_STR(DialogTitleBuiltinStartFailed,  "Built-in visualizer failed to start", "내장 플러그인 시작 실패")
VIZRACK_STR(DialogTitleVst3LoadFailed,      "VST3 load failed", "VST3 로딩 실패")
VIZRACK_STR(DialogTitleSupportedPluginMissing, "Supported plug-in not found", "지원 플러그인을 찾지 못함")
VIZRACK_STR(DialogTitleIncompatibleVst3,    "Incompatible VST3", "호환되지 않는 VST3")
VIZRACK_STR(StatusPortableReadOnly,
    "Portable data folder not writable — settings and logs are not saved",
    "포터블 데이터 폴더 쓰기 불가 — 설정과 로그를 저장하지 않음")

// ── "Supported plug-in required" dialog ─────────────────────────────────────
VIZRACK_STR(DialogTitlePluginRequired, "Supported plug-in required", "지원 플러그인 필요")
VIZRACK_STR(DialogBodyPluginRequiredFmt,
    "{} was not found. Install a Windows x64 VST3 plug-in first.\n\n"
    "[Yes] Open the official install page\n"
    "[No] Pick a .vst3 file you already installed\n"
    "[Cancel] Choose later from the Plug-in menu",
    "{}을(를) 찾지 못했습니다. 먼저 Windows x64 VST3 플러그인을 설치해야 합니다.\n\n"
    "[예] 공식 설치 페이지 열기\n"
    "[아니요] 이미 설치한 .vst3 파일 직접 선택\n"
    "[취소] 나중에 플러그인 메뉴에서 선택")

// ── Oscilloscope view ──────────────────────────────────────────────────────
VIZRACK_STR(ScopeTitleHistory,   "Built-in Oscilloscope · Time history", "내장 오실로스코프 · 시간 히스토리")
VIZRACK_STR(ScopeTitleWaveform,  "Built-in Oscilloscope · Instant waveform", "내장 오실로스코프 · 순간 파형")
VIZRACK_STR(ScopeHintRightClick, "Right-click: display settings", "우클릭: 표시 설정")
VIZRACK_STR(ScopePlaySomeAudio,  "Try playing some system audio", "시스템 소리를 재생해 보세요")
VIZRACK_STR(ScopeMenuModeWaveform,  "Instant waveform", "순간 파형")
VIZRACK_STR(ScopeMenuModeHistory,   "Time history (~15 s)", "시간 히스토리 (약 15초)")
VIZRACK_STR(ScopeMenuScaleSmall,    "Small (50%)", "작게 (50%)")
VIZRACK_STR(ScopeMenuScaleNormal,   "Normal (70%)", "보통 (70%)")
VIZRACK_STR(ScopeMenuScaleLarge,    "Large (100%)", "크게 (100%)")
VIZRACK_STR(ScopeMenuSmoothingOff,    "Off", "끔")
VIZRACK_STR(ScopeMenuSmoothingLight,  "Light", "약하게")
VIZRACK_STR(ScopeMenuSmoothingStrong, "Strong", "강하게")
VIZRACK_STR(ScopeMenuDisplayMode,   "Display mode", "표시 방식")
VIZRACK_STR(ScopeMenuWaveformSize,  "Waveform size", "파형 크기")
VIZRACK_STR(ScopeMenuLineStability, "Line stabilization", "선 안정화")
VIZRACK_STR(ScopeMenuFrameRate,     "Frame rate", "프레임 속도")

// ── Campfire view ──────────────────────────────────────────────────────────
VIZRACK_STR(CampfireMenuFlameResponse,  "Flame audio reaction", "불꽃 음악 반응")
VIZRACK_STR(CampfireMenuStarSpeed,      "Star drift speed", "별 이동 속도")
VIZRACK_STR(CampfireMenuStarBrightness, "Star base brightness", "별 평소 밝기")
VIZRACK_STR(CampfireMenuStarResponse,   "Star audio reaction", "별 음악 반응")
VIZRACK_STR(CampfireMenuSparkAmount,    "Spark amount", "불티 양")
VIZRACK_STR(CampfireMenuSparkIntensity, "Spark intensity", "불티 강도")

// ── Art visualizer view ────────────────────────────────────────────────────
VIZRACK_STR(ArtMenuScene,   "Scene", "장면")
VIZRACK_STR(ArtMenuPalette, "Color palette", "색상 팔레트")

// ── 3D spectrum view ───────────────────────────────────────────────────────
VIZRACK_STR(SpectrumMenuPalette,  "Color palette", "색상 팔레트")
VIZRACK_STR(SpectrumMenuRotation, "Rotation (quarter view)", "회전 (쿼터뷰)")
VIZRACK_STR(SpectrumMenuTilt,     "Tilt / perspective", "기울기 / 원근")
VIZRACK_STR(SpectrumMenuDepth,    "Time depth", "시간 깊이")
VIZRACK_STR(SpectrumMenuHeight,   "Height", "높이")
