///////////////////////////////////////////////////////////////////////
//
// Part of ShaderToggler, a shader toggler add on for ReShade 5+ which allows you
// to define groups of shaders to toggle them on/off with one key press
//
// (c) Frans 'Otis_Inf' Bouma.
//
// All rights reserved.
// https://github.com/FransBouma/ShaderToggler
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
//
//  * Redistributions of source code must retain the above copyright notice, this
//	  list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and / or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/////////////////////////////////////////////////////////////////////////
#include "AddonUIData.h"
#include "AddonUIDisplay.h"
#include "CDataFile.h"
#include "ConstantManager.h"
#include "KeyMonitor.h"
#include "PipelinePrivateData.h"
#include "RenderingBindingManager.h"
#include <cstring>
#include "RenderingEffectManager.h"
#include "RenderingManager.h"
#include "RenderingPreviewManager.h"
#include "RenderingQueueManager.h"
#include "RenderingShaderManager.h"
#include "ResourceManager.h"
#include "ShaderManager.h"
#include "StateTracking.h"
#include "TechniqueManager.h"
#include "ToggleGroup.h"
#include "crc32_hash.hpp"
#include <MinHook.h>
#include <chrono>
#include <d3d11.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <cmath>
#include <filesystem>
#include <format>
#include <functional>
#include <imgui.h>
#include <reshade.hpp>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace reshade::api;
using namespace ShaderToggler;
using namespace AddonImGui;
using namespace Shim::Constants;
using namespace std;

extern "C" __declspec(dllexport) const char* NAME = "REST Enhanced — ReShade Effect Shader Toggler";
extern "C" __declspec(dllexport) const char* AUTHOR = "DeViLhoOD";
#if REST_LOCALIZED_UI
extern "C" __declspec(dllexport) const char* DESCRIPTION =
  "在着色器指定的渲染阶段应用选定的 ReShade 效果，支持为升采样渲染自动注入场景色彩。";
#else
extern "C" __declspec(dllexport) const char* DESCRIPTION =
  "ReShade add-on for applying selected effects at shader-defined points in the rendering pipeline, including automatic scene-colour injection for upscaled rendering.";
#endif
extern "C" __declspec(dllexport) const char* WEBSITE = "https://github.com/Pav-Osmolski/ReshadeEffectShaderToggler";
extern "C" __declspec(dllexport) const char* ISSUES = "https://github.com/Pav-Osmolski/ReshadeEffectShaderToggler/issues";

constexpr auto MAX_EFFECT_HANDLES = 128;
constexpr auto REST_VAR_ANNOTATION = "source";

static filesystem::path g_dllPath;
static filesystem::path g_basePath;

static ShaderToggler::ShaderManager g_pixelShaderManager("PS");
static ShaderToggler::ShaderManager g_vertexShaderManager("VS");
static ShaderToggler::ShaderManager g_computeShaderManager("CS");

static ConstantManager constantManager;
static ConstantHandlerBase* constantHandler = nullptr;
static ConstantCopyBase* constantCopy = nullptr;
static bool constantHandlerHooked = false;

static atomic_uint32_t g_activeCollectorFrameCounter = 0;
static AddonUIData g_addonUIData(&g_pixelShaderManager, &g_vertexShaderManager, &g_computeShaderManager, constantHandler, &g_activeCollectorFrameCounter);

static KeyMonitor keyMonitor;
static Rendering::ResourceManager resourceManager;
static Rendering::ToggleGroupResourceManager groupResourceManager;
static Rendering::RenderingShaderManager renderingShaderManager(g_addonUIData, resourceManager);
static Rendering::RenderingEffectManager renderingEffectManager(g_addonUIData, resourceManager, renderingShaderManager, groupResourceManager);
static Rendering::RenderingBindingManager renderingBindingManager(g_addonUIData, resourceManager, groupResourceManager);
static Rendering::RenderingPreviewManager renderingPreviewManager(g_addonUIData, resourceManager, renderingShaderManager);
static Rendering::RenderingQueueManager renderingQueueManager(g_addonUIData, resourceManager);
static ShaderToggler::TechniqueManager techniqueManager(keyMonitor);

// TODO: actually implement ability to turn off srgb-view generation
static vector<effect_runtime*> runtimes;

/// <summary>
/// Calculates a crc32 hash from the passed in shader bytecode. The hash is used to identity the shader in future runs.
/// </summary>
/// <param name="shaderData"></param>
/// <returns></returns>
static uint32_t calculateShaderHash(void* shaderData) {
    if (nullptr == shaderData) {
        return 0;
    }

    const auto shaderDesc = *static_cast<shader_desc*>(shaderData);
    return compute_crc32(static_cast<const uint8_t*>(shaderDesc.code), shaderDesc.code_size);
}

static void onInitDevice(device* device) {
    if (device->get_api() == device_api::d3d9)
        reshade::log::message(reshade::log::level::info, "D3D9 copy/preview state: native target and depth snapshots enabled.");
    device->create_private_data<DeviceDataContainer>();
}

static void releaseScissorEnabledRasterizerStates(ID3D11Device* device);

static void onDestroyDevice(device* device) {
    if (device->get_api() == device_api::d3d11)
        releaseScissorEnabledRasterizerStates(reinterpret_cast<ID3D11Device*>(device->get_native()));
    DeviceDataContainer& data = *device->get_private_data<DeviceDataContainer>();

    groupResourceManager.DisposeGroupBuffers(device, g_addonUIData.GetToggleGroups());
    renderingBindingManager.DisposeTextureBindings(device, g_addonUIData.GetToggleGroups());
    resourceManager.OnDestroyDevice(device);
    renderingShaderManager.DestroyShaders(device);

    device->destroy_private_data<DeviceDataContainer>();
}

static void onInitCommandQueue(command_queue* queue) {
    auto& data = *queue->get_device()->get_private_data<DeviceDataContainer>();
    std::lock_guard lock(data.queueMutex);
    data.queues.push_back(queue);
}

static void onDestroyCommandQueue(command_queue* queue) {
    auto& data = *queue->get_device()->get_private_data<DeviceDataContainer>();
    std::lock_guard lock(data.queueMutex);
    std::erase(data.queues, queue);
}

static void onInitCommandList(command_list* commandList) {
    commandList->create_private_data<CommandListDataContainer>();
}

static void onDestroyCommandList(command_list* commandList) {
    auto& previewData = *commandList->get_device()->get_private_data<DeviceDataContainer>();
    std::lock_guard previewLock(previewData.previewMutex);
    auto& preview = previewData.huntPreview;
    if (preview.tracked_command_list == commandList)
        preview.tracked_command_list = nullptr;
    if (preview.vulkan_command_list == commandList) {
        // The state tracker may already have been destroyed by its own callback.
        preview.vulkan_capture_pending = false;
        preview.vulkan_command_list = nullptr;
        commandList->get_device()->get_private_data<DeviceDataContainer>()->vulkanPreviewWorkPending.store(false);
    }
    commandList->destroy_private_data<CommandListDataContainer>();
}

static void onResetCommandList(command_list* commandList) {
    auto& previewData = *commandList->get_device()->get_private_data<DeviceDataContainer>();
    std::lock_guard previewLock(previewData.previewMutex);
    auto& preview = previewData.huntPreview;
    if (preview.tracked_command_list == commandList)
        preview.tracked_command_list = nullptr;
    if (preview.vulkan_command_list == commandList) {
        preview.vulkan_capture_pending = false;
        preview.vulkan_command_list = nullptr;
        commandList->get_device()->get_private_data<DeviceDataContainer>()->vulkanPreviewWorkPending.store(false);
    }
    CommandListDataContainer& commandListData = *commandList->get_private_data<CommandListDataContainer>();
    commandListData.Reset();
}

static bool onCreateSwapchain(device_api api, swapchain_desc& desc, void* hwnd) {
    (void)api;
    return resourceManager.OnCreateSwapchain(desc, hwnd);
}

static void onInitSwapchain(reshade::api::swapchain* swapchain, bool resize) {
    (void)resize;
    resourceManager.OnInitSwapchain(swapchain);
}

static void onDestroySwapchain(reshade::api::swapchain* swapchain, bool resize) {
    (void)resize;
    resourceManager.OnDestroySwapchain(swapchain);
}

static bool onCreateResource(device* device, resource_desc& desc, subresource_data* initial_data, resource_usage initial_state) {
    return resourceManager.OnCreateResource(device, desc, initial_data, initial_state);
}

static void ForgetIndexBuffer(DeviceDataContainer& state, uint64_t handle) {
    const auto it = state.indexBuffers.find(handle);
    if (it != state.indexBuffers.end()) {
        for (const auto& snapshot : it->second)
            state.indexBufferBytes -= snapshot.bytes.size();
        state.indexBuffers.erase(it);
    }
}

