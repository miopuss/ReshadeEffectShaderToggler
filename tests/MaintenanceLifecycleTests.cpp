#include "CDataFile.h"
#include "DeviceStub.h"
#include "GlobalResourceView.h"
#include "PipelinePrivateData.h"
#include "ToggleGroup.h"
#include <map>
#include <thread>
#include <cstdio>

using namespace reshade::api;
static void Check(bool ok) { if (!ok) throw std::runtime_error("Maintenance lifecycle regression"); }
struct ViewHost : TestHost::DeviceStub {
    mutable unsigned queries = 0;
    unsigned created = 0, destroyed = 0;
    std::map<uint64_t, bool> live;
    device_api get_api() const override { return device_api::d3d11; }
    resource_desc get_resource_desc(resource) const override {
        ++queries;
        return resource_desc(64, 64, 1, 1, format::r16g16b16a16_float, 1, memory_heap::default_,
                             resource_usage::render_target | resource_usage::shader_resource);
    }
    bool create_resource_view(resource, resource_usage, const resource_view_desc&, resource_view* out) override {
        *out = {++created}; live[out->handle] = true; return true;
    }
    void destroy_resource_view(resource_view view) override {
        Check(live.erase(view.handle) == 1); ++destroyed;
    }
};
int main() {
    try {
        ViewHost host;
        {
            Rendering::GlobalResourceView view(&host, {19}, format::r16g16b16a16_float);
            Check(view.rtv == view.rtv_srgb && view.srv == view.srv_srgb);
            Check(host.created == 2);
            view.Dispose(); view.Dispose();
            Check(host.destroyed == 2 && host.live.empty());
            Check(view.resource_handle == 0 && view.state == Rendering::GlobalResourceState::RESOURCE_INVALID);
        }
        Check(host.destroyed == 2); // Destructor after explicit repeated disposal.

        ShaderToggler::ToggleGroup group("Queued", 9);
        group.toggleActive();
        Check(group.isActive() && !group.isRetired());
        std::unordered_map<int, ShaderToggler::ToggleGroup> groups;
        groups.emplace(9, group);
        auto* queued = &groups.at(9);
        auto retained = groups.extract(9);
        retained.mapped().retire();
        Check(groups.empty() && queued == &retained.mapped() && queued->isRetired() && !queued->isActive());
        queued->toggleActive();
        Check(!queued->isActive()); // Retired work cannot be reactivated by a stale hotkey.
        queued->retire();
        ShaderToggler::ToggleGroup copied(*queued);
        Check(!copied.isRetired()); // New objects do not inherit terminal retirement.
        ShaderToggler::ToggleGroup concurrent("Concurrent", 10);
        concurrent.toggleActive();
        std::atomic_bool stop{false};
        std::thread reader([&] { while (!stop.load()) (void)concurrent.isRetired(); });
        concurrent.retire();
        stop.store(true); reader.join();
        Check(concurrent.isRetired() && !concurrent.isActive());
        ShaderToggler::ToggleGroup filtered("Filtered", 11);
        filtered.storeCollectedHashes({123}, {123}, {});
        ShaderToggler::ShaderDrawSettings filter;
        filter.suppressDrawCall = true;
        filter.matchIndexCount = true;
        filter.indexCountRanges = {{30, 50}, {70, 80}};
        filter.matchInstanceCount = true;
        filter.instanceCountMin = 2;
        filter.instanceCountMax = 2;
        filter.addObservedIndexCount(123);
        filter.addObservedIndexCount(123);
        Check(filter.indexCountRanges.size() == 3 && filter.instanceCountMin == 2 && filter.instanceCountMax == 2);
        filter.spatialFilterEnabled = true;
        Check(ShaderToggler::addScreenRegion(filter.spatialRegions, {0.25f, 0.25f, 0.5f, 0.75f}));
        Check(!ShaderToggler::addScreenRegion(filter.spatialRegions, {0.3f, 0.3f, 0.4f, 0.4f}));
        filtered.SetShaderDrawSettings(0, 123, filter);
        const auto savedRule = filtered.FindShaderDrawSettings(0, 123);
        Check(savedRule && savedRule->matchesDrawGeometry(true, 30, 2) && savedRule->matchesDrawGeometry(true, 50, 2));
        Check(savedRule->matchesDrawGeometry(true, 75, 2) && savedRule->matchesDrawGeometry(true, 123, 2));
        Check(!savedRule->matchesDrawGeometry(false, 30, 2) && !savedRule->matchesDrawGeometry(true, 51, 2) &&
              !savedRule->matchesDrawGeometry(true, 30, 1));
        Check(!filtered.FindShaderDrawSettings(1, 123));

        ShaderToggler::ShaderDrawSettings observed;
        observed.addObservedIndexCount(0);
        observed.addObservedIndexCount(UINT32_MAX);
        Check(observed.suppressDrawCall && observed.matchIndexCount && !observed.matchInstanceCount);
        Check(observed.matchesDrawGeometry(true, 0, 5) && observed.matchesDrawGeometry(true, UINT32_MAX, 9));
        Check(!observed.matchesDrawGeometry(true, 1));
        observed.addObservedVertexBuffer(0x1234567800000001ull);
        observed.addObservedVertexBuffer(1234);
        observed.addObservedVertexBuffer(1234);
        Check(observed.vertexBufferFilters.size() == 2);
        Check(observed.matchIndexCount && observed.indexCountRanges.size() == 2);
        observed.addObservedIndexCount(25);
        observed.addObservedIndexCount(25);
        Check(observed.vertexBufferFilters.size() == 2 && observed.indexCountRanges.size() == 3);
        const std::array<uint64_t, 2> boundBuffers{1234, 5678};
        const std::array<uint64_t, 1> otherBuffer{9999};
        Check(observed.matchesDrawGeometry(true, 0, 5, std::nullopt, boundBuffers));
        Check(!observed.matchesDrawGeometry(true, 0, 5, std::nullopt, otherBuffer));
        Check(!observed.matchesDrawGeometry(true, 0, 5));
        filtered.SetShaderDrawSettings(1, 123, observed);

        ShaderToggler::ShaderDrawSettings vertices;
        vertices.matchVertexCount = true;
        vertices.vertexCountMin = 4;
        vertices.vertexCountMax = 4;
        Check(vertices.matchesDrawGeometry(false, 4));
        Check(!vertices.matchesDrawGeometry(false, 6));
        Check(vertices.matchesDrawGeometry(true, 6, 1, 4));
        Check(!vertices.matchesDrawGeometry(true, 6, 1, 5));
        Check(!vertices.matchesDrawGeometry(true, 4));
        vertices.matchIndexCount = true;
        vertices.indexCountRanges = {{6, 6}};
        Check(vertices.matchesDrawGeometry(true, 6));
        Check(!vertices.matchesDrawGeometry(true, 4, 1, 4));
        Check(vertices.matchesDrawGeometry(false, 4));

        DeviceDataContainer::IndexBufferSnapshot snapshot;
        const uint16_t indices16[] = {99, 0, 1, 2, 2, 3, 0, UINT16_MAX};
        snapshot.bytes.resize(sizeof(indices16));
        std::memcpy(snapshot.bytes.data(), indices16, sizeof(indices16));
        snapshot.offset = 32;
        auto uploadRevision = std::make_shared<uint64_t>(1);
        snapshot.uploadRevision = uploadRevision;
        snapshot.capturedRevision = 1;
        Check(snapshot.countDistinct(34, 6, 2, IndexStripCut::unknown) == 4);
        Check(snapshot.countDistinct(34, 7, 2, IndexStripCut::disabled) == 5);
        Check(snapshot.countDistinct(34, 7, 2, IndexStripCut::enabled) == 4);
        Check(!snapshot.countDistinct(34, 7, 2, IndexStripCut::unknown));
        Check(!snapshot.countDistinct(32, 9, 2, IndexStripCut::disabled));
        Check(!snapshot.countDistinct(30, 2, 2, IndexStripCut::disabled));
        const uint32_t indices32[] = {70000, 70001, 70000, 70003};
        DeviceDataContainer::IndexBufferSnapshot wideIndices;
        wideIndices.bytes.resize(sizeof(indices32));
        std::memcpy(wideIndices.bytes.data(), indices32, sizeof(indices32));
        Check(wideIndices.countDistinct(0, 4, 4, IndexStripCut::disabled) == 3);
        ++*uploadRevision;
        Check(!snapshot.countDistinct(34, 6, 2, IndexStripCut::unknown));

        D3D12IndexBufferWrites ranges;
        ranges.recordRange(32, 12, D3D12IndexBufferCopy{{1}, 7, 0, 32, 12});
        ranges.recordRange(64, 16, D3D12IndexBufferCopy{{1}, 7, 20, 64, 16});
        Check(ranges.copies.size() == 2);
        ranges.recordRange(34, 2, std::nullopt);
        Check(ranges.copies.size() == 1 && ranges.copies.front().destOffset == 64);

        CDataFile ini;
        filtered.saveState(ini, 0);
        CDataFile parsed;
        Check(parsed.LoadFromString(ini.Serialize()));
        ShaderToggler::ToggleGroup restored("Restored", 12);
        restored.loadState(parsed, 0);
        Check(restored.configurationSignature() == filtered.configurationSignature());
        const auto restoredRule = restored.FindShaderDrawSettings(0, 123);
        Check(restoredRule && restoredRule->spatialRegions.size() == 1 && restoredRule->spatialRegions.front().maxY == 0.75f);
        Check(restoredRule->indexCountRanges == filter.indexCountRanges);
        const auto restoredObserved = restored.FindShaderDrawSettings(1, 123);
        Check(restoredObserved && restoredObserved->indexCountRanges == observed.indexCountRanges);
        Check(restoredObserved->matchVertexBuffer && restoredObserved->vertexBufferFilters == observed.vertexBufferFilters);
        Check(restoredObserved->matchesDrawGeometry(true, 0, 5, std::nullopt, boundBuffers));
        const std::array<uint64_t, 1> wideBuffer{0x1234567800000001ull};
        Check(restoredObserved->matchesDrawGeometry(true, 25, 5, std::nullopt, wideBuffer));
        Check(!restoredObserved->matchesDrawGeometry(true, 26, 5, std::nullopt, wideBuffer));
        Check(!restoredObserved->matchesDrawGeometry(true, 25, 5, std::nullopt, otherBuffer));
        const std::string drawSection = "Group0_ShaderDrawSettings";
        const std::string legacyEntry = parsed.GetUInt("Entry0Stage", drawSection) == 0 ? "Entry0" : "Entry1";
        Check(parsed.DeleteKey(legacyEntry + "IndexRangeCount", drawSection));
        Check(parsed.SetUInt(legacyEntry + "IndexMax", 0, "", drawSection));
        ShaderToggler::ToggleGroup legacy("Legacy", 14);
        legacy.loadState(parsed, 0);
        const auto legacyRule = legacy.FindShaderDrawSettings(0, 123);
        Check(legacyRule && legacyRule->indexCountRanges == std::vector<ShaderToggler::IndexCountRange>{{30, UINT32_MAX}});
        Check(!legacyRule->matchesDrawGeometry(true, 29, 2) && legacyRule->matchesDrawGeometry(true, UINT32_MAX, 2));
        const auto clone = restored.cloneForNewId(13);
        Check(static_cast<bool>(clone.FindShaderDrawSettings(0, 123)));
        restored.storeCollectedHashes({}, {123}, {});
        Check(!restored.FindShaderDrawSettings(0, 123) && restoredRule->suppressDrawCall);
        std::puts("Per-shader geometry, region serialization, clone and snapshot checks passed.");
        std::puts("Production view disposal, retained group addresses, terminal retirement, clone and atomic publication checks passed.");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
