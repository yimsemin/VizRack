#include "vst/vst_host.h"

#include "core/audio_ring.h"
#include "core/logger.h"
#include "core/plugin_state.h"
#include "core/utf.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace vizrack {
namespace {

using namespace Steinberg;
using namespace Steinberg::Vst;

constexpr int32 kProcessBlockSize = 2048;
constexpr size_t kParameterQueueSize = 256;

bool succeeded(tresult result) {
    return result == kResultOk || result == kResultTrue;
}

std::string resultText(tresult result) {
    return "VST3 result " + std::to_string(static_cast<int64_t>(result));
}

std::vector<uint8_t> streamBytes(MemoryStream& stream) {
    const auto size = stream.getSize();
    if (size <= 0 || !stream.getData()) return {};
    const auto* begin = reinterpret_cast<const uint8_t*>(stream.getData());
    return {begin, begin + static_cast<size_t>(size)};
}

} // namespace

struct VstHost::Impl final : Steinberg::Vst::IComponentHandler, Steinberg::IPlugFrame {
    struct ParameterEdit {
        ParamID id{};
        ParamValue value{};
    };

    Impl(StereoFrameRing& audioRing, Logger& appLogger) : ring(audioRing), logger(appLogger) {}

    tresult PLUGIN_API queryInterface(const TUID interfaceId, void** object) override {
        if (!object) return kInvalidArgument;
        if (FUnknownPrivate::iidEqual(interfaceId, IComponentHandler::iid)) {
            *object = static_cast<IComponentHandler*>(this);
            addRef();
            return kResultTrue;
        }
        if (FUnknownPrivate::iidEqual(interfaceId, IPlugFrame::iid)) {
            *object = static_cast<IPlugFrame*>(this);
            addRef();
            return kResultTrue;
        }
        if (FUnknownPrivate::iidEqual(interfaceId, FUnknown::iid)) {
            *object = static_cast<IPlugFrame*>(this);
            addRef();
            return kResultTrue;
        }
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1000; }
    uint32 PLUGIN_API release() override { return 1000; }

    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        const uint32_t write = parameterWrite.load(std::memory_order_relaxed);
        const uint32_t read = parameterRead.load(std::memory_order_acquire);
        if (write - read >= parameterQueue.size()) {
            return kResultFalse;
        }
        parameterQueue[write % parameterQueue.size()] = {id, value};
        parameterWrite.store(write + 1, std::memory_order_release);
        wake.notify_one();
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override {
        logger.debug("VST3 restartComponent flags=" + std::to_string(flags));
        return kResultOk;
    }
    tresult PLUGIN_API resizeView(IPlugView* requestedView, ViewRect* newSize) override {
        if (!requestedView || requestedView != view || !newSize) return kInvalidArgument;
        if (resizeInProgress) return kResultFalse;
        const int width = newSize->right - newSize->left;
        const int height = newSize->bottom - newSize->top;
        if (width <= 0 || height <= 0) return kInvalidArgument;
        resizeInProgress = true;
        if (resizeCallback) resizeCallback(width, height);
        const auto result = view->onSize(newSize);
        resizeInProgress = false;
        return result;
    }

    bool configureBusses(std::string& error) {
        if (!processor || !component) {
            error = "The VST3 processor/component is missing.";
            return false;
        }
        if (processor->canProcessSampleSize(kSample32) != kResultTrue) {
            error = "The plug-in does not support 32-bit float processing.";
            return false;
        }
        const int32 inputCount = component->getBusCount(kAudio, kInput);
        const int32 outputCount = component->getBusCount(kAudio, kOutput);
        if (inputCount < 1) {
            error = "The plug-in has no audio input bus.";
            return false;
        }
        std::vector<SpeakerArrangement> inputs(static_cast<size_t>(inputCount), SpeakerArr::kEmpty);
        std::vector<SpeakerArrangement> outputs(static_cast<size_t>(outputCount), SpeakerArr::kEmpty);
        for (int32 index = 0; index < inputCount; ++index) {
            processor->getBusArrangement(kInput, index, inputs[static_cast<size_t>(index)]);
        }
        for (int32 index = 0; index < outputCount; ++index) {
            processor->getBusArrangement(kOutput, index, outputs[static_cast<size_t>(index)]);
        }
        inputs[0] = SpeakerArr::kStereo;
        if (outputCount > 0) outputs[0] = SpeakerArr::kStereo;
        processor->setBusArrangements(inputs.data(), inputCount,
                                      outputs.empty() ? nullptr : outputs.data(), outputCount);

        for (int32 index = 0; index < inputCount; ++index) {
            component->activateBus(kAudio, kInput, index, index == 0);
        }
        for (int32 index = 0; index < outputCount; ++index) {
            component->activateBus(kAudio, kOutput, index, index == 0);
        }
        BusInfo inputInfo{};
        if (!succeeded(component->getBusInfo(kAudio, kInput, 0, inputInfo)) ||
            inputInfo.channelCount < 2) {
            error = "Could not configure the plug-in's main input bus as stereo.";
            return false;
        }
        return true;
    }