static void RememberIndexBuffer(device* device, const resource_desc& desc, const subresource_data* initData, resource buffer) {
    // GPU-writable and shared resources can change without a CPU upload event.
    constexpr auto gpuWrites = resource_usage::unordered_access | resource_usage::stream_output | resource_usage::render_target;
    constexpr auto externalStorage = resource_flags::shared | resource_flags::shared_nt_handle | resource_flags::sparse_binding;
    constexpr size_t byteBudget = DeviceDataContainer::indexBufferByteBudget;
    if (device->get_api() == device_api::d3d12 && desc.type == resource_type::buffer &&
        (desc.usage & resource_usage::unordered_access) == resource_usage::undefined &&
        (desc.flags & externalStorage) == resource_flags::none &&
        (desc.heap == memory_heap::default_ || desc.heap == memory_heap::upload)) {
        auto& state = *device->get_private_data<DeviceDataContainer>();
        std::lock_guard lock(state.indexBufferMutex);
        const bool upload = desc.heap == memory_heap::upload;
        state.d3d12Buffers.try_emplace(buffer.handle, DeviceDataContainer::D3D12BufferInfo{
            ++state.d3d12IndexSerial, desc.buffer.size, 0, upload, 0, false, upload ? std::make_shared<uint64_t>(0) : nullptr});
        return;
    }
    if (device->get_api() != device_api::d3d11 || desc.type != resource_type::buffer ||
        (desc.usage & resource_usage::index_buffer) == resource_usage::undefined ||
        (desc.usage & gpuWrites) != resource_usage::undefined ||
        (desc.flags & externalStorage) != resource_flags::none || initData == nullptr || initData->data == nullptr ||
        desc.buffer.size == 0 || desc.buffer.size > byteBudget)
        return;

    auto& state = *device->get_private_data<DeviceDataContainer>();
    std::lock_guard lock(state.indexBufferMutex);
    const auto size = static_cast<size_t>(desc.buffer.size);
    if (size > byteBudget - state.indexBufferBytes)
        return;

    const auto [it, inserted] = state.indexBuffers.try_emplace(buffer.handle);
    if (inserted) {
        const auto* bytes = static_cast<const uint8_t*>(initData->data);
        it->second.emplace_back().bytes.assign(bytes, bytes + size);
        state.indexBufferBytes += size;
    }
}

static void RecordIndexBufferWrite(command_list* cmd_list, resource dest, resource source = {},
                                   uint64_t sourceOffset = 0, uint64_t destOffset = 0, uint64_t size = 0) {
    auto* dev = cmd_list->get_device();
    const auto api = dev->get_api();
    if (!dest.handle || (api != device_api::d3d11 && api != device_api::d3d12))
        return;

    auto& state = *dev->get_private_data<DeviceDataContainer>();
    std::lock_guard lock(state.indexBufferMutex);
    ForgetIndexBuffer(state, dest.handle);
    if (api != device_api::d3d12)
        return;

    const auto target = state.d3d12Buffers.find(dest.handle);
    if (target == state.d3d12Buffers.end())
        return;
    if (target->second.upload) {
        target->second.uploaded = false;
        ++*target->second.uploadRevision;
        state.indexUploadsInvalidated = true;
        return;
    }

    auto& writes = cmd_list->get_private_data<CommandListDataContainer>()->d3d12IndexWrites[dest.handle];
    writes.serial = ++state.d3d12IndexSerial;
    target->second.latestWrite = writes.serial;
    const auto origin = state.d3d12Buffers.find(source.handle);
    const bool fromUpload = origin != state.d3d12Buffers.end() && origin->second.upload;
    if (size == UINT64_MAX)
        size = target->second.size;
    if (size == 0 || destOffset > target->second.size || size > target->second.size - destOffset) {
        writes.copies.clear();
        return;
    }
    std::optional<D3D12IndexBufferCopy> copy;
    if (fromUpload && sourceOffset <= origin->second.size && size <= origin->second.size - sourceOffset)
        copy = D3D12IndexBufferCopy{source, origin->second.id, sourceOffset, destOffset, size};
    writes.recordRange(destOffset, size, copy);
}

static bool onCopyIndexBufferResource(command_list* cmd_list, resource source, resource dest) {
    RecordIndexBufferWrite(cmd_list, dest, source, 0, 0, UINT64_MAX);
    return false;
}

static bool onCopyIndexBufferRegion(command_list* cmd_list, resource source, uint64_t sourceOffset,
                                    resource dest, uint64_t destOffset, uint64_t size) {
    RecordIndexBufferWrite(cmd_list, dest, source, sourceOffset, destOffset, size);
    return false;
}

static bool onUpdateIndexBufferCommand(command_list* cmd_list, const void*, resource dest, uint64_t, uint64_t) {
    RecordIndexBufferWrite(cmd_list, dest);
    return false;
}

static thread_local bool readingD3D12IndexUpload = false;

// Null alias barriers do not identify the affected buffer.
static void InvalidateAliasedIndexBuffers(DeviceDataContainer& state) {
    state.indexBuffers.clear();
    state.indexBufferBytes = 0;
    for (auto& [_, buffer] : state.d3d12Buffers) {
        buffer.latestWrite = 0;
        if (buffer.upload) {
            buffer.uploaded = false;
            ++*buffer.uploadRevision;
        }
    }
}

static void ReclaimInvalidIndexUploads(DeviceDataContainer& state) {
    if (!state.indexUploadsInvalidated)
        return;
    state.indexUploadsInvalidated = false;
    for (auto it = state.indexBuffers.begin(); it != state.indexBuffers.end();) {
        std::erase_if(it->second, [&state](const auto& snapshot) {
            if (snapshot.isCurrent())
                return false;
            state.indexBufferBytes -= snapshot.bytes.size();
            return true;
        });
        if (it->second.empty())
            it = state.indexBuffers.erase(it);
        else
            ++it;
    }
}

// Called under indexBufferMutex. Read only uploads the application has finished
// mapping. A later application map invalidates every snapshot from that upload.
static bool CaptureD3D12IndexUpload(DeviceDataContainer& state, resource source, uint64_t sourceOffset,
                                    uint64_t dest, uint64_t destOffset, uint64_t size) {
    const auto it = state.d3d12Buffers.find(source.handle);
    if (it == state.d3d12Buffers.end() || !it->second.uploaded || it->second.mapCount != 0 ||
        size == 0 || sourceOffset > it->second.size ||
        size > it->second.size - sourceOffset || size > DeviceDataContainer::indexBufferByteBudget ||
        sourceOffset > SIZE_MAX - size)
        return false;
    if (size > DeviceDataContainer::indexBufferByteBudget - state.indexBufferBytes) {
        ReclaimInvalidIndexUploads(state);
        if (size > DeviceDataContainer::indexBufferByteBudget - state.indexBufferBytes)
            return false;
    }

    DeviceDataContainer::IndexBufferSnapshot snapshot;
    snapshot.offset = destOffset;
    snapshot.bytes.resize(static_cast<size_t>(size));
    snapshot.uploadRevision = it->second.uploadRevision;
    snapshot.capturedRevision = *snapshot.uploadRevision;

    auto* native = reinterpret_cast<ID3D12Resource*>(source.handle);
    const D3D12_RANGE readRange{static_cast<SIZE_T>(sourceOffset), static_cast<SIZE_T>(sourceOffset + size)};
    const D3D12_RANGE noWrites{0, 0};
    void* mapped = nullptr;
    readingD3D12IndexUpload = true;
    const HRESULT result = native->Map(0, &readRange, &mapped);
    if (SUCCEEDED(result)) {
        std::memcpy(snapshot.bytes.data(), static_cast<const uint8_t*>(mapped) + readRange.Begin, snapshot.bytes.size());
        native->Unmap(0, &noWrites);
    }
    readingD3D12IndexUpload = false;
    if (FAILED(result))
        return false;

    state.indexBufferBytes += snapshot.bytes.size();
    state.indexBuffers[dest].push_back(std::move(snapshot));
    return true;
}

static void onExecuteIndexBufferCopies(command_queue* queue, command_list* cmd_list) {
    auto* dev = queue->get_device();
    if (dev->get_api() != device_api::d3d12)
        return;

    const auto& data = *cmd_list->get_private_data<CommandListDataContainer>();
    if (data.d3d12IndexWrites.empty() && !data.d3d12UnknownIndexAlias)
        return;
    auto& state = *dev->get_private_data<DeviceDataContainer>();
    std::lock_guard lock(state.indexBufferMutex);
    if (data.d3d12UnknownIndexAlias) {
        InvalidateAliasedIndexBuffers(state);
        return;
    }
    for (const auto& [dest, ranges] : data.d3d12IndexWrites) {
        ForgetIndexBuffer(state, dest);
        const auto target = state.d3d12Buffers.find(dest);
        if (target == state.d3d12Buffers.end() || target->second.latestWrite != ranges.serial)
            continue;
        for (const auto& copy : ranges.copies) {
            const auto origin = state.d3d12Buffers.find(copy.source.handle);
            if (origin != state.d3d12Buffers.end() && origin->second.id == copy.sourceId)
                CaptureD3D12IndexUpload(state, copy.source, copy.sourceOffset, dest, copy.destOffset, copy.size);
        }
    }
}

static void onD3D12IndexBinding(command_list* cmd_list, resource buffer, uint64_t offset, uint32_t indexSize) {
    if (cmd_list->get_device()->get_api() == device_api::d3d12)
        cmd_list->get_private_data<CommandListDataContainer>()->d3d12IndexBuffer = {buffer, offset, indexSize};
}

static void onD3D12IndexTopology(command_list* cmd_list, uint32_t count, const dynamic_state* states, const uint32_t* values) {
    if (cmd_list->get_device()->get_api() != device_api::d3d12)
        return;
    for (uint32_t i = 0; i < count; ++i) {
        if (states[i] != dynamic_state::primitive_topology)
            continue;
        auto& cut = cmd_list->get_private_data<CommandListDataContainer>()->d3d12IndexStripCut;
        switch (static_cast<primitive_topology>(values[i])) {
            case primitive_topology::undefined:
            case primitive_topology::line_strip:
            case primitive_topology::triangle_strip:
            case primitive_topology::line_strip_adj:
            case primitive_topology::triangle_strip_adj:
                // ReShade does not expose the D3D12 PSO's strip-cut value.
                cut = IndexStripCut::unknown;
                break;
            default:
                cut = IndexStripCut::disabled;
                break;
        }
    }
}

static void onExecuteIndexBufferBundle(command_list* cmd_list, command_list*) {
    if (cmd_list->get_device()->get_api() == device_api::d3d12) {
        auto& data = *cmd_list->get_private_data<CommandListDataContainer>();
        data.d3d12IndexBuffer = {};
        data.d3d12IndexStripCut = IndexStripCut::unknown;
    }
}

