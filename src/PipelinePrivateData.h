#pragma once

#include "CDataFile.h"
#include "VulkanBoundaryTrace.h"
#include "EffectData.h"
#include "ToggleGroup.h"
#include "reshade.hpp"
#include <chrono>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <shared_mutex>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <vector>

struct __declspec(novtable) ResourceRenderData final {
    constexpr ResourceRenderData()
      : group(nullptr)
      , invocationLocation(0)
      , resource({ 0 })
      , format(reshade::api::format::unknown) {}
    constexpr ResourceRenderData(ShaderToggler::ToggleGroup* g, uint64_t i, reshade::api::resource r, reshade::api::format f)
      : group(g)
      , invocationLocation(i)
      , resource(r)
      , format(f) {}

    ShaderToggler::ToggleGroup* group;
    uint64_t invocationLocation;
    reshade::api::resource resource;
    reshade::api::format format;
};

using effect_queue = std::unordered_map<EffectData*, ResourceRenderData>;
using binding_queue = std::unordered_map<ShaderToggler::ToggleGroup*, ResourceRenderData>;

struct __declspec(novtable) ShaderData final {
    uint32_t activeShaderHash = -1;
    binding_queue bindingsToUpdate;
    std::unordered_set<ShaderToggler::ToggleGroup*> constantBuffersToUpdate;
    effect_queue techniquesToRender;
    std::unordered_set<ShaderToggler::ToggleGroup*> srvToUpdate;
    std::vector<ShaderToggler::ToggleGroup*> blockedShaderGroups;
    uint32_t id = 0;

    ShaderData(uint32_t _id)
      : id(_id) {}

    void Reset() {
        activeShaderHash = -1;
        bindingsToUpdate.clear();
        constantBuffersToUpdate.clear();
        techniquesToRender.clear();
        srvToUpdate.clear();
        blockedShaderGroups.clear();
    }
};

enum class IndexStripCut { disabled, enabled, unknown };

struct VertexBufferBinding {
    reshade::api::resource buffer{};
    uint64_t offset = 0;
    uint32_t stride = 0;
};

struct D3D12IndexBufferBinding {
    reshade::api::resource buffer{};
    uint64_t offset = 0;
    uint32_t indexSize = 0;
};

struct D3D12IndexBufferCopy {
    reshade::api::resource source{};
    uint64_t sourceId = 0;
    uint64_t sourceOffset = 0;
    uint64_t destOffset = 0;
    uint64_t size = 0;
};

struct D3D12IndexBufferWrites {
    uint64_t serial = 0;
    std::vector<D3D12IndexBufferCopy> copies;

    void recordRange(uint64_t offset, uint64_t size, std::optional<D3D12IndexBufferCopy> copy) {
        // A later write can replace any part of a previously tracked range.
        std::erase_if(copies, [=](const D3D12IndexBufferCopy& old) {
            return offset < old.destOffset + old.size && old.destOffset < offset + size;
        });
        if (copy && copies.size() < 256)
            copies.push_back(*copy);
    }
};

struct __declspec(uuid("222F7169-3C09-40DB-9BC9-EC53842CE537")) CommandListDataContainer {
    uint64_t commandQueue = 0;
    bool vulkanAutoInjectionActive = false;
    uint32_t vulkanTraceLastPixelHash = 0;
    bool vulkanInsideRenderPass = false;
    bool vulkanRenderPassEndPending = false;
    bool vulkanRenderPassSuspends = false;
    bool d3d12InsideRenderPass = false;
    D3D12IndexBufferBinding d3d12IndexBuffer;
    std::array<VertexBufferBinding, 32> d3d12VertexBuffers{};
    IndexStripCut d3d12IndexStripCut = IndexStripCut::unknown;
    std::unordered_map<uint64_t, D3D12IndexBufferWrites> d3d12IndexWrites;
    bool d3d12UnknownIndexAlias = false;
    ShaderData ps{ 0 };
    ShaderData vs{ 1 };
    ShaderData cs{ 2 };

    void Reset() {
        ps.Reset();
        vs.Reset();
        cs.Reset();

        commandQueue = 0;
        vulkanAutoInjectionActive = false;
        vulkanInsideRenderPass = false;
        vulkanRenderPassEndPending = false;
        vulkanRenderPassSuspends = false;
        d3d12InsideRenderPass = false;
        d3d12IndexBuffer = {};
        d3d12VertexBuffers = {};
        d3d12IndexStripCut = IndexStripCut::unknown;
        d3d12IndexWrites.clear();
        d3d12UnknownIndexAlias = false;
    }
};

