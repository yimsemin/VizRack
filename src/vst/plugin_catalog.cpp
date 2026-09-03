#include "vst/plugin_catalog.h"

#include <algorithm>

namespace vizrack {

const std::vector<PluginDefinition>& pluginCatalog() {
    static const std::vector<PluginDefinition> catalog{
        {
            PluginKind::builtIn,
            "builtin-oscilloscope",
            "내장 오실로스코프",
            "VizRack",
            "",
            "",
            "",
            "Built-in",
            {},
            "",
        },
        {
            PluginKind::builtIn,
            "builtin-art-visualizer",
            "내장 아트 비주얼라이저",
            "VizRack",
            "",
            "",
            "",
            "6 Scenes / 6 Palettes",
            {},
            "",
        },
        {
            PluginKind::builtIn,
            "builtin-campfire",
            "내장 캠프파이어",
            "VizRack",
            "",
            "",
            "",
            "Natural Flame / Audio Reactive",
            {},
            "",
        },
        {
            PluginKind::builtIn,
            "builtin-spectrum3d",
            "내장 3D 스펙트럼",
            "VizRack",
            "",
            "",
            "",
            "Classic Cascade / Time Depth",
            {},
            "",
        },
        {
            PluginKind::builtIn,
            "builtin-joydivision",
            "내장 Joy Division",
            "VizRack",
            "",
            "",
            "",
            "Ridgeline Cascade",
            {},
            "",
        },
        {
            PluginKind::vst3,
            "mvmeter2",
            "mvMeter2",
            "TBProAudio",
            "mvmeter2",
            "tbproaudio",
            "",
            "GPU / noGPU",
            {std::filesystem::path(L"VST3") / L"TBProAudio"},
            "https://www.tbproaudio.de/products/mvmeter2",
        },
        {
            PluginKind::vst3,
            "anspec",
            "AnSpec",
            "Voxengo",
            "anspec",
            "voxengo",
            "com.voxengo.audio-plugins.VST3.AnSpec",
            "Free",
            {std::filesystem::path(L"VST3") / L"AnSpec.vst3"},
            "https://www.voxengo.com/product/anspec/",
        },
    };
    return catalog;
}

const PluginDefinition* findPluginDefinition(std::string_view id) {
    const auto& catalog = pluginCatalog();
    const auto found = std::find_if(catalog.begin(), catalog.end(), [id](const auto& definition) {
        return definition.id == id;
    });
    return found == catalog.end() ? nullptr : &*found;
}

} // namespace vizrack