static void onIndexBufferBarrier(command_list* cmd_list, uint32_t count, const resource* buffers,
                                 const resource_usage* oldStates, const resource_usage* newStates) {
    if (cmd_list->get_device()->get_api() != device_api::d3d12)
        return;
    for (uint32_t i = 0; i < count; ++i) {
        // ReShade represents aliasing as an undefined-to-general barrier.
        const bool alias = oldStates[i] == resource_usage::undefined && newStates[i] == resource_usage::general;
        if (alias && !buffers[i].handle) {
            cmd_list->get_private_data<CommandListDataContainer>()->d3d12UnknownIndexAlias = true;
            auto& state = *cmd_list->get_device()->get_private_data<DeviceDataContainer>();
            std::lock_guard lock(state.indexBufferMutex);
            InvalidateAliasedIndexBuffers(state);
        } else if (alias || (newStates[i] & resource_usage::stream_output) != resource_usage::undefined) {
            RecordIndexBufferWrite(cmd_list, buffers[i]);
        }
    }
}

static bool onIndexCopyTextureToBuffer(command_list* cmd_list, resource, uint32_t, const subresource_box*,
                                       resource dest, uint64_t, uint32_t, uint32_t) {
    if (cmd_list->get_device()->get_api() == device_api::d3d12)
        RecordIndexBufferWrite(cmd_list, dest);
    return false;
}

static bool onIndexCopyQueryResults(command_list* cmd_list, query_heap, query_type, uint32_t, uint32_t,
                                    resource dest, uint64_t, uint32_t) {
    if (cmd_list->get_device()->get_api() == device_api::d3d12)
        RecordIndexBufferWrite(cmd_list, dest);
    return false;
}

static void onInitResource(device* device, const resource_desc& desc, const subresource_data* initData, resource_usage usage, reshade::api::resource handle) {
    RememberIndexBuffer(device, desc, initData, handle);
    resourceManager.OnInitResource(device, desc, initData, usage, handle);

    if (constantCopy != nullptr)
        constantCopy->OnInitResource(device, desc, initData, usage, handle);
}

static void onDestroyResource(device* device, resource res) {
    if (device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12) {
        auto& state = *device->get_private_data<DeviceDataContainer>();
        std::lock_guard lock(state.indexBufferMutex);
        ForgetIndexBuffer(state, res.handle);
        state.d3d12Buffers.erase(res.handle);
    }
    resourceManager.OnDestroyResource(device, res);

    if (constantCopy != nullptr)
        constantCopy->OnDestroyResource(device, res);
}

static bool onCreateResourceView(device* device, resource resource, resource_usage usage_type, resource_view_desc& desc) {
    return resourceManager.OnCreateResourceView(device, resource, usage_type, desc);
}

static void onInitResourceView(device* device, resource resource, resource_usage usage_type, const resource_view_desc& desc, resource_view view) {
    resourceManager.OnInitResourceView(device, resource, usage_type, desc, view);
}

static void onDestroyResourceView(device* device, resource_view view) {
    resourceManager.OnDestroyResourceView(device, view);
}

static void onReShadeReloadedEffects(effect_runtime* runtime) {
    RuntimeDataContainer& runtimeData = *runtime->get_private_data<RuntimeDataContainer>();
    DeviceDataContainer& deviceData = *runtime->get_device()->get_private_data<DeviceDataContainer>();

    {
        unique_lock<shared_mutex> renderLock(deviceData.render_mutex);
        deviceData.vulkanAutoPendingEffects.clear();
        deviceData.vulkanAutoWorkPending.store(false, std::memory_order_release);
    }

    techniqueManager.OnReShadeReloadedEffects(runtime);

    if (deviceData.current_runtime == runtime) {
        shared_lock<shared_mutex> techLock(runtimeData.technique_mutex);
        g_addonUIData.AssignPreferredGroupTechniques(runtimeData.allTechniques);
    }
}

static bool onReShadeSetTechniqueState(effect_runtime* runtime, effect_technique technique, bool enabled) {
    RuntimeDataContainer& data = *runtime->get_private_data<RuntimeDataContainer>();

    bool ret = techniqueManager.OnReShadeSetTechniqueState(runtime, technique, enabled);

    return ret;
}

static bool onReShadeReorderTechniques(effect_runtime* runtime, size_t count, effect_technique* techniques) {
    RuntimeDataContainer& runtimeData = *runtime->get_private_data<RuntimeDataContainer>();
    DeviceDataContainer& deviceData = *runtime->get_device()->get_private_data<DeviceDataContainer>();

    {
        unique_lock<shared_mutex> renderLock(deviceData.render_mutex);
        deviceData.vulkanAutoPendingEffects.clear();
        deviceData.vulkanAutoWorkPending.store(false, std::memory_order_release);
    }
    bool ret = techniqueManager.OnReShadeReorderTechniques(runtime, count, techniques);

    if (deviceData.current_runtime == runtime) {
        shared_lock<shared_mutex> techLock(runtimeData.technique_mutex);
        g_addonUIData.AssignPreferredGroupTechniques(runtimeData.allTechniques);
    }

    return ret;
}

static void onInitEffectRuntime(effect_runtime* runtime) {
    runtime->create_private_data<RuntimeDataContainer>();
    DeviceDataContainer& data = *runtime->get_device()->get_private_data<DeviceDataContainer>();

    keyMonitor.Init(runtime);
    renderingShaderManager.InitShaders(runtime->get_device());

    // Push new runtime on top
    runtimes.push_back(runtime);
    data.current_runtime = runtime;

    renderingBindingManager.InitTextureBingings(runtime);

    if (constantHandler != nullptr) {
        constantHandler->ReloadConstantVariables(runtime);
    }
}

static void onDestroyEffectRuntime(effect_runtime* runtime) {
    DeviceDataContainer& data = *runtime->get_device()->get_private_data<DeviceDataContainer>();

    device* dev = runtime->get_device();
    const bool otherRuntimeOnDevice = std::any_of(runtimes.begin(), runtimes.end(), [=](effect_runtime* other) {
        return other != runtime && other->get_device() == dev;
    });
    if (!otherRuntimeOnDevice) {
        Rendering::ResourceManager::SynchronizeGpu(runtime);
        renderingBindingManager.DisposeTextureBindings(dev, g_addonUIData.GetToggleGroups());
        if (dev->get_api() == device_api::d3d9) {
            // Adapted from the CrazyDaifu/BadassBaboon D3D9 reset fixes.
            groupResourceManager.DisposeGroupBuffers(dev, g_addonUIData.GetToggleGroups());
            resourceManager.OnDestroyDevice(dev, true);
            renderingShaderManager.DestroyShaders(dev);
        }
    }

    // Remove runtime from stack
    for (auto it = runtimes.begin(); it != runtimes.end();) {
        if (runtime == *it) {
            it = runtimes.erase(it);
            continue;
        }

        it++;
    }

    // Pick the runtime on top of our stack if there are any
    if (runtime == data.current_runtime) {
        const auto replacement = std::find_if(runtimes.rbegin(), runtimes.rend(), [=](effect_runtime* other) {
            return other->get_device() == dev;
        });
        if (replacement != runtimes.rend()) {
            data.current_runtime = *replacement;

            if (constantHandler != nullptr) {
                constantHandler->ReloadConstantVariables(data.current_runtime);
            }
        } else {
            data.current_runtime = nullptr;

            if (constantHandler != nullptr) {
                constantHandler->ClearConstantVariables();
            }
        }
    }

    runtime->destroy_private_data<RuntimeDataContainer>();
}

static void onInitPipeline(device* device, pipeline_layout, uint32_t subobjectCount, const pipeline_subobject* subobjects, pipeline pipelineHandle) {
    // shader has been created, we will now create a hash and store it with the handle we got.
    for (uint32_t i = 0; i < subobjectCount; ++i) {
        switch (subobjects[i].type) {
            case pipeline_subobject_type::vertex_shader: {
                g_vertexShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
            } break;
            case pipeline_subobject_type::pixel_shader: {
                g_pixelShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
            } break;
            case pipeline_subobject_type::compute_shader: {
                g_computeShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
            } break;
        }
    }
}

static void onDestroyPipeline(device* device, pipeline pipelineHandle) {
    g_pixelShaderManager.removeHandle(pipelineHandle.handle);
    g_vertexShaderManager.removeHandle(pipelineHandle.handle);
    g_computeShaderManager.removeHandle(pipelineHandle.handle);
}

