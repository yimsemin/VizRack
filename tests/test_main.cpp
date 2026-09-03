#include "builtin/art_visualizer_engine.h"
#include "builtin/campfire_engine.h"
#include "builtin/draw_list.h"
#include "builtin/oscilloscope_engine.h"
#include "builtin/spectrum3d_engine.h"
#include "core/audio_ring.h"
#include "core/channel_mapper.h"
#include "core/i18n.h"
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
    original.uiLanguage = "ko";
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
    original.campfireFlameResponse = 30;
    original.campfireStarSpeed = 70;
    original.campfireStarBrightness = 40;
    original.campfireStarResponse = 80;
    original.campfireParticleAmount = 90;
    original.campfireParticleIntensity = 60;
    original.spectrum3dPalette = 3;
    original.spectrum3dRotation = 70;
    original.spectrum3dTilt = 30;
    original.spectrum3dDepth = 90;
    original.spectrum3dHeight = 20;
    original.joyDivisionPalette = 5;
    original.joyDivisionRotation = 10;
    original.joyDivisionTilt = 80;
    original.joyDivisionDepth = 40;
    original.joyDivisionHeight = 100;
    std::string error;
    const auto path = directory / L"settings.json";
    CHECK(vizrack::saveSettings(path, original, error));
    const auto loaded = vizrack::loadSettings(path);
    CHECK(loaded.loaded);
    CHECK(loaded.value.fixedDeviceId == original.fixedDeviceId);
    CHECK(loaded.value.selectedPluginId == original.selectedPluginId);
    CHECK(loaded.value.uiLanguage == "ko");
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
    CHECK(loaded.value.campfireFlameResponse == 30);
    CHECK(loaded.value.campfireStarSpeed == 70);
    CHECK(loaded.value.campfireStarBrightness == 40);
    CHECK(loaded.value.campfireStarResponse == 80);
    CHECK(loaded.value.campfireParticleAmount == 90);
    CHECK(loaded.value.campfireParticleIntensity == 60);
    CHECK(loaded.value.spectrum3dPalette == 3);
    CHECK(loaded.value.spectrum3dRotation == 70);
    CHECK(loaded.value.spectrum3dTilt == 30);
    CHECK(loaded.value.spectrum3dDepth == 90);
    CHECK(loaded.value.spectrum3dHeight == 20);
    CHECK(loaded.value.joyDivisionPalette == 5);
    CHECK(loaded.value.joyDivisionRotation == 10);
    CHECK(loaded.value.joyDivisionTilt == 80);
    CHECK(loaded.value.joyDivisionDepth == 40);
    CHECK(loaded.value.joyDivisionHeight == 100);
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

    const auto badLanguage = directory / L"bad-language.json";
    {
        std::ofstream output(badLanguage, std::ios::binary);
        output << "{\"schemaVersion\":1,\"uiLanguage\":\"xx\"}";
    }
    const auto badLanguageResult = vizrack::loadSettings(badLanguage);
    CHECK(badLanguageResult.loaded);
    CHECK(badLanguageResult.value.uiLanguage == "auto");
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
        CHECK(builtIn->displayName == "Built-in Oscilloscope");
        CHECK(builtIn->installUrl.empty());
        CHECK(builtIn->searchLocations.empty());
    }
    const auto* art = vizrack::findPluginDefinition("builtin-art-visualizer");
    CHECK(art != nullptr);
    if (art) {
        CHECK(art->kind == vizrack::PluginKind::builtIn);
        CHECK(art->displayName == "Built-in Art Visualizer");
        CHECK(art->installUrl.empty());
        CHECK(art->searchLocations.empty());
    }
    const auto* campfire = vizrack::findPluginDefinition("builtin-campfire");
    CHECK(campfire != nullptr);
    if (campfire) {
        CHECK(campfire->kind == vizrack::PluginKind::builtIn);
        CHECK(campfire->displayName == "Built-in Campfire");
        CHECK(campfire->editionLabel == "Natural Flame / Audio Reactive");
        CHECK(campfire->installUrl.empty());
        CHECK(campfire->searchLocations.empty());
    }
    const auto* spectrum3d = vizrack::findPluginDefinition("builtin-spectrum3d");
    CHECK(spectrum3d != nullptr);
    if (spectrum3d) {
        CHECK(spectrum3d->kind == vizrack::PluginKind::builtIn);
        CHECK(spectrum3d->displayName == "Built-in 3D Spectrum");
        CHECK(spectrum3d->installUrl.empty());
        CHECK(spectrum3d->searchLocations.empty());
    }
    const auto* joyDivision = vizrack::findPluginDefinition("builtin-joydivision");
    CHECK(joyDivision != nullptr);
    if (joyDivision) {
        CHECK(joyDivision->kind == vizrack::PluginKind::builtIn);
        CHECK(joyDivision->displayName == "Built-in Joy Division");
        CHECK(joyDivision->inspiration == "Inspired by Joy Division");
        CHECK(joyDivision->installUrl.empty());
        CHECK(joyDivision->searchLocations.empty());
    }
    for (const auto& item : catalog) {
        if (item.id != "builtin-joydivision") CHECK(item.inspiration.empty());
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
            command.primitive == vizrack::builtin::DrawPrimitive::fillPolygon ||
            command.primitive == vizrack::builtin::DrawPrimitive::strokePolygon) {
            CHECK(command.points.offset <= points.size());
            if (command.points.offset <= points.size()) {
                CHECK(command.points.count <= points.size() - command.points.offset);
            }
            const uint32_t minimum =
                command.primitive == vizrack::builtin::DrawPrimitive::fillPolygon ? 3u : 2u;
            CHECK(command.points.count >= minimum);
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

void testCampfireCore() {
    using vizrack::CampfireOptions;
    using vizrack::builtin::CampfireEngine;
    using vizrack::builtin::DrawList;
    using vizrack::builtin::DrawPrimitive;

    CampfireEngine engine;
    engine.setOptions({-1, 101, -20, 140, -50, 120});
    const auto normalized = engine.options();
    CHECK(normalized.flameResponse == 0);
    CHECK(normalized.starSpeed == 100);
    CHECK(normalized.starBrightness == 0);
    CHECK(normalized.starResponse == 100);
    CHECK(normalized.particleAmount == 0);
    CHECK(normalized.particleIntensity == 100);
    engine.setOptions(CampfireOptions{});
    engine.setSampleRate(96000);
    engine.setSampleRate(1);
    auto left = engine.inputLeft();
    auto right = engine.inputRight();
    for (size_t index = 0; index < left.size(); ++index) {
        const float phase = static_cast<float>(index) * 0.027f;
        left[index] = std::sin(phase) * 0.32f;
        right[index] = std::cos(phase * 0.91f) * 0.27f;
    }
    left[11] = std::numeric_limits<float>::quiet_NaN();
    right[17] = std::numeric_limits<float>::infinity();
    engine.update(CampfireEngine::kMaxSamples + 64, 1.0f / 60.0f);

    DrawList drawList;
    engine.buildFrame(1920.0f, 1080.0f, drawList);
    CHECK(!drawList.commands().empty());
    CHECK(drawList.commands().front().primitive == DrawPrimitive::verticalGradient);
    CHECK(drawList.commands().size() <= 600);
    CHECK(drawList.points().size() <= 800);
    bool hasRadialGlow = false;
    bool hasFilledFlame = false;
    bool hasGroundRectangle = false;
    for (const auto& command : drawList.commands()) {
        hasRadialGlow = hasRadialGlow ||
                        command.primitive == DrawPrimitive::radialGradientEllipse;
        hasFilledFlame = hasFilledFlame ||
                         command.primitive == DrawPrimitive::fillPolygon;
        hasGroundRectangle = hasGroundRectangle ||
                             command.primitive == DrawPrimitive::fillRectangle;
    }
    CHECK(hasRadialGlow);
    CHECK(hasFilledFlame);
    CHECK(!hasGroundRectangle);
    checkDrawList(drawList);
    const auto info = engine.frameInfo();
    CHECK(std::isfinite(info.lowLevel) && info.lowLevel >= 0.0f && info.lowLevel <= 1.0f);
    CHECK(std::isfinite(info.midLevel) && info.midLevel >= 0.0f && info.midLevel <= 1.0f);
    CHECK(std::isfinite(info.highLevel) && info.highLevel >= 0.0f && info.highLevel <= 1.0f);
    CHECK(std::isfinite(info.stereoLevel) && info.stereoLevel >= 0.0f &&
          info.stereoLevel <= 1.0f);
    CHECK(std::isfinite(info.beatLevel) && info.beatLevel >= 0.0f &&
          info.beatLevel <= 1.0f);
    CHECK(std::isfinite(info.particleActivity) && info.particleActivity >= 0.0f &&
          info.particleActivity <= 1.0f);
    CHECK(std::isfinite(info.meteorActivity) && info.meteorActivity >= 0.0f &&
          info.meteorActivity <= 1.0f);
    CHECK(info.intensity >= 1.0f && info.intensity <= 1.38f);

    const size_t commandCapacity = drawList.commandCapacity();
    const size_t pointCapacity = drawList.pointCapacity();
    for (int frame = 0; frame < 180; ++frame) {
        engine.update(0, frame % 2 == 0 ? 1.0f / 15.0f : 1.0f / 60.0f);
        engine.buildFrame(frame % 3 == 0 ? 640.0f : 2560.0f,
                          frame % 3 == 0 ? 480.0f : 1440.0f, drawList);
        CHECK(drawList.commandCapacity() == commandCapacity);
        CHECK(drawList.pointCapacity() == pointCapacity);
        CHECK(drawList.commands().size() <= 600);
        CHECK(drawList.points().size() <= 800);
        checkDrawList(drawList);
    }

    CampfireEngine at15Fps;
    CampfireEngine at60Fps;
    for (int frame = 0; frame < 15; ++frame) at15Fps.update(0, 1.0f / 15.0f);
    for (int frame = 0; frame < 60; ++frame) at60Fps.update(0, 1.0f / 60.0f);
    DrawList frame15;
    DrawList frame60;
    at15Fps.buildFrame(800.0f, 600.0f, frame15);
    at60Fps.buildFrame(800.0f, 600.0f, frame60);
    CHECK(frame15.commands().size() == frame60.commands().size());
    CHECK(frame15.points().size() == frame60.points().size());
    CHECK(frame15.commands().size() > 1);
    CHECK(frame60.commands().size() > 1);
    if (frame15.commands().size() > 1 && frame60.commands().size() > 1) {
        CHECK(std::abs(frame15.commands()[1].x - frame60.commands()[1].x) < 0.02f);
        CHECK(std::abs(frame15.commands()[1].y - frame60.commands()[1].y) < 0.02f);
    }
    if (!frame15.points().empty() && frame15.points().size() == frame60.points().size()) {
        float maximumDifference = 0.0f;
        for (size_t index = 0; index < frame15.points().size(); ++index) {
            maximumDifference =
                std::max(maximumDifference,
                         std::abs(frame15.points()[index].x - frame60.points()[index].x));
            maximumDifference =
                std::max(maximumDifference,
                         std::abs(frame15.points()[index].y - frame60.points()[index].y));
        }
        CHECK(maximumDifference < 0.02f);
    }

    CampfireEngine idleHeight;
    CampfireEngine reactiveHeight;
    auto reactiveLeft = reactiveHeight.inputLeft();
    auto reactiveRight = reactiveHeight.inputRight();
    for (size_t index = 0; index < reactiveLeft.size(); ++index) {
        const float lowWave = std::sin(static_cast<float>(index) * 0.011f) * 0.82f;
        reactiveLeft[index] = lowWave;
        reactiveRight[index] = lowWave;
    }
    idleHeight.update(0, 1.0f / 60.0f);
    reactiveHeight.update(reactiveLeft.size(), 1.0f / 60.0f);
    DrawList idleFrame;
    DrawList reactiveFrame;
    idleHeight.buildFrame(960.0f, 720.0f, idleFrame);
    reactiveHeight.buildFrame(960.0f, 720.0f, reactiveFrame);
    CHECK(reactiveHeight.frameInfo().beatLevel > idleHeight.frameInfo().beatLevel);
    CHECK(reactiveHeight.frameInfo().particleActivity >
          idleHeight.frameInfo().particleActivity);
    const auto firstFlame = [](const DrawList& list) {
        for (const auto& command : list.commands()) {
            if (command.primitive == DrawPrimitive::fillPolygon &&
                command.points.count == 39) {
                return command.points;
            }
        }
        return vizrack::builtin::PointRange{};
    };
    const auto idleFlame = firstFlame(idleFrame);
    const auto reactiveFlame = firstFlame(reactiveFrame);
    CHECK(idleFlame.count == 39);
    CHECK(reactiveFlame.count == 39);
    if (idleFlame.count == 39 && reactiveFlame.count == 39) {
        const auto idlePoints = idleFrame.points().subspan(idleFlame.offset, idleFlame.count);
        const auto reactivePoints =
            reactiveFrame.points().subspan(reactiveFlame.offset, reactiveFlame.count);
        const float idleBaseWidth = idlePoints.back().x - idlePoints.front().x;
        const float reactiveBaseWidth =
            reactivePoints.back().x - reactivePoints.front().x;
        CHECK(std::abs(idleBaseWidth - reactiveBaseWidth) < 0.01f);
        CHECK(reactivePoints[18].y < idlePoints[18].y);
    }

    CampfireEngine movingSky;
    DrawList skyStart;
    DrawList skyLater;
    movingSky.buildFrame(960.0f, 720.0f, skyStart);
    for (int frame = 0; frame < 60; ++frame) movingSky.update(0, 1.0f / 60.0f);
    movingSky.buildFrame(960.0f, 720.0f, skyLater);
    CHECK(skyStart.commands().size() > 1);
    CHECK(skyLater.commands().size() > 1);
    if (skyStart.commands().size() > 1 && skyLater.commands().size() > 1) {
        CHECK(skyStart.commands()[1].primitive == DrawPrimitive::line);
        CHECK(skyLater.commands()[1].primitive == DrawPrimitive::line);
        CHECK(std::abs(skyStart.commands()[1].x - skyLater.commands()[1].x) > 0.1f);
    }
    const auto starCenter = [](const DrawList& list, size_t star) {
        constexpr size_t commandsPerStar = 7;
        const size_t commandIndex = 1 + star * commandsPerStar + 6;
        if (commandIndex >= list.commands().size()) return vizrack::builtin::Point{};
        const auto& command = list.commands()[commandIndex];
        return vizrack::builtin::Point{command.x + command.width * 0.5f,
                                       command.y + command.height * 0.5f};
    };
    const float poleX = 960.0f * 0.70f;
    const float skyHeight = std::min(720.0f * 0.76f * 0.82f, 720.0f * 0.72f);
    const float poleY = skyHeight * 1.16f;
    for (size_t star = 0; star < 2; ++star) {
        const auto start = starCenter(skyStart, star);
        const auto later = starCenter(skyLater, star);
        const float startX = start.x - poleX;
        const float startY = (start.y - poleY) / 0.58f;
        const float laterX = later.x - poleX;
        const float laterY = (later.y - poleY) / 0.58f;
        CHECK(startX * laterY - startY * laterX > 0.0f);
    }

    CampfireEngine quietFire;
    for (int frame = 0; frame < 9 * 60; ++frame) {
        quietFire.update(0, 1.0f / 60.0f);
    }
    CHECK(quietFire.frameInfo().quietSeconds < 10.0f);
    CHECK(quietFire.frameInfo().fireScale > 0.95f);
    for (int frame = 0; frame < 3 * 60; ++frame) {
        quietFire.update(0, 1.0f / 60.0f);
    }
    CHECK(quietFire.frameInfo().quietSeconds >= 10.0f);
    CHECK(quietFire.frameInfo().fireScale < 0.5f);

    CampfireEngine meteorSky;
    bool sawMeteor = false;
    float firstMeteorSeconds = 0.0f;
    for (int frame = 0; frame < 90 * 60; ++frame) {
        meteorSky.update(0, 1.0f / 60.0f);
        if (!sawMeteor && meteorSky.frameInfo().meteorActivity > 0.0f) {
            sawMeteor = true;
            firstMeteorSeconds = static_cast<float>(frame + 1) / 60.0f;
            break;
        }
    }
    CHECK(sawMeteor);
    CHECK(firstMeteorSeconds >= 38.0f);
    CHECK(firstMeteorSeconds <= 82.0f);
    DrawList meteorFrame;
    meteorSky.buildFrame(960.0f, 720.0f, meteorFrame);
    CHECK(meteorFrame.commands().size() <= 600);
    CHECK(meteorFrame.points().size() <= 800);
    checkDrawList(meteorFrame);

    engine.reset();
    engine.buildFrame(640.0f, 480.0f, drawList);
    CHECK(!drawList.commands().empty());
    engine.buildFrame(0.0f, 480.0f, drawList);
    CHECK(drawList.commands().empty());
    engine.update(0, std::numeric_limits<float>::quiet_NaN());
    engine.buildFrame(std::numeric_limits<float>::infinity(), 480.0f, drawList);
    CHECK(drawList.commands().empty());
}

void testSpectrum3dCore() {
    using vizrack::Spectrum3dOptions;
    using vizrack::builtin::DrawList;
    using vizrack::builtin::DrawPrimitive;
    using vizrack::builtin::Spectrum3dEngine;

    Spectrum3dEngine engine;
    engine.setOptions({-3, 99});
    CHECK(engine.options().style == 0);
    CHECK(engine.options().palette == Spectrum3dEngine::kPaletteCount - 1);
    engine.setOptions({0, 0, -20, 250, -1, 300});
    CHECK(engine.options().rotation == 0);
    CHECK(engine.options().tilt == 100);
    CHECK(engine.options().depth == 0);
    CHECK(engine.options().heightScale == 100);
    engine.setOptions({});
    engine.setSampleRate(96000);
    engine.setSampleRate(1);  // out of range, ignored

    auto left = engine.inputLeft();
    auto right = engine.inputRight();
    for (size_t index = 0; index < left.size(); ++index) {
        const float phase = static_cast<float>(index) * 0.05f;
        left[index] = std::sin(phase) * 0.30f + std::sin(phase * 6.0f) * 0.15f;
        right[index] = std::sin(phase * 1.03f) * 0.27f;
    }
    left[4] = std::numeric_limits<float>::quiet_NaN();
    right[8] = std::numeric_limits<float>::infinity();

    for (int frame = 0; frame < 200; ++frame) {
        engine.update(Spectrum3dEngine::kMaxSamples + 32, 1.0f / 60.0f);
    }
    CHECK(engine.frameInfo().fill > 0.99f);

    DrawList drawList;
    for (int style = 0; style < Spectrum3dEngine::kStyleCount; ++style) {
        CHECK(!Spectrum3dEngine::styleName(style).empty());
        for (int palette = 0; palette < Spectrum3dEngine::kPaletteCount; ++palette) {
            CHECK(!Spectrum3dEngine::palette(palette).name.empty());
            engine.setOptions({style, palette});
            engine.buildFrame(1920.0f, 1080.0f, drawList);
            CHECK(!drawList.commands().empty());
            CHECK(drawList.commands().front().primitive == DrawPrimitive::verticalGradient);
            CHECK(drawList.commands().size() <= 400);
            CHECK(drawList.points().size() <= Spectrum3dEngine::kMaxRenderedPoints);
            checkDrawList(drawList);
            const auto info = engine.frameInfo();
            CHECK(info.style == style);
            CHECK(info.palette == palette);
            CHECK(std::isfinite(info.lowLevel) && info.lowLevel >= 0.0f && info.lowLevel <= 1.0f);
            CHECK(std::isfinite(info.highLevel) && info.highLevel >= 0.0f &&
                  info.highLevel <= 1.0f);
            CHECK(info.styleName == Spectrum3dEngine::styleName(style));
        }
    }

    engine.setOptions({1, 0});
    engine.buildFrame(1600.0f, 900.0f, drawList);
    size_t polylines = 0;
    size_t fills = 0;
    for (const auto& command : drawList.commands()) {
        polylines += command.primitive == DrawPrimitive::polyline ? 1u : 0u;
        fills += command.primitive == DrawPrimitive::fillPolygon ? 1u : 0u;
    }
    CHECK(polylines >= 10);
    CHECK(fills >= 10);

    const size_t commandCapacity = drawList.commandCapacity();
    const size_t pointCapacity = drawList.pointCapacity();
    for (int frame = 0; frame < 240; ++frame) {
        engine.setOptions({frame % 2, frame % Spectrum3dEngine::kPaletteCount});
        engine.update(frame % 3 == 0 ? size_t{0} : size_t{256},
                      frame % 2 == 0 ? 1.0f / 15.0f : 1.0f / 60.0f);
        engine.buildFrame(frame % 2 == 0 ? 640.0f : 2560.0f,
                          frame % 2 == 0 ? 480.0f : 1440.0f, drawList);
        CHECK(drawList.commandCapacity() == commandCapacity);
        CHECK(drawList.pointCapacity() == pointCapacity);
        CHECK(drawList.commands().size() <= 400);
        CHECK(drawList.points().size() <= Spectrum3dEngine::kMaxRenderedPoints);
        checkDrawList(drawList);
    }

    Spectrum3dEngine slow;
    Spectrum3dEngine fast;
    for (int frame = 0; frame < 30; ++frame) slow.update(0, 1.0f / 15.0f);
    for (int frame = 0; frame < 120; ++frame) fast.update(0, 1.0f / 60.0f);
    CHECK(std::abs(slow.frameInfo().fill - fast.frameInfo().fill) < 0.05f);

    engine.reset();
    CHECK(engine.frameInfo().fill == 0.0f);
    engine.buildFrame(800.0f, 600.0f, drawList);
    CHECK(drawList.commands().size() == 1);
    engine.buildFrame(0.0f, 600.0f, drawList);
    CHECK(drawList.commands().empty());
    engine.update(0, std::numeric_limits<float>::quiet_NaN());
    engine.buildFrame(std::numeric_limits<float>::infinity(), 600.0f, drawList);
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

void testI18n() {
    using vizrack::Str;
    using vizrack::UiLanguage;

    const int count = static_cast<int>(Str::count_);
    CHECK(count > 0);

    // Every string must be present and non-empty in every language, and must
    // survive the UTF-8 -> UTF-16 conversion used for Win32. This is the guard
    // that a newly added Str row was translated in both columns.
    for (int index = 0; index < count; ++index) {
        const auto id = static_cast<Str>(index);
        vizrack::setUiLanguage(UiLanguage::english);
        const char* english = vizrack::tr(id);
        const std::wstring englishWide = vizrack::trw(id);
        vizrack::setUiLanguage(UiLanguage::korean);
        const char* korean = vizrack::tr(id);
        const std::wstring koreanWide = vizrack::trw(id);
        CHECK(english != nullptr && english[0] != '\0');
        CHECK(korean != nullptr && korean[0] != '\0');
        CHECK(!englishWide.empty());
        CHECK(!koreanWide.empty());
    }

    CHECK(vizrack::resolveUiLanguage("en") == UiLanguage::english);
    CHECK(vizrack::resolveUiLanguage("ko") == UiLanguage::korean);
    static_cast<void>(vizrack::resolveUiLanguage("auto"));  // must not crash
    CHECK(vizrack::uiLanguageToken(UiLanguage::english) == "en");
    CHECK(vizrack::uiLanguageToken(UiLanguage::korean) == "ko");

    vizrack::setUiLanguage(UiLanguage::english);
    CHECK(vizrack::currentUiLanguage() == UiLanguage::english);
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
    testCampfireCore();
    testSpectrum3dCore();
    testOscilloscopeCore();
    testReconnect();
    testPluginState(directory);
    testI18n();
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