    void restoreState(const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return;
        PluginStateData state;
        std::string error;
        if (!loadPluginStateFile(path, state, error) || state.classId != descriptor.classId) {
            if (error.empty()) error = "The saved state's class ID differs from the current plug-in.";
            logger.warning("Plugin state ignored: " + error);
            quarantineCorruptState(path);
            return;
        }
        if (!state.component.empty()) {
            MemoryStream componentStream(state.component.data(), static_cast<TSize>(state.component.size()));
            if (!succeeded(component->setState(&componentStream))) {
                error = "component setState failed";
            } else {
                componentStream.seek(0, IBStream::kIBSeekSet, nullptr);
                if (controller) {
                    const auto controllerResult = controller->setComponentState(&componentStream);
                    if (!succeeded(controllerResult)) {
                        logger.debug("Controller did not accept component state; continuing: " +
                                     resultText(controllerResult));
                    }
                }
            }
        }
        if (error.empty() && controller && !state.controller.empty()) {
            MemoryStream controllerStream(state.controller.data(), static_cast<TSize>(state.controller.size()));
            if (!succeeded(controller->setState(&controllerStream))) error = "controller setState failed";
        }
        if (!error.empty()) {
            logger.warning("Plugin state restore failed: " + error);
            quarantineCorruptState(path);
        } else {
            logger.info("Plugin state restored from " + toUtf8(path.wstring()));
        }
    }

    bool configureProcessing(uint32_t rate) {
        if (!processor || !component || rate < 8000) return false;
        if (processingActive) {
            processor->setProcessing(false);
            component->setActive(false);
            processingActive = false;
        }
        processData.unprepare();
        ProcessSetup setup{kRealtime, kSample32, kProcessBlockSize, static_cast<SampleRate>(rate)};
        const tresult setupResult = processor->setupProcessing(setup);
        if (!succeeded(setupResult)) {
            logger.error("VST3 setupProcessing failed: " + resultText(setupResult));
            return false;
        }
        if (!succeeded(component->setActive(true))) {
            logger.error("VST3 setActive(true) failed");
            return false;
        }
        processData.prepare(*component, kProcessBlockSize, kSample32);
        processContext = {};
        processContext.sampleRate = rate;
        processContext.state = ProcessContext::kContTimeValid;
        processData.processContext = &processContext;
        processData.inputEvents = &inputEvents;
        processData.inputParameterChanges = &inputParameterChanges;
        processData.processMode = kRealtime;
        processData.symbolicSampleSize = kSample32;
        const tresult processingResult = processor->setProcessing(true);
        if (!succeeded(processingResult)) {
            component->setActive(false);
            logger.error("VST3 setProcessing(true) failed: " + resultText(processingResult));
            return false;
        }
        currentSampleRate = rate;
        processingActive = true;
        logger.info("VST3 processing configured: sampleRate=" + std::to_string(rate) +
                    " blockSize=" + std::to_string(kProcessBlockSize));
        return true;
    }

    void transferParameterEdits() {
        uint32_t read = parameterRead.load(std::memory_order_relaxed);
        const uint32_t write = parameterWrite.load(std::memory_order_acquire);
        while (read != write) {
            const auto edit = parameterQueue[read % parameterQueue.size()];
            int32 queueIndex = 0;
            if (auto* queue = inputParameterChanges.addParameterData(edit.id, queueIndex)) {
                int32 pointIndex = 0;
                queue->addPoint(0, edit.value, pointIndex);
            }
            ++read;
        }
        parameterRead.store(read, std::memory_order_release);
    }