static void onBindPipeline(command_list* commandList, pipeline_stage stages, pipeline pipelineHandle) {
    if (nullptr == commandList ||
        !((uint32_t)(stages & pipeline_stage::pixel_shader) || (uint32_t)(stages & pipeline_stage::vertex_shader) ||
          (uint32_t)(stages & pipeline_stage::compute_shader))) {
        return;
    }

    const uint32_t handleHasPixelShaderAttached =
      (uint32_t)(stages & pipeline_stage::pixel_shader) ? g_pixelShaderManager.safeGetShaderHash(pipelineHandle.handle) : 0;
    const uint32_t handleHasVertexShaderAttached =
      (uint32_t)(stages & pipeline_stage::vertex_shader) ? g_vertexShaderManager.safeGetShaderHash(pipelineHandle.handle) : 0;
    const uint32_t handleHasComputeShaderAttached =
      (uint32_t)(stages & pipeline_stage::compute_shader) ? g_computeShaderManager.safeGetShaderHash(pipelineHandle.handle) : 0;

    CommandListDataContainer& commandListData = *commandList->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = *commandList->get_device()->get_private_data<DeviceDataContainer>();

    if (deviceData.current_runtime == nullptr || !deviceData.current_runtime->get_effects_state()) {
        return;
    }

    uint32_t pipelineChanged = 0;

    if ((uint32_t)(stages & pipeline_stage::pixel_shader)) {
        if (g_activeCollectorFrameCounter > 0) {
            // in collection mode
            g_pixelShaderManager.addActivePipelineHandle(pipelineHandle.handle);
        }
        if (commandListData.ps.activeShaderHash != handleHasPixelShaderAttached) {
            pipelineChanged |= Rendering::MATCH_EFFECT_PS | Rendering::MATCH_BINDING_PS | Rendering::MATCH_PREVIEW_PS | Rendering::MATCH_CONST_PS;
            commandListData.ps.constantBuffersToUpdate.clear();
        }

        commandListData.ps.blockedShaderGroups = g_addonUIData.GetToggleGroupsForPixelShaderHash(handleHasPixelShaderAttached);
        commandListData.ps.activeShaderHash = handleHasPixelShaderAttached;
    }

    if ((uint32_t)(stages & pipeline_stage::vertex_shader)) {
        if (g_activeCollectorFrameCounter > 0) {
            // in collection mode
            g_vertexShaderManager.addActivePipelineHandle(pipelineHandle.handle);
        }
        if (commandListData.vs.activeShaderHash != handleHasVertexShaderAttached) {
            pipelineChanged |= Rendering::MATCH_EFFECT_VS | Rendering::MATCH_BINDING_VS | Rendering::MATCH_PREVIEW_VS | Rendering::MATCH_CONST_VS;
            commandListData.vs.constantBuffersToUpdate.clear();
        }

        commandListData.vs.blockedShaderGroups = g_addonUIData.GetToggleGroupsForVertexShaderHash(handleHasVertexShaderAttached);
        commandListData.vs.activeShaderHash = handleHasVertexShaderAttached;
    }

    if ((uint32_t)(stages & pipeline_stage::compute_shader)) {
        if (g_activeCollectorFrameCounter > 0) {
            // in collection mode
            g_computeShaderManager.addActivePipelineHandle(pipelineHandle.handle);
        }
        if (commandListData.cs.activeShaderHash != handleHasComputeShaderAttached) {
            pipelineChanged |= Rendering::MATCH_EFFECT_CS | Rendering::MATCH_BINDING_CS | Rendering::MATCH_PREVIEW_CS | Rendering::MATCH_CONST_CS;
            commandListData.cs.constantBuffersToUpdate.clear();
        }

        commandListData.cs.blockedShaderGroups = g_addonUIData.GetToggleGroupsForComputeShaderHash(handleHasComputeShaderAttached);
        commandListData.cs.activeShaderHash = handleHasComputeShaderAttached;
    }

    if (pipelineChanged > 0) {
        // Perform updates scheduled for after a shader has been applied. Only do so once the draw flag has been cleared.
        if (commandListData.commandQueue & Rendering::CHECK_MATCH_BIND_PIPELINE_PREVIEW &&
            !(commandListData.commandQueue & pipelineChanged & Rendering::MATCH_PREVIEW)) {
            renderingPreviewManager.UpdatePreview(commandList, Rendering::CALL_BIND_PIPELINE, pipelineChanged & Rendering::MATCH_PREVIEW);
        }

        if (commandListData.commandQueue & Rendering::CHECK_MATCH_BIND_PIPELINE_BINDING &&
            !(commandListData.commandQueue & pipelineChanged & Rendering::MATCH_BINDING)) {
            renderingBindingManager.UpdateTextureBindings(commandList, Rendering::CALL_BIND_PIPELINE, pipelineChanged & Rendering::MATCH_BINDING);
        }

        if (commandListData.commandQueue & Rendering::CHECK_MATCH_BIND_PIPELINE_EFFECT &&
            !(commandListData.commandQueue & pipelineChanged & Rendering::MATCH_EFFECT)) {
            renderingEffectManager.RenderEffects(commandList, Rendering::CALL_BIND_PIPELINE, pipelineChanged & Rendering::MATCH_EFFECT);
        }

        renderingQueueManager.ClearQueue(commandListData, pipelineChanged);
        renderingQueueManager.CheckCallForCommandList(commandList);
    }
}

static void onBindRenderTargetsAndDepthStencil(command_list* cmd_list, uint32_t count, const resource_view* rtvs, resource_view dsv) {
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr) {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = *cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = *device->get_private_data<DeviceDataContainer>();

    // if (count > 0)
    //{
    if (commandListData.commandQueue & Rendering::CHECK_MATCH_BIND_RENDERTARGET_PREVIEW &&
        !(commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW_PREVIEW)) {
        renderingPreviewManager.UpdatePreview(cmd_list, Rendering::CALL_BIND_RENDER_TARGET, Rendering::MATCH_PREVIEW);
    }

    if (commandListData.commandQueue & Rendering::CHECK_MATCH_BIND_RENDERTARGET_BINDING &&
        !(commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW_BINDING)) {
        renderingBindingManager.UpdateTextureBindings(cmd_list, Rendering::CALL_BIND_RENDER_TARGET, Rendering::MATCH_BINDING);
    }

    if (commandListData.commandQueue & Rendering::CHECK_MATCH_BIND_RENDERTARGET_EFFECT &&
        !(commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW_EFFECT)) {
        renderingEffectManager.RenderEffects(cmd_list, Rendering::CALL_BIND_RENDER_TARGET, Rendering::MATCH_EFFECT);
    }

    renderingQueueManager.RescheduleGroups(commandListData, deviceData);
    //}
}

static void onBarrier(command_list* cmd_list,
                      uint32_t count,
                      const resource* resources,
                      const resource_usage* oldStates,
                      const resource_usage* newStates) {
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr ||
        cmd_list->get_device()->get_api() != device_api::vulkan) {
        return;
    }

    CommandListDataContainer& commandListData = *cmd_list->get_private_data<CommandListDataContainer>();

    // An end_render_pass callback may describe either a real render-pass end or the
    // first half of vkCmdNextSubpass. Seeing a subsequent barrier proves the actual
    // render pass has ended, because there is no command-recording opportunity between
    // ReShade's paired end/begin callbacks for a subpass transition.
    if (commandListData.vulkanRenderPassEndPending) {
        commandListData.vulkanInsideRenderPass = false;
        commandListData.vulkanRenderPassEndPending = false;
    }

    if (commandListData.vulkanAutoInjectionActive || commandListData.vulkanInsideRenderPass ||
        !cmd_list->get_device()->get_private_data<DeviceDataContainer>()->vulkanAutoWorkPending.load(std::memory_order_acquire))
        return;

    renderingEffectManager.RenderDeferredVulkanAutoEffectsAfterBarrier(cmd_list, count, resources, oldStates, newStates);
}

static bool onBeginRenderPass(command_list* cmd_list,
                              uint32_t count,
                              const render_pass_render_target_desc* rts,
                              const render_pass_depth_stencil_desc* ds,
                              render_pass_flags flags) {
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr) {
        return false;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = *cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = *device->get_private_data<DeviceDataContainer>();
    if (device->get_api() == device_api::d3d12)
        commandListData.d3d12InsideRenderPass = true;
    const bool vulkan = device->get_api() == device_api::vulkan;
    const bool resuming = vulkan && (flags & render_pass_flags::resume) != render_pass_flags::none;
    const bool suspending = vulkan && (flags & render_pass_flags::suspend) != render_pass_flags::none;

    if (deviceData.current_runtime == nullptr || !deviceData.current_runtime->get_effects_state()) {
        if (vulkan) {
            commandListData.vulkanInsideRenderPass = true;
            commandListData.vulkanRenderPassEndPending = false;
            commandListData.vulkanRenderPassSuspends = suspending;
        }
        return false;
    }

    if (vulkan) {
        // ReShade API 20 exposes suspended/resumed dynamic-rendering segments
        // explicitly. A resumed segment is a continuation of the same logical pass,
        // so it can never be used as a safe point for preview/effect transfer work.

        // ReShade also emits begin_render_pass around vkCmdNextSubpass. In that case
        // Vulkan is still inside the original render pass, so effect/preview transfer
        // work is not legal here. Only treat a begin callback as a safe boundary when
        // REST has positively tracked the command list as being outside a render pass.
        const bool safeVulkanBoundary = !commandListData.vulkanInsideRenderPass && !resuming;
        if (safeVulkanBoundary &&
            deviceData.vulkanPreviewWorkPending.load(std::memory_order_acquire))
            renderingPreviewManager.CaptureDeferredVulkanPreview(cmd_list);

        // Vulkan does not emit bind_render_targets_and_depth_stencil events for render
        // pass attachments. Mirror the begin_render_pass descriptors into REST's state
        // tracker so a marked draw can resolve the live primary colour target.
        state_tracking& trackedState = *cmd_list->get_private_data<state_tracking>();
        trackedState.render_targets.clear();
        trackedState.render_targets.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
            trackedState.render_targets.push_back(rts[i].view);
        trackedState.depth_stencil = ds != nullptr ? ds->view : resource_view{ 0 };

        // For a true new render pass this callback occurs before vkCmdBeginRenderPass
        // or vkCmdBeginRendering and is safe. Subpass transitions are intentionally
        // skipped and remain pending for a proven post-pass boundary.
        if (safeVulkanBoundary &&
            deviceData.vulkanAutoWorkPending.load(std::memory_order_acquire))
            renderingEffectManager.RenderDeferredVulkanAutoEffects(cmd_list, count, rts);

        commandListData.vulkanInsideRenderPass = true;
        commandListData.vulkanRenderPassEndPending = false;
        commandListData.vulkanRenderPassSuspends = suspending;
    }

    if (commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW_BINDING) {
        renderingBindingManager.UpdateTextureBindings(cmd_list, Rendering::CALL_DRAW, Rendering::MATCH_BINDING_PS | Rendering::MATCH_BINDING_VS);
    }

    if (commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW_EFFECT) {
        renderingEffectManager.RenderEffects(cmd_list, Rendering::CALL_DRAW, Rendering::MATCH_EFFECT_PS | Rendering::MATCH_EFFECT_VS);
    }

    return false;
}

