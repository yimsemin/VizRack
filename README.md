# VizRack

VizRack은 Windows에서 재생 중인 소리를 읽어 내장 시각화 또는 지원 대상으로 지정된
VST® 3 플러그인에 전달하고,
그 화면을 독립 창으로 보여 주는 포터블 앱입니다. 원래 소리를 다시 출력하거나
Windows의 재생 경로를 바꾸지 않으므로 VizRack을 종료해도 시스템 재생은 계속됩니다.

<img src="docs/assets/VST_Compatible_Logo_Steinberg.svg" alt="VST Compatible" width="116">

VST® 3 호환 표시는 아래 표의 지원 대상으로 지정된 플러그인에 한정됩니다. VizRack은
임의의 VST 3 플러그인을 불러오는 범용 호스트가 아닙니다.

## 처음 사용하는 사람을 위한 빠른 시작

### 준비물

- Windows 10 버전 1703 이상 또는 Windows 11이 설치된 x64 PC
- 압축을 풀 약간의 저장 공간

VizRack 자체를 실행하기 위해 Visual Studio, CMake, 별도 오디오 드라이버 또는
VC++ 재배포 패키지를 설치할 필요는 없습니다. 내장 오실로스코프와 아트
비주얼라이저가 포함되어 있어 외부 플러그인을 하나도 설치하지 않아도 바로 동작을
확인할 수 있습니다.

### 설치하고 실행하기

1. 받은 `VizRack-win-x64.zip`을 마우스 오른쪽 버튼으로 누르고 `속성`을 엽니다.
   아래쪽에 `차단 해제`가 보이면, 파일을 보낸 사람을 신뢰할 수 있을 때만 체크한 뒤
   `확인`을 누릅니다.
2. ZIP 안에서 EXE를 바로 실행하지 말고 `압축 풀기` 또는 `모두 추출`을 사용합니다.
   예를 들어 `문서\VizRack`처럼 일반 사용자가 파일을 쓸 수 있는 폴더가 좋습니다.
   `C:\Program Files`나 읽기 전용·네트워크 폴더는 피하세요.
3. 압축을 푼 폴더의 `VizRack.exe`를 실행합니다. 최초 실행에는 `내장 오실로스코프`가
   자동으로 선택됩니다.
4. 음악이나 영상을 재생합니다. 제목 표시줄에 출력 장치 이름이 보이고 초록색 L 파형과
   파란색 R 파형이 움직이면 정상입니다.
   화면을 오른쪽 클릭하면 파형 크기, 선 안정화, 15/30/60 FPS와 최근 약 15초의
   `시간 히스토리`를 선택할 수 있습니다.
5. 좀 더 감상용에 가까운 화면은
   `플러그인 > 내장 아트 비주얼라이저 > 사용`을 선택합니다. 화면을 클릭하거나
   `Space`를 누르면 여섯 장면이 바뀝니다. 화살표나 숫자 `1`~`6`으로 이동하고,
   오른쪽 클릭으로 장면·팔레트를 고르거나 `C`로 다음 팔레트를 선택할 수 있습니다.
6. 다른 화면이 필요할 때만 아래 표의 mvMeter2 또는 AnSpec x64 VST3를 설치한 뒤
   `플러그인 > 제품 이름 > 자동 검색하여 사용`을 누릅니다.

코드 서명이 없기 때문에 처음 실행할 때 Microsoft Defender SmartScreen 경고가
나타날 수 있습니다. 파일을 보낸 사람과 출처를 확인할 수 있을 때만 `추가 정보`에서
실행을 선택하세요.

## 기본 사용법

- 기본값은 `설정 > 출력 장치 > Windows 기본 출력 자동 추적`입니다. 특정 장치만
  계속 보려면 같은 메뉴에서 장치 이름을 선택합니다.
- `플러그인` 메뉴에서 내장 오실로스코프, 내장 아트 비주얼라이저, mvMeter2,
  AnSpec을 같은 방식으로 전환합니다.
- `설정`에서 항상 위, 창 테두리 숨김, 투명도를 바꿀 수 있습니다.
- 테두리를 숨긴 뒤에는 창 위쪽 24px 영역을 드래그해 이동합니다. 우클릭 또는
  `F10`으로 설정 메뉴를 다시 열 수 있습니다.
- 창의 `X`를 누르면 오디오 캡처와 플러그인을 정리하고 완전히 종료합니다.

## 문제가 생겼을 때

### 플러그인을 찾지 못함

설치 프로그램에서 **64-bit VST3**를 선택했는지 확인한 뒤
`플러그인 > 제품 이름 > 자동 검색하여 사용`을 다시 누르세요. mvMeter2의 GPU판과
noGPU판은 둘 다 지원합니다. 다른 위치에 설치했다면 같은 메뉴의
`VST3 파일 선택...` 또는 `VST3 번들 폴더 선택...`을 사용합니다.