    void processOneBlock(bool forceSilence) {
        if (!processingActive || processData.numInputs < 1 ||
            processData.inputs[0].numChannels < 2) return;
        ring.discardOlderThan(static_cast<size_t>(kProcessBlockSize) * 3);
        auto* left = processData.inputs[0].channelBuffers32[0];
        auto* right = processData.inputs[0].channelBuffers32[1];
        size_t frames = forceSilence ? 0 : ring.popPlanar(left, right, kProcessBlockSize);
        if (frames == 0) frames = std::min<uint32_t>(currentSampleRate / 20, kProcessBlockSize);
        if (forceSilence || ring.available() == 0) {
            if (forceSilence) {
                std::fill_n(left, frames, 0.0f);
                std::fill_n(right, frames, 0.0f);
            }
        }
        for (int32 channel = 2; channel < processData.inputs[0].numChannels; ++channel) {
            std::fill_n(processData.inputs[0].channelBuffers32[channel], frames, 0.0f);
        }
        for (int32 bus = 1; bus < processData.numInputs; ++bus) {
            for (int32 channel = 0; channel < processData.inputs[bus].numChannels; ++channel) {
                std::fill_n(processData.inputs[bus].channelBuffers32[channel], frames, 0.0f);
            }
            processData.inputs[bus].silenceFlags = HostProcessData::kAllChannelsSilent;
        }
        for (int32 bus = 0; bus < processData.numOutputs; ++bus) {
            for (int32 channel = 0; channel < processData.outputs[bus].numChannels; ++channel) {
                std::fill_n(processData.outputs[bus].channelBuffers32[channel], frames, 0.0f);
            }
            processData.outputs[bus].silenceFlags = 0;
        }
        processData.inputs[0].silenceFlags = forceSilence ? HostProcessData::kAllChannelsSilent : 0;
        processData.numSamples = static_cast<int32>(frames);
        processContext.continousTimeSamples = continuousFrames;
        transferParameterEdits();
        const tresult result = processor->process(processData);
        inputParameterChanges.clearQueue();
        inputEvents.clear();
        continuousFrames += static_cast<int64>(frames);
        if (!succeeded(result) && !processErrorReported.exchange(true)) {
            logger.error("VST3 process failed: " + resultText(result));
        }
    }

    void processingLoop() {
        uint32_t desired = desiredSampleRate.load(std::memory_order_acquire);
        configureProcessing(desired);
        while (!stopRequested.load(std::memory_order_acquire)) {
            std::unique_lock lock(wakeMutex);
            const bool signalled = wake.wait_for(lock, std::chrono::milliseconds(50), [&] {
                return stopRequested.load(std::memory_order_acquire) || ring.available() > 0 ||
                       desiredSampleRate.load(std::memory_order_acquire) != currentSampleRate;
            });
            lock.unlock();
            if (stopRequested.load(std::memory_order_acquire)) break;
            desired = desiredSampleRate.load(std::memory_order_acquire);
            if (desired != currentSampleRate) configureProcessing(desired);
            if (!processingActive) continue;
            if (ring.available() > 0) {
                while (ring.available() > 0 && !stopRequested.load(std::memory_order_relaxed)) {
                    processOneBlock(false);
                }
            } else if (!signalled) {
                processOneBlock(true);
            }
        }
        if (processingActive) {
            processor->setProcessing(false);
            component->setActive(false);
            processingActive = false;
        }
        processData.unprepare();
    }

    StereoFrameRing& ring;
    Logger& logger;
    PluginDescriptor descriptor;
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IEditController> controller;
    FUnknownPtr<IAudioProcessor> processor;
    IPtr<IPlugView> view;
    HostApplication hostApplication;
    HWND editorParent{nullptr};
    EditorResizeCallback resizeCallback;