static bool onEndRenderPass(command_list* cmd_list) {
    if (cmd_list != nullptr && cmd_list->get_device()->get_api() == device_api::d3d12)
        cmd_list->get_private_data<CommandListDataContainer>()->d3d12InsideRenderPass = false;
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr ||
        cmd_list->get_device()->get_api() != device_api::vulkan) {
        return false;
    }

    CommandListDataContainer& commandListData = *cmd_list->get_private_data<CommandListDataContainer>();

    if (commandListData.vulkanRenderPassSuspends) {
        // API 20 tells us this dynamic-rendering segment is intentionally suspended.
        // Its matching resume begins the same logical pass, so do not arm a false
        // post-pass boundary between the two segments.
        commandListData.vulkanInsideRenderPass = true;
        commandListData.vulkanRenderPassEndPending = false;
        commandListData.vulkanRenderPassSuspends = false;
        return false;
    }

    // Traditional Vulkan subpass transitions still surface as end/begin event pairs
    // without a dedicated subpass flag. Retain the proven conservative fallback:
    // stay inside the pass until a later barrier proves that the render pass ended.
    commandListData.vulkanInsideRenderPass = true;
    commandListData.vulkanRenderPassEndPending = true;
    return false;
}

static void onReShadeOverlay(effect_runtime* runtime) {
    std::lock_guard previewLock(runtime->get_device()->get_private_data<DeviceDataContainer>()->previewMutex);
    DisplayOverlay(g_addonUIData, resourceManager, runtime);
}

static void onPresent(command_queue* queue,
                      swapchain* swapchain,
                      const rect* source_rect,
                      const rect* dest_rect,
                      uint32_t dirty_rect_count,
                      const rect* dirty_rects) {
    device* dev = queue->get_device();
    DeviceDataContainer& deviceData = *dev->get_private_data<DeviceDataContainer>();

    if (deviceData.current_runtime == nullptr) {
        return;
    }

    effect_runtime* runtime = deviceData.current_runtime;

    if (queue == runtime->get_command_queue()) {
        if (runtime->get_effects_state()) {
            renderingEffectManager.RenderRemainingEffects(runtime);
        }
    }

    if (dev->get_api() != device_api::d3d12 && dev->get_api() != device_api::vulkan)
        onResetCommandList(runtime->get_command_queue()->get_immediate_command_list());
}

static void onReShadePresent(effect_runtime* runtime) {
    device* dev = runtime->get_device();
    DeviceDataContainer& deviceData = *dev->get_private_data<DeviceDataContainer>();
    std::lock_guard previewLock(deviceData.previewMutex);
    command_queue* queue = runtime->get_command_queue();

    {
        unique_lock<shared_mutex> renderLock(deviceData.render_mutex);
        deviceData.vulkanAutoPendingEffects.clear();
        deviceData.vulkanAutoWorkPending.store(false, std::memory_order_release);
    }

    deviceData.rendered_effects = false;

    keyMonitor.PollKeyStates(runtime);

    if (g_addonUIData.GetPreventRuntimeReload()) {
        renderingEffectManager.PreventRuntimeReload(runtime, queue->get_immediate_command_list());
    }

    if (runtime->get_effects_state()) {
        resourceManager.CheckPreview(queue->get_immediate_command_list(), dev);
        groupResourceManager.CheckGroupBuffers(runtime, g_addonUIData.GetToggleGroups());
        renderingBindingManager.ClearUnmatchedTextureBindings(runtime->get_command_queue()->get_immediate_command_list());
        resourceManager.CheckResourceViews(runtime);
    }

    techniqueManager.OnReShadePresent(runtime);

    deviceData.bindingsUpdated.clear();
    deviceData.constantsUpdated.clear();
    renderingPreviewManager.CancelDeferredVulkanPreview(dev);
    deviceData.huntPreview.Reset();

    CheckHotkeys(g_addonUIData, runtime);
}

static void onMapBufferRegion(device* device, resource resource, uint64_t offset, uint64_t size, map_access access, void** data) {
    if (readingD3D12IndexUpload)
        return;
    if ((device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12) &&
        access != map_access::read_only) {
        auto& state = *device->get_private_data<DeviceDataContainer>();
        std::lock_guard lock(state.indexBufferMutex);
        ForgetIndexBuffer(state, resource.handle);
        const auto it = state.d3d12Buffers.find(resource.handle);
        if (it != state.d3d12Buffers.end() && it->second.upload) {
            ++it->second.mapCount;
            ++*it->second.uploadRevision;
            state.indexUploadsInvalidated = true;
        }
    }
    if (constantCopy != nullptr)
        constantCopy->OnMapBufferRegion(device, resource, offset, size, access, data);
}

static void onUnmapBufferRegion(device* device, resource resource) {
    if (readingD3D12IndexUpload)
        return;
    if (device->get_api() == device_api::d3d12) {
        auto& state = *device->get_private_data<DeviceDataContainer>();
        std::lock_guard lock(state.indexBufferMutex);
        const auto it = state.d3d12Buffers.find(resource.handle);
        if (it != state.d3d12Buffers.end() && it->second.mapCount != 0) {
            if (--it->second.mapCount == 0)
                it->second.uploaded = true;
        }
    }
    if (constantCopy != nullptr)
        constantCopy->OnUnmapBufferRegion(device, resource);
}

static bool onUpdateBufferRegion(device* device, const void* data, resource resource, uint64_t offset, uint64_t size) {
    if (device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12) {
        auto& state = *device->get_private_data<DeviceDataContainer>();
        std::lock_guard lock(state.indexBufferMutex);
        ForgetIndexBuffer(state, resource.handle);
        const auto it = state.d3d12Buffers.find(resource.handle);
        if (it != state.d3d12Buffers.end()) {
            it->second.latestWrite = 0;
            if (it->second.upload) {
                it->second.uploaded = false;
                ++*it->second.uploadRevision;
                state.indexUploadsInvalidated = true;
            }
        }
    }
    if (constantCopy != nullptr)
        constantCopy->OnUpdateBufferRegion(device, data, resource, offset, size);

    return false;
}

static void displayRestTab(effect_runtime* runtime) {
    std::lock_guard previewLock(runtime->get_device()->get_private_data<DeviceDataContainer>()->previewMutex);
    DisplayRestTab(g_addonUIData, runtime);
}

static void displayAddonsTab(effect_runtime*) {
    DisplayAddonsTab();
}

static void Init() {
    resourceManager.SetResourceShim(g_addonUIData.GetResourceShim());
    resourceManager.Init();
    constantManager.Init(g_addonUIData, groupResourceManager, &constantCopy, &constantHandler);

    g_addonUIData.AddToggleGroupRemovalCallback(
      std::bind(&Rendering::ToggleGroupResourceManager::ToggleGroupRemoved, &groupResourceManager, std::placeholders::_1, std::placeholders::_2));
    techniqueManager.AddEffectsReloadingCallback(std::bind(&Shim::Constants::ConstantHandlerBase::OnEffectsReloading, constantHandler, std::placeholders::_1));
    techniqueManager.AddEffectsReloadedCallback(std::bind(&Shim::Constants::ConstantHandlerBase::OnEffectsReloaded, constantHandler, std::placeholders::_1));
    techniqueManager.AddEffectsReloadingCallback(std::bind(&Rendering::ResourceManager::OnEffectsReloading, &resourceManager, std::placeholders::_1));
    techniqueManager.AddEffectsReloadedCallback(std::bind(&Rendering::ResourceManager::OnEffectsReloaded, &resourceManager, std::placeholders::_1));
}

static void UnInit() {
    constantManager.UnInit();
}

static void CheckDrawCall(command_list* cmd_list, const uint64_t match_modifier = Rendering::MATCH_ALL) {
    CommandListDataContainer& commandListData = *cmd_list->get_private_data<CommandListDataContainer>();

    if (commandListData.commandQueue & Rendering::MATCH_ALL & match_modifier) {
        if (constantHandler != nullptr && (commandListData.commandQueue & Rendering::MATCH_CONST & match_modifier)) {
            constantHandler->UpdateConstants(cmd_list);
            commandListData.commandQueue &= ~(Rendering::MATCH_CONST & match_modifier);
        }

        if (commandListData.commandQueue & Rendering::MATCH_PREVIEW & match_modifier) {
            renderingPreviewManager.UpdatePreview(cmd_list, Rendering::CALL_DRAW, Rendering::MATCH_PREVIEW & match_modifier);
        }

        if (commandListData.commandQueue & Rendering::MATCH_BINDING & match_modifier) {
            renderingBindingManager.UpdateTextureBindings(cmd_list, Rendering::CALL_DRAW, Rendering::MATCH_BINDING & match_modifier);
        }

        if (commandListData.commandQueue & Rendering::MATCH_EFFECT & match_modifier) {
            renderingEffectManager.RenderEffects(cmd_list, Rendering::CALL_DRAW, Rendering::MATCH_EFFECT & match_modifier);
        }
    }
}

static void ClearSuppressedCallState(CommandListDataContainer& commandListData, uint64_t matchModifier, bool clearBlockedGroups = true) {
    auto clearStage = [clearBlockedGroups](ShaderData& stage) {
        stage.bindingsToUpdate.clear();
        stage.constantBuffersToUpdate.clear();
        stage.techniquesToRender.clear();
        stage.srvToUpdate.clear();
        if (clearBlockedGroups)
            stage.blockedShaderGroups.clear();
    };

    uint64_t clearMask = 0;
    if ((matchModifier & Rendering::MATCH_PS) != 0) {
        clearStage(commandListData.ps);
        clearMask |= Rendering::MATCH_PS;
    }
    if ((matchModifier & Rendering::MATCH_VS) != 0) {
        clearStage(commandListData.vs);
        clearMask |= Rendering::MATCH_VS;
    }
    if ((matchModifier & Rendering::MATCH_CS) != 0) {
        clearStage(commandListData.cs);
        clearMask |= Rendering::MATCH_CS;
    }

    commandListData.commandQueue &= ~(clearMask |
                                      (clearMask << Rendering::MATCH_DELIMITER) |
                                      (clearMask << (2 * Rendering::MATCH_DELIMITER)));
}

