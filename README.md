# VizRack

**음악은 그대로 흐르고, 화면만 조금 더 살아납니다.**

VizRack은 Windows에서 재생 중인 음악에 맞춰 움직이는 화면을 띄우는 가벼운 포터블
비주얼라이저입니다. 책상 한쪽이나 보조 모니터에 작은 창으로 올려두고, 음악을
들으면서 잠시 멍하니 바라볼 무언가가 필요할 때 사용해 보세요.

별도 오디오 드라이버나 외부 플러그인 없이 바로 시작할 수 있습니다. 사용 중인 음악
플레이어와 Windows의 재생 경로는 건드리지 않으므로 VizRack을 닫아도 음악은 그대로
이어집니다.

## 무엇을 볼 수 있나요?

- **아트 비주얼라이저** — 음악의 저·중·고역과 스테레오 움직임에 반응하는 6개 장면과
  6개 팔레트
- **오실로스코프** — 좌우 채널 파형과 최근 약 15초의 음량 흐름
- **선택형 외부 비주얼라이저** — mvMeter2와 AnSpec을 설치해 원하는 계측 화면으로 전환
- **감상에 맞춘 창 설정** — 항상 위, 테두리 숨김, 투명도와 15/30/60 FPS 조절

## 바로 시작하기

VizRack은 **Windows 10 버전 1703 이상 또는 Windows 11이 설치된 x64 PC**에서
실행됩니다.

1. `VizRack-win-x64.zip`을 원하는 로컬 폴더에 완전히 압축 해제합니다.
2. 압축을 푼 폴더에서 `VizRack.exe`를 실행합니다.
3. 평소처럼 음악이나 영상을 재생합니다.
4. `플러그인 > 내장 아트 비주얼라이저 > 사용`을 선택하고 화면을 클릭해 장면을
   바꿔 보세요.

설정을 저장하려면 ZIP 안에서 직접 실행하거나 `C:\Program Files` 같은 쓰기 제한
폴더에 두지 마세요.

코드 서명이 없어 Microsoft Defender SmartScreen 경고가 나타날 수 있습니다. 파일의
출처를 신뢰할 수 있을 때만 `추가 정보 > 실행`을 선택하세요.

## 자주 쓰는 조작

| 동작 | 결과 |
| --- | --- |
| 화면 클릭 또는 `Space` | 다음 아트 장면 |
| 화살표 또는 숫자 `1`~`6` | 장면 선택 |
| `C` | 다음 팔레트 |
| 오른쪽 클릭 | 현재 화면과 창 설정 |
| `F10` | 테두리를 숨긴 상태에서 메뉴 열기 |

`설정` 메뉴에서 항상 위, 테두리 숨김, 투명도와 출력 장치를 바꿀 수 있습니다.
기본값은 Windows의 기본 출력 장치를 자동으로 따라갑니다.

## 선택형 외부 비주얼라이저

<img src="docs/assets/VST_Compatible_Logo_Steinberg.svg" alt="VST Compatible" width="116">

VizRack의 VST® 3 지원은 아래 두 제품의 Windows x64 버전에만 한정됩니다. 임의의
VST 3 플러그인을 불러오는 범용 호스트가 아닙니다.

| 제품 | 지원 버전 | 공식 설치 페이지 |
| --- | --- | --- |
| mvMeter2 | x64 VST 3, GPU 또는 noGPU | [TBProAudio](https://www.tbproaudio.de/products/mvmeter2) |
| AnSpec | Windows x64 VST 3 | [Voxengo](https://www.voxengo.com/product/anspec/) |

필요한 제품을 설치한 뒤 `플러그인 > 제품 이름 > 자동 검색하여 사용`을 선택하세요.
찾지 못하면 같은 메뉴에서 VST 3 파일이나 번들 폴더를 직접 지정할 수 있습니다.
외부 플러그인은 VizRack 배포본에 포함되지 않습니다.

## 문제가 생겼을 때

| 문제 | 확인할 내용 |
| --- | --- |
| 화면이 음악에 반응하지 않음 | 음악이 실제로 재생 중인지 확인한 뒤 `설정 > 출력 장치`에서 올바른 장치를 선택합니다. |
| 설정이 저장되지 않음 | VizRack을 쓰기 가능한 로컬 폴더에 완전히 압축 해제해 실행합니다. |
| 외부 화면을 찾지 못함 | 플러그인의 Windows **64-bit VST 3** 버전을 설치했는지 확인하고 자동 검색 또는 직접 선택을 사용합니다. |
| mvMeter2 GPU 화면이 열리지 않음 | 그래픽 드라이버를 업데이트하거나 공식 noGPU 버전을 사용합니다. |
| 처음 상태로 되돌리고 싶음 | VizRack을 종료한 뒤 실행 파일 옆의 `data` 폴더 이름을 바꾸고 다시 실행합니다. |

문제가 계속되면 `data\logs\vizrack.log`를 확인하세요. 충돌 시에는
`data\crash-*.dmp`도 함께 생성될 수 있습니다.

## 포터블 저장과 오디오 처리

설정, 플러그인 위치와 로그는 모두 `VizRack.exe` 옆의 `data` 폴더에 저장됩니다.
AppData와 레지스트리는 사용하지 않으므로 폴더 전체를 옮기거나 삭제하기 쉽습니다.

VizRack은 Windows가 재생 중인 소리를 읽기만 하며 오디오 샘플을 저장하거나 로그에
기록하지 않습니다.

<details>
<summary>지원 범위와 제한</summary>

- 내장 아트 비주얼라이저는 감상용이며 정밀 계측 도구가 아닙니다.
- 다채널 출력에서는 Front Left/Front Right 채널을 사용합니다.
- 32비트, ARM64 네이티브, VST 2, AAX, ASIO와 WASAPI Exclusive는 지원하지 않습니다.
- DAW transport, MIDI, automation, 오디오 재출력 기능은 제공하지 않습니다.
- 가상 오디오 장치와 외부 플러그인의 동작은 환경에 따라 달라질 수 있습니다.

</details>

<details>
<summary>개발 및 패키징</summary>

Visual Studio Build Tools 2026의 **Desktop development with C++** 워크로드,
Windows SDK, CMake 4.2 이상과 Git 또는 로컬 Steinberg VST 3 SDK checkout이
필요합니다. Developer PowerShell에서 다음 명령을 실행합니다.

```powershell
cmake --preset vs2026-x64
cmake --build --preset release
ctest --preset release
out\build\vs2026-x64\Release\VizRack.exe --smoke-test
```

포터블 ZIP은 다음 명령으로 만듭니다.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

내장 비주얼라이저의 플랫폼 경계와 변경 규칙은
[`docs/BUILTIN_VISUALIZER_CORE.md`](docs/BUILTIN_VISUALIZER_CORE.md), 전체 구조는
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)를 참고하세요.

</details>

## 라이선스

VizRack의 코드와 자체 제작 앱 아이콘은
[MIT License](LICENSE), Copyright (c) 2026 SubProject로 공개됩니다. 빌드에 포함되는
Steinberg VST 3 SDK와 제3자 고지는 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)를
참고하세요.

공식 VST Compatible 로고와 VST 상표는 VizRack의 MIT License로 재허가되지 않으며
Steinberg의 VST 사용 지침에 따라 사용됩니다.

VST is a registered trademark of Steinberg Media Technologies GmbH.