    HostProcessData processData;
    ProcessContext processContext{};
    EventList inputEvents;
    ParameterChanges inputParameterChanges{64};
    std::array<ParameterEdit, kParameterQueueSize> parameterQueue{};
    std::atomic<uint32_t> parameterWrite{0};
    std::atomic<uint32_t> parameterRead{0};
    std::atomic<uint32_t> desiredSampleRate{48000};
    uint32_t currentSampleRate{0};
    int64 continuousFrames{0};
    bool processingActive{false};
    bool resizeInProgress{false};
    std::atomic<bool> processErrorReported{false};
    std::atomic<bool> stopRequested{false};
    std::thread processThread;
    std::condition_variable wake;
    std::mutex wakeMutex;
};

VstHost::VstHost(StereoFrameRing& ring, Logger& logger)
    : impl_(std::make_unique<Impl>(ring, logger)) {}

VstHost::~VstHost() { unload(); }

VstLoadResult VstHost::load(const PluginDescriptor& descriptor,
                            const std::filesystem::path& statePath) {
    unload();
    VstLoadResult result;
    impl_->descriptor = descriptor;
    std::string error;
    impl_->module = VST3::Hosting::Module::create(toUtf8(descriptor.modulePath.wstring()), error);
    if (!impl_->module) {
        result.message = "Could not open the VST3 module: " + error;
        return result;
    }
    auto factory = impl_->module->getFactory();
    factory.setHostContext(&impl_->hostApplication);
    Steinberg::Vst::PluginContextFactory::instance().setPluginContext(&impl_->hostApplication);
    for (const auto& info : factory.classInfos()) {
        if (std::memcmp(info.ID().data(), descriptor.classId.data(), descriptor.classId.size()) != 0) continue;
        impl_->provider = Steinberg::owned(new Steinberg::Vst::PlugProvider(factory, info, true));
        break;
    }
    if (!impl_->provider || !impl_->provider->initialize()) {
        result.message = "VST3 component/controller initialization failed.";
        unload();
        return result;
    }
    impl_->component = impl_->provider->getComponentPtr();
    impl_->controller = impl_->provider->getControllerPtr();
    impl_->processor = impl_->component;
    if (!impl_->component || !impl_->controller || !impl_->processor) {
        result.message = "The plug-in does not provide the required component, controller or audio processor.";
        unload();
        return result;
    }
    impl_->controller->setComponentHandler(impl_.get());
    if (!impl_->configureBusses(error)) {
        result.message = error;
        unload();
        return result;
    }
    impl_->restoreState(statePath);
    impl_->view = Steinberg::owned(impl_->controller->createView(Steinberg::Vst::ViewType::kEditor));
    if (!impl_->view) {
        result.message = "The plug-in does not provide an official VST3 editor GUI.";
        unload();
        return result;
    }
    ViewRect initialEditorRect{};
    const bool editorResizable = impl_->view->canResize() == Steinberg::kResultTrue;
    if (succeeded(impl_->view->getSize(&initialEditorRect))) {
        impl_->logger.info("VST3 editor: size=" +
                           std::to_string(initialEditorRect.right - initialEditorRect.left) + "x" +
                           std::to_string(initialEditorRect.bottom - initialEditorRect.top) +
                           " hostResizable=" + (editorResizable ? "true" : "false"));
    }
    impl_->logger.info("VST3 loaded: path='" + toUtf8(descriptor.modulePath.wstring()) +
                       "' class='" + descriptor.name + "' vendor='" + descriptor.vendor +
                       "' version='" + descriptor.version + "'");
    result.loaded = true;
    return result;
}

void VstHost::unload() {
    stopProcessing();
    detachEditor();
    if (impl_->controller) impl_->controller->setComponentHandler(nullptr);
    impl_->view = nullptr;
    impl_->processor = nullptr;
    impl_->controller = nullptr;
    impl_->component = nullptr;
    impl_->provider = nullptr;
    impl_->module.reset();
    Steinberg::Vst::PluginContextFactory::instance().setPluginContext(nullptr);
}

bool VstHost::startProcessing(uint32_t sampleRate) {
    if (!loaded() || impl_->processThread.joinable()) return false;
    impl_->desiredSampleRate.store(sampleRate, std::memory_order_release);
    impl_->stopRequested.store(false, std::memory_order_release);
    impl_->processErrorReported.store(false, std::memory_order_release);
    impl_->processThread = std::thread(&Impl::processingLoop, impl_.get());
    return true;
}

void VstHost::stopProcessing() {
    if (!impl_->processThread.joinable()) return;
    impl_->stopRequested.store(true, std::memory_order_release);
    impl_->wake.notify_all();
    impl_->processThread.join();
}