struct DrawSuppression {
    bool hideAll = false;
    std::vector<ShaderToggler::ScreenRegion> regions;
};

struct DrawGeometry {
    bool indexed;
    uint32_t count;
    uint32_t instances;
    std::optional<uint32_t> indexedVertexCount;
};

static void EvaluateShaderHiding(const ShaderData& stage, uint32_t stageIndex, const DrawGeometry* geometry, DrawSuppression& result) {
    if (stage.activeShaderHash == 0 || result.hideAll)
        return;

    for (const auto* group : stage.blockedShaderGroups) {
        if (group == nullptr || !group->isActive())
            continue;
        const auto settings = group->FindShaderDrawSettings(stageIndex, stage.activeShaderHash);
        if (!settings || !settings->suppressDrawCall) {
            if (stageIndex < 2 && group->getHideMarkedShaders()) {
                result.hideAll = true;
                return;
            }
            continue;
        }
        if (settings->hasGeometryFilter() &&
            (geometry == nullptr || !settings->matchesDrawGeometry(geometry->indexed, geometry->count, geometry->instances,
                                                                  geometry->indexedVertexCount)))
            continue;
        if (stageIndex < 2 && settings->spatialFilterEnabled) {
            if (geometry != nullptr)
                result.regions.insert(result.regions.end(), settings->spatialRegions.begin(), settings->spatialRegions.end());
            continue;
        }
        result.hideAll = true;
        return;
    }
}

static void subtractScreenRect(const reshade::api::rect& cut, std::vector<reshade::api::rect>& pieces) {
    std::vector<reshade::api::rect> remaining;
    remaining.reserve(pieces.size() + 4);

    for (const reshade::api::rect& piece : pieces) {
        const int32_t left = std::max(piece.left, cut.left);
        const int32_t top = std::max(piece.top, cut.top);
        const int32_t right = std::min(piece.right, cut.right);
        const int32_t bottom = std::min(piece.bottom, cut.bottom);
        if (left >= right || top >= bottom) {
            remaining.push_back(piece);
            continue;
        }

        if (piece.left < cut.left)
            remaining.push_back(reshade::api::rect{ piece.left, piece.top, cut.left, piece.bottom });
        if (cut.right < piece.right)
            remaining.push_back(reshade::api::rect{ cut.right, piece.top, piece.right, piece.bottom });
        if (piece.top < cut.top)
            remaining.push_back(reshade::api::rect{ left, piece.top, right, cut.top });
        if (cut.bottom < piece.bottom)
            remaining.push_back(reshade::api::rect{ left, cut.bottom, right, piece.bottom });
    }

    pieces = std::move(remaining);
}

// Unconditional but throttled diagnostics for the screen-selection filter, so a failing setup can
// be diagnosed from ReShade.log without enabling the full diagnostic log.
// D3D11 needs the scissor test enabled in the rasterizer state; games often keep it disabled for
// 3D draws, so cache one variant per rasterizer description with the scissor test turned on.
struct ScissorRasterizerStateEntry {
    ID3D11Device* device;
    D3D11_RASTERIZER_DESC desc;
    ID3D11RasterizerState* state;
};

static std::mutex s_scissorRasterizerStateMutex;
static std::vector<ScissorRasterizerStateEntry> s_scissorRasterizerStates;

static ID3D11RasterizerState* acquireScissorEnabledRasterizerState(ID3D11Device* device, const D3D11_RASTERIZER_DESC& desc) {
    if (device == nullptr)
        return nullptr;

    std::lock_guard<std::mutex> lock(s_scissorRasterizerStateMutex);
    for (const ScissorRasterizerStateEntry& entry : s_scissorRasterizerStates) {
        if (entry.device == device && std::memcmp(&entry.desc, &desc, sizeof(desc)) == 0)
            return entry.state;
    }

    D3D11_RASTERIZER_DESC scissorDesc = desc;
    scissorDesc.ScissorEnable = TRUE;

    ID3D11RasterizerState* state = nullptr;
    if (FAILED(device->CreateRasterizerState(&scissorDesc, &state)) || state == nullptr)
        return nullptr;

    s_scissorRasterizerStates.push_back({ device, desc, state });
    return state;
}

static void releaseScissorEnabledRasterizerStates(ID3D11Device* device) {
    std::lock_guard<std::mutex> lock(s_scissorRasterizerStateMutex);
    for (auto it = s_scissorRasterizerStates.begin(); it != s_scissorRasterizerStates.end();) {
        if (device == nullptr || it->device == device) {
            it->state->Release();
            it = s_scissorRasterizerStates.erase(it);
        } else {
            ++it;
        }
    }
}

// Re-records the draw with the scissor set to the part of the draw's area that is outside the
// selected boxes. Returns true when the original draw must stay suppressed (clipped draws were
// recorded or the boxes cover the whole draw); false when no box touches the draw.
static bool clipDrawToScreenRegions(command_list* cmd_list,
                                    const DrawSuppression& suppression,
                                    bool indexed,
                                    uint32_t count,
                                    uint32_t instanceCount,
                                    uint32_t first,
                                    int32_t vertexOffset,
                                    uint32_t firstInstance) {
    if (count == 0 || instanceCount == 0)
        return false;

    // D3D12 always enables the scissor test and supports several scissor rectangles in one draw.
    // D3D11 needs one draw per rectangle and a rasterizer state with the scissor test enabled.
    const reshade::api::device_api api = cmd_list->get_device()->get_api();
    if (api != reshade::api::device_api::d3d12 && api != reshade::api::device_api::d3d11) {
        return false;
    }

    const bool d3d11 = api == device_api::d3d11;
    auto* context = d3d11 ? reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native()) : nullptr;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> originalRasterizer;
    D3D11_RASTERIZER_DESC rasterizerDesc{};
    reshade::api::viewport viewport{};
    reshade::api::rect savedScissor{};
    UINT scissorCount = 1;
    if (d3d11) {
        if (context == nullptr)
            return false;

        // Read the effective native state, including the default null rasterizer.
        context->RSGetState(originalRasterizer.GetAddressOf());
        if (originalRasterizer) {
            originalRasterizer->GetDesc(&rasterizerDesc);
        } else {
            rasterizerDesc.FillMode = D3D11_FILL_SOLID;
            rasterizerDesc.CullMode = D3D11_CULL_BACK;
            rasterizerDesc.DepthClipEnable = TRUE;
        }

        UINT viewportCount = 0;
        context->RSGetViewports(&viewportCount, nullptr);
        context->RSGetScissorRects(&scissorCount, nullptr);
        if (viewportCount != 1 || scissorCount > 1 || (rasterizerDesc.ScissorEnable && scissorCount == 0))
            return false;
        D3D11_VIEWPORT nativeViewport{};
        context->RSGetViewports(&viewportCount, &nativeViewport);
        viewport = {nativeViewport.TopLeftX, nativeViewport.TopLeftY, nativeViewport.Width, nativeViewport.Height,
                    nativeViewport.MinDepth, nativeViewport.MaxDepth};
        if (scissorCount == 1) {
            D3D11_RECT rect{};
            context->RSGetScissorRects(&scissorCount, &rect);
            savedScissor = {rect.left, rect.top, rect.right, rect.bottom};
        }
    } else {
        auto* state = cmd_list->get_private_data<state_tracking>();
        if (state == nullptr || state->viewports.size() != 1 || state->scissor_rects.size() != 1)
            return false;
        viewport = state->viewports.front();
        savedScissor = state->scissor_rects.front();
    }

    if (viewport.width <= 0.0f || viewport.height <= 0.0f)
        return false;

    const auto toPixelX = [&viewport](float value) {
        return static_cast<int32_t>(std::lround(viewport.x + value * viewport.width));
    };
    const auto toPixelY = [&viewport](float value) {
        return static_cast<int32_t>(std::lround(viewport.y + value * viewport.height));
    };

    const reshade::api::rect viewportRect{toPixelX(0.0f), toPixelY(0.0f), toPixelX(1.0f), toPixelY(1.0f)};
    reshade::api::rect base = d3d11 && !rasterizerDesc.ScissorEnable ? viewportRect : savedScissor;
    base.left = std::max(base.left, viewportRect.left);
    base.top = std::max(base.top, viewportRect.top);
    base.right = std::min(base.right, viewportRect.right);
    base.bottom = std::min(base.bottom, viewportRect.bottom);
    if (base.right <= base.left || base.bottom <= base.top) {
        return false;
    }

    std::vector<reshade::api::rect> pieces{ base };
    uint32_t intersectingBoxes = 0;
    for (const ShaderToggler::ScreenRegion& region : suppression.regions) {
        reshade::api::rect box{ toPixelX(region.minX), toPixelY(region.minY), toPixelX(region.maxX), toPixelY(region.maxY) };
        box.left = std::clamp(box.left, base.left, base.right);
        box.top = std::clamp(box.top, base.top, base.bottom);
        box.right = std::clamp(box.right, base.left, base.right);
        box.bottom = std::clamp(box.bottom, base.top, base.bottom);
        if (box.right <= box.left || box.bottom <= box.top)
            continue;

        ++intersectingBoxes;
        subtractScreenRect(box, pieces);
        if (pieces.empty())
            break;
        if (pieces.size() > 64)
            return false;
    }

    if (intersectingBoxes == 0)
        return false;
    if (pieces.empty())
        return true;

    const bool changedRasterizer = d3d11 && !rasterizerDesc.ScissorEnable;
    if (changedRasterizer) {
        auto* device = reinterpret_cast<ID3D11Device*>(cmd_list->get_device()->get_native());
        auto* scissorState = acquireScissorEnabledRasterizerState(device, rasterizerDesc);
        if (scissorState == nullptr)
            return false;
        context->RSSetState(scissorState);
    }

    for (const reshade::api::rect& piece : pieces) {
        cmd_list->bind_scissor_rects(0, 1, &piece);
        if (indexed)
            cmd_list->draw_indexed(count, instanceCount, first, vertexOffset, firstInstance);
        else
            cmd_list->draw(count, instanceCount, first, firstInstance);
    }

    if (scissorCount == 0)
        context->RSSetScissorRects(0, nullptr);
    else
        cmd_list->bind_scissor_rects(0, 1, &savedScissor);
    if (changedRasterizer)
        context->RSSetState(originalRasterizer.Get());

    return true;
}