struct __declspec(novtable) TextureBindingData final {
    reshade::api::resource res;
    reshade::api::format format;
    reshade::api::resource_view srv;
    reshade::api::resource_view rtv;
    uint32_t width;
    uint32_t height;
    uint16_t levels;
    bool enabled_reset_on_miss;
    bool copy;
    bool reset = false;
};

struct __declspec(novtable) HuntPreview final {
    reshade::api::resource target = reshade::api::resource{ 0 };
    bool matched = false;
    uint64_t target_invocation_location = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    reshade::api::format format = reshade::api::format::unknown;
    reshade::api::format view_format = reshade::api::format::unknown;
    reshade::api::resource_desc target_desc;
    bool recreate_preview = false;
    reshade::api::command_list* tracked_command_list = nullptr;

    // Vulkan hunting cannot copy from the target inside the active render pass.
    // Record the candidate at the suppressed draw and consume it at the next safe
    // render-pass boundary on the same command list.
    bool vulkan_capture_pending = false;
    reshade::api::command_list* vulkan_command_list = nullptr;
    uint32_t hunted_shader_hash = 0;
    uint32_t hunted_stage = 0;
    std::string status;

    void Reset() {
        matched = false;
        target = reshade::api::resource{ 0 };
        target_invocation_location = 0;
        width = 0;
        height = 0;
        format = reshade::api::format::unknown;
        view_format = reshade::api::format::unknown;
        recreate_preview = false;
        tracked_command_list = nullptr;
        vulkan_capture_pending = false;
        vulkan_command_list = nullptr;
        hunted_shader_hash = 0;
        hunted_stage = 0;
        status.clear();
    }
};

struct TechniqueUIEntry {
    std::string name;
    std::string upperName;
    EffectData* effect = nullptr;
};

struct __declspec(novtable) SpecialEffect final {
    std::string name;
    reshade::api::effect_technique technique;
};

enum SpecialEffects : uint32_t { REST_TONEMAP_TO_SDR = 0, REST_TONEMAP_TO_HDR, REST_FLIP, REST_NOOP, REST_EFFECTS_COUNT };

struct __declspec(novtable) CustomShaderInstance final {
    reshade::api::pipeline pipeline = { 0 };
    reshade::api::pipeline_layout pipelineLayout = { 0 };
    reshade::api::sampler pipelineSampler = { 0 };
};

struct __declspec(novtable) CustomShader final {
    CustomShaderInstance copyPipeline;
    CustomShaderInstance alphaPreservingCopyPipeline;
    std::unordered_map<uint32_t, CustomShaderInstance> copyFormatPipelines;
    std::unordered_map<uint32_t, CustomShaderInstance> alphaFormatPipelines;
    std::mutex pipelineMutex;

    reshade::api::resource fullscreenQuadVertexBuffer = { 0 };
};

struct __declspec(novtable) ResouceManagerData final {
    reshade::api::resource preview_res[2] = { { 0 }, { 0 } };
    reshade::api::resource_view preview_rtv[2] = { { 0 }, { 0 } };
    reshade::api::resource_view preview_srv[2] = { { 0 }, { 0 } };

    reshade::api::resource dummy_res = { 0 };
    reshade::api::resource_view dummy_rtv = { 0 };
};

struct __declspec(novtable) BindingManagerData final {
    reshade::api::resource empty_res = { 0 };
    reshade::api::resource_view empty_srv = { 0 };
    reshade::api::resource_view empty_rtv = { 0 };
};

struct __declspec(uuid("C63E95B1-4E2F-46D6-A276-E8B4612C069A")) DeviceDataContainer {
    struct IndexBufferSnapshot {
        uint64_t offset = 0;
        std::vector<uint8_t> bytes;
        std::map<std::tuple<uint64_t, uint32_t, uint32_t, IndexStripCut>, std::optional<uint32_t>> vertexCounts;
        std::shared_ptr<const uint64_t> uploadRevision;
        uint64_t capturedRevision = 0;

        bool isCurrent() const {
            return !uploadRevision || *uploadRevision == capturedRevision;
        }

        bool contains(uint64_t start, uint64_t length) const {
            return start >= offset && start - offset <= bytes.size() && length <= bytes.size() - (start - offset);
        }

