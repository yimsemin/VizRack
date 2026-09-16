#include "vst/plugin_catalog.h"

#include <algorithm>

namespace vizrack {

const std::vector<PluginDefinition>& pluginCatalog() {
    static const std::vector<PluginDefinition> catalog{
        {
            PluginKind::builtIn,
            "builtin-oscilloscope",
            "Oscilloscope",
            "VizRack",
            "",
            "",
            "",
            "Built-in",
            {},
            "",
            "",
        },
        {
            PluginKind::builtIn,
            "builtin-art-visualizer",
            "Art Visualizer",
            "VizRack",
            "",
            "",
            "",
            "6 Scenes / 6 Palettes",
            {},
            "",
            "",
        },
        {
            PluginKind::builtIn,
            "builtin-campfire",
            "Campfire",
            "VizRack",
            "",
            "",
            "",
            "Natural Flame / Audio Reactive",
            {},
            "",
            "",
        },
        {
            PluginKind::builtIn,
            "builtin-spectrum3d",
            "Classic Cascade",
            "VizRack",
            "",
            "",
            "",
            "Time-Depth Spectrum",
            {},
            "",
            "",
        },
        {
            PluginKind::builtIn,
            "builtin-joydivision",
            "Joy Division",
            "VizRack",
            "",
            "",
            "",
            "Ridgeline Cascade",
            {},
            "",
            "Inspired by Joy Division",
        },
        {
            PluginKind::builtIn,
            "builtin-starguitar",
            "Star Guitar",
            "VizRack",
            "",
            "",
            "",
            "Rhythm Sequencer Landscape",
            {},
            "",
            "Inspired by The Chemical Brothers / Michel Gondry's \"Star Guitar\"",
        },
        {
            PluginKind::builtIn,
            "builtin-rhythmripple",
            "Rhythm Ripple",
            "VizRack",
            "",
            "",
            "",
            "One Beat, One Ripple",
            {},
            "",
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
            "",
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
            "",
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