static bool ShouldSuppressVulkanHuntedCall(command_list* cmd_list, uint64_t matchModifier) {
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr || cmd_list->get_device()->get_api() != device_api::vulkan)
        return false;

    CommandListDataContainer& commandListData = *cmd_list->get_private_data<CommandListDataContainer>();

    const bool huntingPS = g_pixelShaderManager.isInHuntingMode();
    const bool huntingVS = g_vertexShaderManager.isInHuntingMode();
    const bool huntingCS = g_computeShaderManager.isInHuntingMode();
    const uint32_t huntedPS = g_pixelShaderManager.getActiveHuntedShaderHash();
    const uint32_t huntedVS = g_vertexShaderManager.getActiveHuntedShaderHash();
    const uint32_t huntedCS = g_computeShaderManager.getActiveHuntedShaderHash();

    const bool suppressPS =
      (matchModifier & Rendering::MATCH_PS) != 0 &&
      huntingPS && huntedPS != 0 &&
      commandListData.ps.activeShaderHash == huntedPS;

    const bool suppressVS =
      (matchModifier & Rendering::MATCH_VS) != 0 &&
      huntingVS && huntedVS != 0 &&
      commandListData.vs.activeShaderHash == huntedVS;

    const bool suppressCS =
      (matchModifier & Rendering::MATCH_CS) != 0 &&
      huntingCS && huntedCS != 0 &&
      commandListData.cs.activeShaderHash == huntedCS;

    if (!suppressPS && !suppressVS && !suppressCS)
        return false;

    // Vulkan hunting keeps the visualisation safe by suppressing the whole draw,
    // but still records the candidate target so the preview can be copied later
    // at a legal render-pass boundary.
    if (suppressPS)
        renderingPreviewManager.RecordVulkanHuntedTarget(cmd_list, 0, commandListData.ps.activeShaderHash);
    else if (suppressVS)
        renderingPreviewManager.RecordVulkanHuntedTarget(cmd_list, 1, commandListData.vs.activeShaderHash);
    else if (suppressCS)
        renderingPreviewManager.RecordVulkanHuntedTarget(cmd_list, 2, commandListData.cs.activeShaderHash);

    // The entire draw/dispatch is suppressed, so discard queued REST work for
    // every shader stage participating in that skipped call, not only the stage
    // whose hunted hash triggered the suppression. This prevents stale VS/PS work
    // from leaking into the next unsuppressed draw.
    ClearSuppressedCallState(commandListData, matchModifier);
    return true;
}

static bool HandleGraphicsDraw(command_list* cmd_list, const DrawGeometry* geometry,
                               uint32_t first = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) {
    constexpr uint64_t stages = Rendering::MATCH_PS | Rendering::MATCH_VS;
    auto& cmdData = *cmd_list->get_private_data<CommandListDataContainer>();
    if (geometry != nullptr) {
        g_pixelShaderManager.recordDrawGeometry(cmdData.ps.activeShaderHash, geometry->indexed, geometry->count,
                                               geometry->instances, geometry->indexedVertexCount);
        g_vertexShaderManager.recordDrawGeometry(cmdData.vs.activeShaderHash, geometry->indexed, geometry->count,
                                                geometry->instances, geometry->indexedVertexCount);
    }
    if (ShouldSuppressVulkanHuntedCall(cmd_list, stages))
        return true;

    DrawSuppression suppression;
    EvaluateShaderHiding(cmdData.ps, 0, geometry, suppression);
    EvaluateShaderHiding(cmdData.vs, 1, geometry, suppression);
    if (suppression.hideAll) {
        ClearSuppressedCallState(cmdData, stages, false);
        return true;
    }
    CheckDrawCall(cmd_list, stages);
    return geometry != nullptr && !suppression.regions.empty() &&
           clipDrawToScreenRegions(cmd_list, suppression, geometry->indexed, geometry->count, geometry->instances,
                                   first, vertexOffset, firstInstance);
}

static bool HandleComputeDispatch(command_list* cmd_list) {
    if (ShouldSuppressVulkanHuntedCall(cmd_list, Rendering::MATCH_CS))
        return true;

    auto& cmdData = *cmd_list->get_private_data<CommandListDataContainer>();
    DrawSuppression suppression;
    EvaluateShaderHiding(cmdData.cs, 2, nullptr, suppression);
    if (suppression.hideAll) {
        ClearSuppressedCallState(cmdData, Rendering::MATCH_CS, false);
        return true;
    }
    CheckDrawCall(cmd_list, Rendering::MATCH_CS);
    return false;
}

static bool onDraw(command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) {
    const DrawGeometry geometry{false, vertex_count, instance_count, std::nullopt};
    return HandleGraphicsDraw(cmd_list, &geometry, first_vertex, 0, first_instance);
}

static bool onDispatch(command_list* cmd_list, uint32_t, uint32_t, uint32_t) {
    return HandleComputeDispatch(cmd_list);
}

static bool NeedsIndexedVertexCount(const ShaderData& stage, uint32_t stageIndex, const ShaderToggler::ShaderManager& manager) {
    if (stage.activeShaderHash == 0)
        return false;
    if (manager.isInHuntingMode() && manager.getActiveHuntedShaderHash() == stage.activeShaderHash)
        return true;

    for (const auto* group : stage.blockedShaderGroups) {
        if (group == nullptr || !group->isActive())
            continue;
        const auto settings = group->FindShaderDrawSettings(stageIndex, stage.activeShaderHash);
        if (settings && settings->suppressDrawCall && settings->matchVertexCount && !settings->matchIndexCount)
            return true;
    }
    return false;
}

static std::optional<uint32_t> GetIndexedVertexCount(command_list* cmd_list, uint32_t firstIndex, uint32_t indexCount) {
    auto* dev = cmd_list->get_device();
    const auto api = dev->get_api();
    if (api != device_api::d3d11 && api != device_api::d3d12)
        return std::nullopt;
    const auto& drawState = *cmd_list->get_private_data<CommandListDataContainer>();
    if (!NeedsIndexedVertexCount(drawState.ps, 0, g_pixelShaderManager) &&
        !NeedsIndexedVertexCount(drawState.vs, 1, g_vertexShaderManager))
        return std::nullopt;

    auto& state = *dev->get_private_data<DeviceDataContainer>();
    if (api == device_api::d3d12) {
        const auto& binding = drawState.d3d12IndexBuffer;
        if (!binding.buffer.handle || (binding.indexSize != 2 && binding.indexSize != 4))
            return std::nullopt;
        const uint64_t start = binding.offset + uint64_t{firstIndex} * binding.indexSize;
        const uint64_t length = uint64_t{indexCount} * binding.indexSize;
        std::lock_guard lock(state.indexBufferMutex);
        const auto it = state.indexBuffers.find(binding.buffer.handle);
        if (it != state.indexBuffers.end()) {
            for (auto& snapshot : it->second) {
                if (snapshot.isCurrent() && snapshot.contains(start, length))
                    return snapshot.countDistinct(start, indexCount, binding.indexSize, drawState.d3d12IndexStripCut);
            }
        }
        const auto buffer = state.d3d12Buffers.find(binding.buffer.handle);
        if (buffer == state.d3d12Buffers.end() || !buffer->second.upload)
            return std::nullopt;
        ForgetIndexBuffer(state, binding.buffer.handle);
        if (!CaptureD3D12IndexUpload(state, binding.buffer, start, binding.buffer.handle, start, length))
            return std::nullopt;
        return state.indexBuffers.at(binding.buffer.handle).front().countDistinct(start, indexCount, binding.indexSize,
                                                                                drawState.d3d12IndexStripCut);
    }

    // Query the actual context state so bindings restored by command lists or
    // state blocks are handled even when there was no bind event.
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT offset = 0;
    context->IAGetIndexBuffer(buffer.GetAddressOf(), &format, &offset);
    if (!buffer)
        return std::nullopt;
    if (format != DXGI_FORMAT_R16_UINT && format != DXGI_FORMAT_R32_UINT)
        return std::nullopt;

    D3D11_PRIMITIVE_TOPOLOGY topology;
    context->IAGetPrimitiveTopology(&topology);
    const bool restart = topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP ||
                         topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP ||
                         topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ ||
                         topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
    const uint32_t indexSize = format == DXGI_FORMAT_R16_UINT ? 2 : 4;
    const uint64_t start = uint64_t{offset} + uint64_t{firstIndex} * indexSize;
    std::lock_guard lock(state.indexBufferMutex);
    const auto it = state.indexBuffers.find(reinterpret_cast<uintptr_t>(static_cast<ID3D11Resource*>(buffer.Get())));
    if (it == state.indexBuffers.end())
        return std::nullopt;
    return it->second.front().countDistinct(start, indexCount, indexSize, restart ? IndexStripCut::enabled : IndexStripCut::disabled);
}

static bool onDrawIndexed(command_list* cmd_list, uint32_t index_count, uint32_t instance_count,
                          uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) {
    const DrawGeometry geometry{true, index_count, instance_count, GetIndexedVertexCount(cmd_list, first_index, index_count)};
    return HandleGraphicsDraw(cmd_list, &geometry, first_index, vertex_offset, first_instance);
}