void VstHost::setSampleRate(uint32_t sampleRate) {
    if (sampleRate < 8000 || sampleRate > 768000) return;
    impl_->desiredSampleRate.store(sampleRate, std::memory_order_release);
    impl_->wake.notify_one();
}

void VstHost::notifyDataReady() { impl_->wake.notify_one(); }

bool VstHost::attachEditor(HWND parent, EditorResizeCallback resizeCallback, std::string& error) {
    if (!impl_->view || !parent) {
        error = "There is no VST3 editor to attach.";
        return false;
    }
    if (impl_->view->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) != Steinberg::kResultTrue) {
        error = "The VST3 GUI does not support Win32 HWND embedding.";
        return false;
    }
    impl_->editorParent = parent;
    impl_->resizeCallback = std::move(resizeCallback);
    impl_->view->setFrame(impl_.get());
    if (impl_->view->attached(parent, Steinberg::kPlatformTypeHWND) != Steinberg::kResultTrue) {
        impl_->view->setFrame(nullptr);
        impl_->editorParent = nullptr;
        error = "Could not attach the VST3 GUI to the host window.";
        return false;
    }
    return true;
}

void VstHost::detachEditor() {
    if (impl_->view && impl_->editorParent) {
        impl_->view->setFrame(nullptr);
        impl_->view->removed();
    }
    impl_->editorParent = nullptr;
    impl_->resizeCallback = {};
}

void VstHost::resizeEditor(int width, int height) {
    if (!impl_->view || !impl_->editorParent || width <= 0 || height <= 0) return;
    const auto [constrainedWidth, constrainedHeight] = constrainEditorSize(width, height);
    Steinberg::ViewRect rect{0, 0, static_cast<Steinberg::int32>(constrainedWidth),
                             static_cast<Steinberg::int32>(constrainedHeight)};
    impl_->view->onSize(&rect);
}

std::pair<int, int> VstHost::constrainEditorSize(int width, int height) const {
    if (!impl_->view || width <= 0 || height <= 0) return {width, height};
    Steinberg::ViewRect rect{0, 0, static_cast<Steinberg::int32>(width),
                             static_cast<Steinberg::int32>(height)};
    if (!succeeded(impl_->view->checkSizeConstraint(&rect))) return {width, height};
    return {std::max(1, rect.right - rect.left), std::max(1, rect.bottom - rect.top)};
}

bool VstHost::editorHostResizable() const {
    return impl_->view && impl_->view->canResize() == Steinberg::kResultTrue;
}

void VstHost::setEditorContentScale(float scale) {
    if (!impl_->view) return;
    Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport> support(impl_->view.get());
    if (support) support->setContentScaleFactor(scale);
}

std::pair<int, int> VstHost::editorSize() const {
    if (!impl_->view) return {0, 0};
    Steinberg::ViewRect rect{};
    if (impl_->view->getSize(&rect) != Steinberg::kResultTrue) return {0, 0};
    return {rect.right - rect.left, rect.bottom - rect.top};
}

bool VstHost::saveState(const std::filesystem::path& path, std::string& error) {
    if (!impl_->component || !impl_->controller) {
        error = "There is no plug-in instance to save.";
        return false;
    }
    PluginStateData state;
    state.classId = impl_->descriptor.classId;
    Steinberg::MemoryStream componentStream;
    const auto componentResult = impl_->component->getState(&componentStream);
    if (!succeeded(componentResult)) {
        error = "Failed to read the VST3 component state: " + resultText(componentResult);
        return false;
    }
    state.component = streamBytes(componentStream);
    Steinberg::MemoryStream controllerStream;
    const auto controllerResult = impl_->controller->getState(&controllerStream);
    if (succeeded(controllerResult)) state.controller = streamBytes(controllerStream);
    if (!savePluginStateFile(path, state, error)) return false;
    impl_->logger.info("Plugin state saved to " + toUtf8(path.wstring()));
    return true;
}

bool VstHost::loaded() const noexcept {
    return impl_->module && impl_->component && impl_->controller && impl_->processor;
}

const PluginDescriptor& VstHost::descriptor() const { return impl_->descriptor; }

} // namespace vizrack