### 내장 화면이 소리에 반응하지 않음

먼저 Windows에서 실제로 소리가 재생 중인지 확인하고, 제목 표시줄에 `캡처 중`이
아닌 상태가 표시되면 `설정 > 출력 장치`에서 올바른 장치를 선택하세요. 메뉴에서
`플러그인 > 내장 오실로스코프 > 사용` 또는
`플러그인 > 내장 아트 비주얼라이저 > 사용`을 다시 선택해 볼 수도 있습니다. 아트
비주얼라이저의 배경 움직임은 무음에도 계속되지만 파형과 색 띠의 크기는 소리에 따라
달라집니다.

### 외부 VST3 화면이 열리지 않음

mvMeter2 GPU판 화면 자체가 열리지 않으면 그래픽 드라이버를 업데이트하거나 공식
noGPU판을 설치해 보세요. 즉시 원인을 나누어 확인하려면 내장 오실로스코프 또는
AnSpec으로 전환할 수도 있습니다.

### 설정이 저장되지 않음

ZIP 안에서 직접 실행했거나 실행 폴더에 쓰기 권한이 없는 경우입니다. 앱을 종료하고
전체 파일을 쓰기 가능한 로컬 폴더에 다시 압축 해제하세요. 정상이라면 실행 파일 옆에
`data` 폴더가 자동으로 생깁니다.

### 다시 처음 상태로 시작하고 싶음

VizRack을 종료한 뒤 `data` 폴더의 이름을 바꾸고 다시 실행하세요. 새 `data` 폴더가
만들리며 기존 설정과 플러그인 화면 설정은 이름을 바꾼 폴더에 보존됩니다.

문제가 계속되면 `data\logs\vizrack.log`를 앱을 전달한 사람에게 보내세요. 충돌한
경우에는 `data\crash-*.dmp`도 함께 생길 수 있습니다. 오디오 샘플 자체는 로그에
기록하지 않습니다.

## 지원 플러그인과 검색 위치