static bool onDrawOrDispatchIndirect(command_list* cmd_list, indirect_command type, resource, uint64_t, uint32_t, uint32_t) {
    // ExecuteIndirect can modify or clear an index binding without a bind event.
    if (cmd_list->get_device()->get_api() == device_api::d3d12)
        cmd_list->get_private_data<CommandListDataContainer>()->d3d12IndexBuffer = {};
    switch (type) {
        case indirect_command::draw:
        case indirect_command::draw_indexed:
            return HandleGraphicsDraw(cmd_list, nullptr);
        case indirect_command::dispatch:
            return HandleComputeDispatch(cmd_list);
        default:
            CheckDrawCall(cmd_list);
            return false;
    }
}

/// <summary>
/// copied from ReShade
/// Returns the path to the module file identified by the specified <paramref name="module"/> handle.
/// </summary>
filesystem::path getModulePath(HMODULE module) {
    WCHAR buf[4096];
    return GetModuleFileNameW(module, buf, ARRAYSIZE(buf)) ? buf : filesystem::path();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            if (!reshade::register_addon(hModule)) {
                return FALSE;
            }

            g_dllPath = getModulePath(hModule);

            g_addonUIData.SetBasePath(g_dllPath.parent_path());
            g_addonUIData.LoadShaderTogglerIniFile();

            state_tracking::register_events(g_addonUIData.GetTrackDescriptors());
            Init();

            reshade::register_event<reshade::addon_event::create_swapchain>(onCreateSwapchain);
            reshade::register_event<reshade::addon_event::init_swapchain>(onInitSwapchain);
            reshade::register_event<reshade::addon_event::destroy_swapchain>(onDestroySwapchain);
            reshade::register_event<reshade::addon_event::init_resource>(onInitResource);
            reshade::register_event<reshade::addon_event::create_resource>(onCreateResource);
            reshade::register_event<reshade::addon_event::map_buffer_region>(onMapBufferRegion);
            reshade::register_event<reshade::addon_event::update_buffer_region>(onUpdateBufferRegion);
            reshade::register_event<reshade::addon_event::update_buffer_region_command>(onUpdateIndexBufferCommand);
            reshade::register_event<reshade::addon_event::copy_resource>(onCopyIndexBufferResource);
            reshade::register_event<reshade::addon_event::copy_buffer_region>(onCopyIndexBufferRegion);
            reshade::register_event<reshade::addon_event::bind_index_buffer>(onD3D12IndexBinding);
            reshade::register_event<reshade::addon_event::bind_pipeline_states>(onD3D12IndexTopology);
            reshade::register_event<reshade::addon_event::execute_command_list>(onExecuteIndexBufferCopies);
            reshade::register_event<reshade::addon_event::execute_secondary_command_list>(onExecuteIndexBufferBundle);
            reshade::register_event<reshade::addon_event::barrier>(onIndexBufferBarrier);
            reshade::register_event<reshade::addon_event::copy_texture_to_buffer>(onIndexCopyTextureToBuffer);
            reshade::register_event<reshade::addon_event::copy_query_heap_results>(onIndexCopyQueryResults);
            reshade::register_event<reshade::addon_event::unmap_buffer_region>(onUnmapBufferRegion);
            reshade::register_event<reshade::addon_event::destroy_resource>(onDestroyResource);
            reshade::register_event<reshade::addon_event::create_resource_view>(onCreateResourceView);
            reshade::register_event<reshade::addon_event::destroy_resource_view>(onDestroyResourceView);
            reshade::register_event<reshade::addon_event::init_resource_view>(onInitResourceView);
            reshade::register_event<reshade::addon_event::init_pipeline>(onInitPipeline);
            reshade::register_event<reshade::addon_event::init_command_queue>(onInitCommandQueue);
            reshade::register_event<reshade::addon_event::destroy_command_queue>(onDestroyCommandQueue);
            reshade::register_event<reshade::addon_event::init_command_list>(onInitCommandList);
            reshade::register_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
            reshade::register_event<reshade::addon_event::reset_command_list>(onResetCommandList);
            reshade::register_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
            reshade::register_event<reshade::addon_event::reshade_overlay>(onReShadeOverlay);
            reshade::register_event<reshade::addon_event::reshade_present>(onReShadePresent);
            reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(onReShadeReloadedEffects);
            reshade::register_event<reshade::addon_event::reshade_set_technique_state>(onReShadeSetTechniqueState);
            reshade::register_event<reshade::addon_event::reshade_reorder_techniques>(onReShadeReorderTechniques);
            reshade::register_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
            reshade::register_event<reshade::addon_event::init_device>(onInitDevice);
            reshade::register_event<reshade::addon_event::destroy_device>(onDestroyDevice);
            reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(onBindRenderTargetsAndDepthStencil);
            reshade::register_event<reshade::addon_event::barrier>(onBarrier);
            reshade::register_event<reshade::addon_event::begin_render_pass>(onBeginRenderPass);
            reshade::register_event<reshade::addon_event::end_render_pass>(onEndRenderPass);
            reshade::register_event<reshade::addon_event::init_effect_runtime>(onInitEffectRuntime);
            reshade::register_event<reshade::addon_event::destroy_effect_runtime>(onDestroyEffectRuntime);
            reshade::register_event<reshade::addon_event::present>(onPresent);

            reshade::register_event<reshade::addon_event::draw>(onDraw);
            reshade::register_event<reshade::addon_event::dispatch>(onDispatch);
            reshade::register_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
            reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);

            reshade::register_overlay("REST", &displayRestTab);
            reshade::register_overlay(nullptr, &displayAddonsTab);
            break;
        case DLL_PROCESS_DETACH:
            UnInit();
            reshade::unregister_event<reshade::addon_event::create_swapchain>(onCreateSwapchain);
            reshade::unregister_event<reshade::addon_event::init_swapchain>(onInitSwapchain);
            reshade::unregister_event<reshade::addon_event::destroy_swapchain>(onDestroySwapchain);
            reshade::unregister_event<reshade::addon_event::reshade_present>(onReShadePresent);
            reshade::unregister_event<reshade::addon_event::map_buffer_region>(onMapBufferRegion);
            reshade::unregister_event<reshade::addon_event::update_buffer_region>(onUpdateBufferRegion);
            reshade::unregister_event<reshade::addon_event::update_buffer_region_command>(onUpdateIndexBufferCommand);
            reshade::unregister_event<reshade::addon_event::copy_resource>(onCopyIndexBufferResource);
            reshade::unregister_event<reshade::addon_event::copy_buffer_region>(onCopyIndexBufferRegion);
            reshade::unregister_event<reshade::addon_event::bind_index_buffer>(onD3D12IndexBinding);
            reshade::unregister_event<reshade::addon_event::bind_pipeline_states>(onD3D12IndexTopology);
            reshade::unregister_event<reshade::addon_event::execute_command_list>(onExecuteIndexBufferCopies);
            reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(onExecuteIndexBufferBundle);
            reshade::unregister_event<reshade::addon_event::barrier>(onIndexBufferBarrier);
            reshade::unregister_event<reshade::addon_event::copy_texture_to_buffer>(onIndexCopyTextureToBuffer);
            reshade::unregister_event<reshade::addon_event::copy_query_heap_results>(onIndexCopyQueryResults);
            reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(onUnmapBufferRegion);
            reshade::unregister_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
            reshade::unregister_event<reshade::addon_event::init_pipeline>(onInitPipeline);
            reshade::unregister_event<reshade::addon_event::reshade_overlay>(onReShadeOverlay);
            reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(onReShadeReloadedEffects);
            reshade::unregister_event<reshade::addon_event::reshade_set_technique_state>(onReShadeSetTechniqueState);
            reshade::unregister_event<reshade::addon_event::reshade_reorder_techniques>(onReShadeReorderTechniques);
            reshade::unregister_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
            reshade::unregister_event<reshade::addon_event::init_command_queue>(onInitCommandQueue);
            reshade::unregister_event<reshade::addon_event::destroy_command_queue>(onDestroyCommandQueue);
            reshade::unregister_event<reshade::addon_event::init_command_list>(onInitCommandList);
            reshade::unregister_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
            reshade::unregister_event<reshade::addon_event::reset_command_list>(onResetCommandList);
            reshade::unregister_event<reshade::addon_event::init_device>(onInitDevice);
            reshade::unregister_event<reshade::addon_event::destroy_device>(onDestroyDevice);
            reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(onBindRenderTargetsAndDepthStencil);
            reshade::unregister_event<reshade::addon_event::barrier>(onBarrier);
            reshade::unregister_event<reshade::addon_event::begin_render_pass>(onBeginRenderPass);
            reshade::unregister_event<reshade::addon_event::end_render_pass>(onEndRenderPass);
            reshade::unregister_event<reshade::addon_event::init_effect_runtime>(onInitEffectRuntime);
            reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(onDestroyEffectRuntime);
            reshade::unregister_event<reshade::addon_event::create_resource>(onCreateResource);
            reshade::unregister_event<reshade::addon_event::init_resource>(onInitResource);
            reshade::unregister_event<reshade::addon_event::destroy_resource>(onDestroyResource);
            reshade::unregister_event<reshade::addon_event::create_resource_view>(onCreateResourceView);
            reshade::unregister_event<reshade::addon_event::init_resource_view>(onInitResourceView);
            reshade::unregister_event<reshade::addon_event::destroy_resource_view>(onDestroyResourceView);
            reshade::unregister_event<reshade::addon_event::present>(onPresent);

            reshade::unregister_event<reshade::addon_event::draw>(onDraw);
            reshade::unregister_event<reshade::addon_event::dispatch>(onDispatch);
            reshade::unregister_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
            reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);

            reshade::unregister_overlay("REST", &displayRestTab);
            reshade::unregister_overlay(nullptr, &displayAddonsTab);

            state_tracking::unregister_events();

            reshade::unregister_addon(hModule);

            break;
    }

    return TRUE;
}