        std::optional<uint32_t> countDistinct(uint64_t start, uint32_t indexCount, uint32_t indexSize, IndexStripCut cut) {
            if (!isCurrent() || (indexSize != 2 && indexSize != 4) || !contains(start, uint64_t{indexCount} * indexSize))
                return std::nullopt;
            if (indexCount == 0)
                return 0;
            const auto key = std::make_tuple(start, indexCount, indexSize, cut);
            const auto cached = vertexCounts.find(key);
            if (cached != vertexCounts.end())
                return cached->second;

            // A common base-vertex offset preserves the number of distinct indices.
            const uint32_t cutIndex = indexSize == 2 ? UINT16_MAX : UINT32_MAX;
            std::vector<uint32_t> indices;
            indices.reserve(indexCount);
            const auto* data = bytes.data() + static_cast<size_t>(start - offset);
            for (uint32_t i = 0; i < indexCount; ++i, data += indexSize) {
                uint32_t index = 0;
                std::memcpy(&index, data, indexSize);
                if (cut == IndexStripCut::unknown && (index == UINT16_MAX || index == UINT32_MAX)) {
                    if (vertexCounts.size() < 256)
                        vertexCounts.emplace(key, std::nullopt);
                    return std::nullopt;
                }
                if (cut != IndexStripCut::enabled || index != cutIndex)
                    indices.push_back(index);
            }
            std::sort(indices.begin(), indices.end());
            const auto distinct = static_cast<uint32_t>(std::unique(indices.begin(), indices.end()) - indices.begin());
            if (vertexCounts.size() < 256)
                vertexCounts.emplace(key, distinct);
            return distinct;
        }
    };
    struct D3D12BufferInfo {
        uint64_t id;
        uint64_t size;
        uint64_t latestWrite = 0;
        bool upload;
        uint32_t mapCount = 0;
        bool uploaded = false;
        std::shared_ptr<uint64_t> uploadRevision;
    };
    static constexpr size_t indexBufferByteBudget = 64 * 1024 * 1024;
    std::mutex indexBufferMutex;
    std::unordered_map<uint64_t, uint64_t> geometryResourceIds;
    std::unordered_map<uint64_t, std::vector<IndexBufferSnapshot>> indexBuffers;
    size_t indexBufferBytes = 0;
    bool indexUploadsInvalidated = false;
    uint64_t d3d12IndexSerial = 0;
    std::unordered_map<uint64_t, D3D12BufferInfo> d3d12Buffers;

    reshade::api::effect_runtime* current_runtime = nullptr;
    std::atomic_bool rendered_effects = false;
    std::shared_mutex binding_mutex;
    std::shared_mutex render_mutex;
    std::unordered_set<const ShaderToggler::ToggleGroup*> bindingsUpdated;
    std::unordered_set<const ShaderToggler::ToggleGroup*> constantsUpdated;
    std::unordered_set<const ShaderToggler::ToggleGroup*> srvUpdated;
    VulkanBoundaryTrace vulkanBoundaryTrace;
    effect_queue vulkanAutoPendingEffects;
    std::atomic_bool vulkanAutoWorkPending{ false };
    std::atomic_bool vulkanPreviewWorkPending{ false };
    std::mutex queueMutex;
    std::vector<reshade::api::command_queue*> queues;
    std::recursive_mutex previewMutex;
    HuntPreview huntPreview;
    CustomShader customShader;
    ResouceManagerData resourceManagerData;
    BindingManagerData bindingManagerData;
};

struct __declspec(uuid("838BAF1D-95C0-4A7E-A517-052642879986")) RuntimeDataContainer {
    std::shared_mutex technique_mutex;
    std::unordered_map<std::string, EffectData> allTechniques;
    std::unordered_set<EffectData*> allEnabledTechniques;
    std::vector<EffectData*> allSortedTechniques;
    std::vector<TechniqueUIEntry> techniqueUiCache;

    SpecialEffect specialEffects[4] = {
        SpecialEffect{ "REST_TONEMAP_TO_SDR", reshade::api::effect_technique{ 0 } },
        SpecialEffect{ "REST_TONEMAP_TO_HDR", reshade::api::effect_technique{ 0 } },
        SpecialEffect{ "REST_FLIP", reshade::api::effect_technique{ 0 } },
        SpecialEffect{ "REST_NOOP", reshade::api::effect_technique{ 0 } },
    };
    int32_t previousEnableCount = 0;
};