| 제품 | 종류와 필요한 버전 | 제품별 검색 하위 경로 | 공식 설치 페이지 |
| --- | --- | --- | --- |
| 내장 오실로스코프 | EXE에 포함, 설치 불필요 | 해당 없음 | 해당 없음 |
| 내장 아트 비주얼라이저 | EXE에 포함, 6개 장면·6개 팔레트 | 해당 없음 | 해당 없음 |
| mvMeter2 | Windows x64 VST3, GPU 또는 noGPU | `VST3\TBProAudio` | [TBProAudio](https://www.tbproaudio.de/products/mvmeter2) |
| AnSpec | Windows x64 VST3 | `VST3\AnSpec.vst3` | [Voxengo](https://www.voxengo.com/product/anspec/) |

외부 VST3를 선택하면 앱은 마지막으로 정상 사용한 경로를 먼저 확인한 다음 아래의
표준 위치를 순서대로 검사합니다.

1. `%LOCALAPPDATA%\Programs\Common\VST3` (현재 사용자)
2. `C:\Program Files\Common Files\VST3` (모든 사용자)
3. `VizRack.exe`가 있는 폴더의 `VST3` (앱 전용)

전체 VST3 폴더를 무차별 스캔하지 않고, 선택한 제품의 위 표에 적힌 파일이나 하위
폴더만 검사합니다. 화이트리스트에 없는 플러그인은 수동으로 골라도 로드하지 않습니다.

## 포터블 데이터

모든 경로는 현재 작업 폴더가 아니라 `VizRack.exe`의 위치를 기준으로 계산합니다.
AppData나 레지스트리에는 VizRack 설정을 저장하지 않습니다.

```text
VizRack/
├─ VizRack.exe
├─ README.md
├─ LICENSE
├─ THIRD_PARTY_NOTICES.md
├─ licenses/
├─ docs/
│  ├─ ARCHITECTURE.md
│  ├─ BUILTIN_VISUALIZER_CORE.md
│  └─ assets/
│     └─ VST_Compatible_Logo_Steinberg.svg
└─ data/
   ├─ settings.json
   ├─ plugins/
   │  └─ <plugin-id>/
   │     ├─ location.txt
   │     ├─ plugin-state.bin
   │     └─ plugin-state.corrupt.bin  (복구 시에만)
   ├─ crash-*.dmp                     (충돌 시에만)
   └─ logs/
      ├─ vizrack.log
      └─ vizrack.N.log
```

설정과 외부 VST3 상태는 임시 파일에 먼저 쓴 뒤 교체합니다. 손상되거나 현재
플러그인과 맞지 않는 상태는 격리하고 기본값으로 시작합니다. 외부 플러그인 GUI의
설정은 플러그인을 전환하거나 정상 종료할 때 저장되므로 강제 종료하면 유실될 수 있습니다.

## 기능 범위와 제한

- WASAPI Shared Mode Loopback으로 시스템 출력의 복사본만 읽습니다.
- 내장 오실로스코프는 최신 4,096개 샘플의 L/R 파형 또는 최근 약 15초의 음량
  히스토리를 표시합니다.
- 내장 아트 비주얼라이저는 같은 샘플에서 저·중·고역 에너지와 스테레오 폭을 가볍게
  추출해 여섯 가지 감상용 장면과 여섯 팔레트로 표시합니다. 정밀 계측 용도는 아닙니다.
- 스테레오는 Left/Right, 다채널은 Front Left/Front Right만 측정합니다.
- 32비트, ARM64 네이티브, VST2, AAX, ASIO, WASAPI Exclusive는 지원하지 않습니다.
- 모노 출력, 임의 다운믹스, DAW transport, MIDI, automation, 오디오 재출력은
  지원하지 않습니다.
- 가상 오디오 장치는 일반 출력 장치처럼 동작할 수 있지만 호환성을 보장하지 않습니다.
- 플러그인 자체나 GPU 드라이버의 오류는 로그와 crash dump를 함께 확인해야 합니다.

## 개발자를 위한 빌드와 테스트

이 절은 소스 코드를 빌드할 때만 필요합니다. 앱을 받아 실행하는 사용자는 건너뛰어도
됩니다.

필요 항목:

- Visual Studio Build Tools 2026의 **Desktop development with C++** 워크로드
- Windows 10 또는 11 SDK
- CMake 4.2 이상
- Git 또는 로컬 Steinberg VST3 SDK checkout

Developer PowerShell 또는 Developer Command Prompt for VS 2026에서 실행합니다.

```powershell
cmake --preset vs2026-x64
cmake --build --preset release
ctest --preset release
```

프리셋은 C++ 툴셋 버전을 별도로 고정하지 않으며, 설치된 Visual Studio 2026의
기본 툴셋을 사용합니다.

실행 파일은 `out\build\vs2026-x64\Release\VizRack.exe`입니다. SDK는 다음 순서로
결정합니다.

1. `-DVIZRACK_VST3_SDK_PATH=...`로 지정한 checkout
2. `external\vst3sdk` 로컬 checkout
3. `FetchContent`가 받는 Steinberg `v3.8.0_build_66`

`resources\app-icon.png`는 `app.ico`를 다시 만들 때 사용하는 고해상도 원본입니다.
`external\vst3sdk`와 빌드 산출물은 커밋하지 않습니다. Windows 타깃은 정적 MSVC
runtime(`/MT`)을 사용합니다. 자동 테스트는 포터블 저장소, 오디오 전달, 재연결,
VST 상태 무결성과 내장 렌더 명령·버퍼 재사용을 검사합니다.

내장 분석·장면의 단일 원본은 플랫폼 중립 `vizrack_builtin_core`입니다. 경계와 변경
규칙은 [`docs/BUILTIN_VISUALIZER_CORE.md`](docs/BUILTIN_VISUALIZER_CORE.md), 전체 설계는
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)를 참고하세요.

다음 명령은 약 4초 뒤 앱을 자동 종료합니다. 새 데이터 폴더에서는 내장
오실로스코프와 오디오 초기화를 검사하므로 외부 VST3가 필요하지 않습니다.

```powershell
out\build\vs2026-x64\Release\VizRack.exe --smoke-test
```

릴리스 전에는 실제 Windows 10/11 x64 장치에서 플러그인 선택, 신호 반응,
USB·Bluetooth·HDMI 장치 변경, 절전 복귀, DPI 100/125/150%, 다중 모니터와 정상·강제
종료 후 시스템 재생 유지를 수동 확인하세요.

## 패키징

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

결과는 `out\package\VizRack\`과 `out\package\VizRack-win-x64.zip`입니다. 패키지에는
EXE, README, 설계 문서, VST Compatible 로고와 라이선스만 들어갑니다. 두 내장
플러그인은 EXE에 포함되며 `data`, PDB, SDK와 외부 플러그인 바이너리는 포함하지
않습니다.

## 라이선스

VizRack 자체 소스 코드는 [MIT License](LICENSE)로 공개됩니다. 빌드할 때 일부
Steinberg VST 3 SDK 코드가 `VizRack.exe`에 정적으로 포함되므로, 바이너리 배포본에는
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)와
[`licenses/Steinberg-VST3-SDK-MIT.txt`](licenses/Steinberg-VST3-SDK-MIT.txt)를 함께
유지해야 합니다. mvMeter2와 AnSpec의 바이너리·설치 프로그램·아트워크는 이 저장소와
배포본에 포함되지 않으며 각 제품의 라이선스는 사용자가 별도로 설치할 때 적용됩니다.
공식 VST Compatible 로고와 VST 상표는 VizRack의 MIT License로 재허가되는 자산이
아니며, Steinberg의 VST 사용 지침에 따라 사용됩니다.

VST is a registered trademark of Steinberg Media Technologies GmbH.
