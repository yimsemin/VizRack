#include "builtin/art_visualizer_engine.h"
#include "builtin/draw_list.h"
#include "builtin/oscilloscope_engine.h"
#include "core/audio_ring.h"
#include "core/channel_mapper.h"
#include "core/logger.h"
#include "core/plugin_paths.h"
#include "core/plugin_storage.h"
#include "core/plugin_state.h"
#include "core/portable_paths.h"
#include "core/reconnect_policy.h"
#include "core/settings.h"
#include "vst/plugin_catalog.h"

#include <windows.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expression)                                                                      \
    do {                                                                                       \
        if (!(expression)) {                                                                   \
            std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expression "\n"; \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

std::filesystem::path testDirectory() {
    wchar_t temporary[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temporary);
    const auto directory = std::filesystem::path(temporary) /
                           (L"VizRackTests-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    return directory;
}

void testPortablePaths() {
    const auto paths = vizrack::PortablePaths::fromExecutable(L"C:\\Portable\\VizRack\\VizRack.exe");
    CHECK(paths.root.filename() == L"VizRack");
    CHECK(paths.data == paths.root / L"data");
    CHECK(paths.plugins == paths.root / L"data" / L"plugins");
    CHECK(paths.settings == paths.root / L"data" / L"settings.json");
}

void testSettings(const std::filesystem::path& directory) {
    vizrack::Settings original;
    original.followDefaultDevice = false;
    original.fixedDeviceId = "device-{테스트}";
    original.selectedPluginId = "mvmeter2";
    original.windowX = -120;
    original.windowY = 44;
    original.windowWidth = 777;
    original.windowHeight = 555;
    original.alwaysOnTop = true;
    original.borderless = true;
    original.opacityPercent = 75;
    original.oscilloscopeFps = 30;
    original.oscilloscopeScalePercent = 50;
    original.oscilloscopeSmoothing = 2;
    original.oscilloscopeHistoryMode = true;
    original.artScene = 5;
    original.artPalette = 4;
    std::string error;
    const auto path = directory / L"settings.json";
    CHECK(vizrack::saveSettings(path, original, error));
    const auto loaded = vizrack::loadSettings(path);
    CHECK(loaded.loaded);
    CHECK(loaded.value.fixedDeviceId == original.fixedDeviceId);
    CHECK(loaded.value.selectedPluginId == original.selectedPluginId);
    CHECK(loaded.value.windowX == original.windowX);
    CHECK(loaded.value.windowWidth == original.windowWidth);
    CHECK(loaded.value.alwaysOnTop);
    CHECK(loaded.value.borderless);
    CHECK(loaded.value.opacityPercent == 75);
    CHECK(loaded.value.oscilloscopeFps == 30);
    CHECK(loaded.value.oscilloscopeScalePercent == 50);
    CHECK(loaded.value.oscilloscopeSmoothing == 2);
    CHECK(loaded.value.oscilloscopeHistoryMode);
    CHECK(loaded.value.artScene == 5);
    CHECK(loaded.value.artPalette == 4);
    {
        std::ifstream input(path, std::ios::binary);
        const std::string json((std::istreambuf_iterator<char>(input)), {});
        CHECK(json.find("\"schemaVersion\": 1") != std::string::npos);
    }

    const auto damaged = directory / L"damaged.json";
    {
        std::ofstream output(damaged, std::ios::binary);
        output << "not-json";
    }
    CHECK(!vizrack::loadSettings(damaged).loaded);

    const auto unsupported = directory / L"unsupported-settings.json";
    {
        std::ofstream output(unsupported, std::ios::binary);
        output << "{\"schemaVersion\":99}";
    }
    const auto unsupportedResult = vizrack::loadSettings(unsupported);
    CHECK(!unsupportedResult.loaded);
    CHECK(!unsupportedResult.warning.empty());
}

void testPluginCatalogAndStorage(const std::filesystem::path& directory) {
    const auto& catalog = vizrack::pluginCatalog();
    CHECK(!catalog.empty());
    std::set<std::string> ids;
    for (const auto& item : catalog) {
        CHECK(!item.id.empty());
        CHECK(ids.insert(item.id).second);
        if (item.kind == vizrack::PluginKind::vst3) CHECK(!item.installUrl.empty());
    }
    const auto* builtIn = vizrack::findPluginDefinition("builtin-oscilloscope");
    CHECK(builtIn != nullptr);
    CHECK(catalog.front().id == "builtin-oscilloscope");
    if (builtIn) {
        CHECK(builtIn->kind == vizrack::PluginKind::builtIn);
        CHECK(builtIn->displayName == "내장 오실로스코프");
        CHECK(builtIn->installUrl.empty());
        CHECK(builtIn->searchLocations.empty());
    }
    const auto* art = vizrack::findPluginDefinition("builtin-art-visualizer");
    CHECK(art != nullptr);
    if (art) {
        CHECK(art->kind == vizrack::PluginKind::builtIn);
        CHECK(art->displayName == "내장 아트 비주얼라이저");
        CHECK(art->installUrl.empty());
        CHECK(art->searchLocations.empty());
    }
    const auto* definition = vizrack::findPluginDefinition("mvmeter2");
    CHECK(definition != nullptr);
    if (!definition) return;
    CHECK(definition->displayName == "mvMeter2");
    CHECK(definition->kind == vizrack::PluginKind::vst3);
    CHECK(definition->vendorName == "TBProAudio");
    CHECK(definition->installUrl == "https://www.tbproaudio.de/products/mvmeter2");
    CHECK(definition->binaryMarker.empty());
    CHECK(definition->editionLabel == "GPU / noGPU");
    const auto* anspec = vizrack::findPluginDefinition("anspec");
    CHECK(anspec != nullptr);
    if (anspec) {
        CHECK(anspec->displayName == "AnSpec");
        CHECK(anspec->vendorName == "Voxengo");
        CHECK(anspec->classNameContains == "anspec");
        CHECK(anspec->vendorContains == "voxengo");
        CHECK(anspec->binaryMarker == "com.voxengo.audio-plugins.VST3.AnSpec");
        CHECK(anspec->searchLocations.size() == 1);
        CHECK(anspec->searchLocations.front() ==
              std::filesystem::path(L"VST3") / L"AnSpec.vst3");
        CHECK(anspec->installUrl == "https://www.voxengo.com/product/anspec/");
    }
    CHECK(vizrack::findPluginDefinition("not-whitelisted") == nullptr);

    const auto storage = vizrack::pluginStoragePaths(directory / L"plugins", definition->id);
    CHECK(storage.directory.filename() == L"mvmeter2");
    CHECK(storage.location.filename() == L"location.txt");
    CHECK(storage.state.filename() == L"plugin-state.bin");
    const std::filesystem::path module = L"C:\\VST3\\mvMeter2.vst3";
    std::string error;
    CHECK(vizrack::savePluginLocation(storage.location, module, error));
    std::string warning;
    CHECK(vizrack::loadPluginLocation(storage.location, warning) == "C:\\VST3\\mvMeter2.vst3");
    CHECK(warning.empty());
    bool rejectedUnsafeId = false;
    try {
        static_cast<void>(vizrack::pluginStoragePaths(directory, ".."));
    } catch (const std::invalid_argument&) {
        rejectedUnsafeId = true;
    }
    CHECK(rejectedUnsafeId);
}

void testChannels() {
    const auto stereo = vizrack::selectFrontLeftRight(2, 0x3);
    CHECK(stereo.valid);
    CHECK(!stereo.multichannel);
    CHECK(stereo.leftIndex == 0 && stereo.rightIndex == 1);

    const auto surround = vizrack::selectFrontLeftRight(8, 0x63f);
    CHECK(surround.valid);
    CHECK(surround.multichannel);
    CHECK(surround.leftIndex == 0 && surround.rightIndex == 1);
    CHECK(vizrack::channelStatusText(8).find("Front Left") != std::string::npos);
    CHECK(!vizrack::selectFrontLeftRight(1, 0x4).valid);
    CHECK(!vizrack::selectFrontLeftRight(2, 0xc).valid);
    CHECK(!vizrack::selectFrontLeftRight(2, 0x5).valid);
}

void testPluginPaths(const std::filesystem::path& directory) {
    const auto plugins = directory / L"plugins";
    const auto singleFile = plugins / L"mvMeter2.vst3";
    const auto bundle = plugins / L"Nested" / L"Meter.vst3";
    const auto bundleBinary = bundle / L"Contents" / L"x86_64-win" / L"Meter.vst3";
    std::filesystem::create_directories(bundleBinary.parent_path());
    {
        std::ofstream output(singleFile, std::ios::binary);
        output << "module";
    }
    {
        std::ofstream output(bundleBinary, std::ios::binary);
        output << "bundle";
    }
    CHECK(vizrack::isVst3CandidatePath(singleFile));
    CHECK(vizrack::isVst3CandidatePath(bundle));
    CHECK(!vizrack::isVst3CandidatePath(plugins / L"wrong.dll"));
    CHECK(vizrack::resolveVst3Binary(singleFile) == singleFile);
    CHECK(vizrack::resolveVst3Binary(bundle) == bundleBinary);
    const auto candidates = vizrack::discoverVst3Candidates(plugins);
    CHECK(candidates.size() == 2);
    CHECK(vizrack::discoverVst3Candidates(directory / L"missing").empty());
}

void testRing() {
    vizrack::StereoFrameRing ring(4);
    const std::array<float, 10> input{1, 11, 2, 12, 3, 13, 4, 14, 5, 15};
    CHECK(ring.capacity() == 4);
    CHECK(ring.pushInterleaved(input.data(), 5) == 4);
    CHECK(ring.droppedFrames() == 1);
    std::array<float, 3> left{};
    std::array<float, 3> right{};
    CHECK(ring.popPlanar(left.data(), right.data(), 3) == 3);
    CHECK(left[0] == 1 && left[2] == 3);
    CHECK(right[0] == 11 && right[2] == 13);
    CHECK(ring.pushInterleaved(input.data() + 8, 1) == 1);
    CHECK(ring.available() == 2);
    ring.discardOlderThan(1);
    CHECK(ring.available() == 1);
}

void checkDrawList(const vizrack::builtin::DrawList& drawList) {
    const auto points = drawList.points();
    for (const auto& point : points) {
        CHECK(std::isfinite(point.x));
        CHECK(std::isfinite(point.y));
    }
    for (const auto& command : drawList.commands()) {
        CHECK(std::isfinite(command.strokeWidth));
        CHECK(std::isfinite(command.x));
        CHECK(std::isfinite(command.y));
        CHECK(std::isfinite(command.width));
        CHECK(std::isfinite(command.height));
        CHECK(std::isfinite(command.x2));
        CHECK(std::isfinite(command.y2));
        CHECK(std::isfinite(command.startAngle));
        CHECK(std::isfinite(command.sweepAngle));
        if (command.primitive == vizrack::builtin::DrawPrimitive::polyline ||
            command.primitive == vizrack::builtin::DrawPrimitive::strokePolygon) {
            CHECK(command.points.offset <= points.size());
            if (command.points.offset <= points.size()) {
                CHECK(command.points.count <= points.size() - command.points.offset);
            }
            CHECK(command.points.count >= 2);
        }
    }
}

void testArtVisualizerCore() {
    using vizrack::builtin::ArtVisualizerEngine;
    using vizrack::builtin::DrawList;

    ArtVisualizerEngine engine;
    engine.setOptions({-100, 100});
    CHECK(engine.options().scene == 0);
    CHECK(engine.options().palette == ArtVisualizerEngine::kPaletteCount - 1);
    engine.setSampleRate(96000);

    auto left = engine.inputLeft();
    auto right = engine.inputRight();
    for (size_t index = 0; index < left.size(); ++index) {
        const float phase = static_cast<float>(index) * 0.03125f;
        left[index] = std::sin(phase) * 0.35f;
        right[index] = std::cos(phase * 1.07f) * 0.28f;
    }
    left[3] = std::numeric_limits<float>::quiet_NaN();
    right[7] = std::numeric_limits<float>::infinity();
    engine.update(ArtVisualizerEngine::kMaxSamples + 128, 1.0f / 60.0f);

    DrawList drawList;
    for (int scene = 0; scene < ArtVisualizerEngine::kSceneCount; ++scene) {
        CHECK(!ArtVisualizerEngine::sceneName(scene).empty());
        for (int palette = 0; palette < ArtVisualizerEngine::kPaletteCount; ++palette) {
            CHECK(!ArtVisualizerEngine::palette(palette).name.empty());
            engine.setOptions({scene, palette});
            engine.buildFrame(1920.0f, 1080.0f, drawList);
            CHECK(!drawList.commands().empty());
            CHECK(drawList.commands().front().primitive ==
                  vizrack::builtin::DrawPrimitive::verticalGradient);
            CHECK(drawList.commands().size() <= 1200);
            CHECK(drawList.points().size() <= 5000);
            checkDrawList(drawList);
            const auto info = engine.frameInfo();
            CHECK(info.scene == scene);
            CHECK(info.palette == palette);
            CHECK(std::isfinite(info.lowLevel) && info.lowLevel >= 0.0f && info.lowLevel <= 1.0f);
            CHECK(std::isfinite(info.midLevel) && info.midLevel >= 0.0f && info.midLevel <= 1.0f);
            CHECK(std::isfinite(info.highLevel) && info.highLevel >= 0.0f && info.highLevel <= 1.0f);
            CHECK(std::isfinite(info.stereoLevel) && info.stereoLevel >= 0.0f &&
                  info.stereoLevel <= 1.0f);
        }
    }

    const size_t commandCapacity = drawList.commandCapacity();
    const size_t pointCapacity = drawList.pointCapacity();
    for (int scene = 0; scene < ArtVisualizerEngine::kSceneCount; ++scene) {
        engine.setOptions({scene, 0});
        engine.update(0, 1.0f / 15.0f);
        engine.buildFrame(1920.0f, 1080.0f, drawList);
        CHECK(drawList.commandCapacity() == commandCapacity);
        CHECK(drawList.pointCapacity() == pointCapacity);
    }
    engine.buildFrame(0.0f, 1080.0f, drawList);
    CHECK(drawList.commands().empty());
    CHECK(drawList.points().empty());
    engine.update(0, std::numeric_limits<float>::quiet_NaN());
    engine.buildFrame(std::numeric_limits<float>::quiet_NaN(), 1080.0f, drawList);
    CHECK(drawList.commands().empty());
}

void testOscilloscopeCore() {
    using vizrack::builtin::DrawList;
    using vizrack::builtin::OscilloscopeEngine;

    OscilloscopeEngine engine;
    engine.setOptions({27, 83, -5, false});
    CHECK(engine.options().fps == 60);
    CHECK(engine.options().scalePercent == 70);
    CHECK(engine.options().smoothing == 0);
    engine.setSampleRate(96000);
    CHECK(engine.frameInfo().sampleRate == 96000);
    engine.setSampleRate(1);
    CHECK(engine.frameInfo().sampleRate == 96000);

    auto left = engine.inputLeft();
    auto right = engine.inputRight();
    for (size_t index = 0; index < left.size(); ++index) {
        const float phase = static_cast<float>(index) * 0.04f;
        left[index] = std::sin(phase) * 0.4f;
        right[index] = std::sin(phase * 0.93f + 0.4f) * 0.32f;
    }
    left[5] = std::numeric_limits<float>::quiet_NaN();
    right[9] = -std::numeric_limits<float>::infinity();
    engine.update(OscilloscopeEngine::kMaxSamples + 64);

    DrawList drawList;
    engine.buildFrame(4096.0f, 900.0f, drawList);
    CHECK(engine.frameInfo().hasSignalTrace);
    CHECK(!drawList.commands().empty());
    CHECK(drawList.points().size() <= OscilloscopeEngine::kMaxRenderedPoints * 2);
    checkDrawList(drawList);
    const size_t commandCapacity = drawList.commandCapacity();
    const size_t pointCapacity = drawList.pointCapacity();

    engine.setOptions({15, 100, 2, true});
    for (size_t frame = 0; frame < OscilloscopeEngine::kHistoryPoints + 20; ++frame) {
        engine.update(frame % 2 == 0 ? 128 : 0);
    }
    engine.buildFrame(4096.0f, 900.0f, drawList);
    CHECK(drawList.commandCapacity() == commandCapacity);
    CHECK(drawList.pointCapacity() == pointCapacity);
    CHECK(drawList.points().size() <= OscilloscopeEngine::kMaxRenderedPoints * 2);
    checkDrawList(drawList);

    engine.reset();
    engine.setOptions({60, 70, 1, false});
    engine.buildFrame(640.0f, 480.0f, drawList);
    CHECK(!engine.frameInfo().hasSignalTrace);
    CHECK(drawList.points().empty());
    CHECK(!drawList.commands().empty());
    engine.buildFrame(-1.0f, 480.0f, drawList);
    CHECK(drawList.commands().empty());
    engine.buildFrame(std::numeric_limits<float>::infinity(), 480.0f, drawList);
    CHECK(drawList.commands().empty());
}

void testReconnect() {
    vizrack::ReconnectPolicy policy;
    policy.start();
    CHECK(policy.state() == vizrack::CaptureState::connecting);
    CHECK(policy.failed().count() == 500);
    CHECK(policy.failed().count() == 1000);
    policy.deviceChanged();
    CHECK(policy.failureCount() == 0);
    policy.connected();
    CHECK(policy.state() == vizrack::CaptureState::running);
    policy.stop();
    CHECK(policy.state() == vizrack::CaptureState::stopped);
}

void testPluginState(const std::filesystem::path& directory) {
    vizrack::PluginStateData input;
    for (size_t i = 0; i < input.classId.size(); ++i) input.classId[i] = static_cast<uint8_t>(i);
    input.component = {1, 2, 3, 4, 5};
    input.controller = {9, 8, 7};
    auto bytes = vizrack::encodePluginState(input);
    vizrack::PluginStateData decoded;
    std::string error;
    CHECK(vizrack::decodePluginState(bytes, decoded, error));
    CHECK(decoded.classId == input.classId);
    CHECK(decoded.component == input.component);
    CHECK(decoded.controller == input.controller);
    bytes.back() ^= 0xff;
    CHECK(!vizrack::decodePluginState(bytes, decoded, error));

    const auto path = directory / L"plugin-state.bin";
    CHECK(vizrack::savePluginStateFile(path, input, error));
    CHECK(vizrack::loadPluginStateFile(path, decoded, error));
}

void testLoggerRotation(const std::filesystem::path& directory) {
    const auto logs = directory / L"logs";
    vizrack::Logger logger;
    CHECK(logger.open(logs, 120, 3));
    for (int index = 0; index < 10; ++index) {
        logger.info("rotation-test-message-" + std::to_string(index));
    }
    CHECK(std::filesystem::exists(logs / L"vizrack.log"));
    CHECK(std::filesystem::exists(logs / L"vizrack.1.log"));
}

} // namespace

int main() {
    const auto directory = testDirectory();
    testPortablePaths();
    testSettings(directory);
    testPluginCatalogAndStorage(directory);
    testChannels();
    testPluginPaths(directory);
    testRing();
    testArtVisualizerCore();
    testOscilloscopeCore();
    testReconnect();
    testPluginState(directory);
    testLoggerRotation(directory);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    if (failures == 0) {
        std::cout << "All tests passed\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed\n";
    return 1;
}
