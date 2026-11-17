#include "CDataFile.h"
#include "DeviceStub.h"
#include "GlobalResourceView.h"
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
        filter.indexCountMin = 30;
        filter.indexCountMax = 50;
        filter.matchInstanceCount = true;
        filter.instanceCountMin = 2;
        filter.instanceCountMax = 2;
        filter.spatialFilterEnabled = true;
        Check(ShaderToggler::addScreenRegion(filter.spatialRegions, {0.25f, 0.25f, 0.5f, 0.75f}));
        Check(!ShaderToggler::addScreenRegion(filter.spatialRegions, {0.3f, 0.3f, 0.4f, 0.4f}));
        filtered.SetShaderDrawSettings(0, 123, filter);
        const auto savedRule = filtered.FindShaderDrawSettings(0, 123);
        Check(savedRule && savedRule->matchesDrawGeometry(true, 30, 2) && savedRule->matchesDrawGeometry(true, 50, 2));
        Check(!savedRule->matchesDrawGeometry(false, 30, 2) && !savedRule->matchesDrawGeometry(true, 51, 2) &&
              !savedRule->matchesDrawGeometry(true, 30, 1));
        Check(!filtered.FindShaderDrawSettings(1, 123));

        CDataFile ini;
        filtered.saveState(ini, 0);
        CDataFile parsed;
        Check(parsed.LoadFromString(ini.Serialize()));
        ShaderToggler::ToggleGroup restored("Restored", 12);
        restored.loadState(parsed, 0);
        Check(restored.configurationSignature() == filtered.configurationSignature());
        const auto restoredRule = restored.FindShaderDrawSettings(0, 123);
        Check(restoredRule && restoredRule->spatialRegions.size() == 1 && restoredRule->spatialRegions.front().maxY == 0.75f);
        const auto clone = restored.cloneForNewId(13);
        Check(static_cast<bool>(clone.FindShaderDrawSettings(0, 123)));
        restored.storeCollectedHashes({}, {123}, {});
        Check(!restored.FindShaderDrawSettings(0, 123) && restoredRule->suppressDrawCall);
        std::puts("Per-shader geometry, region serialization, clone and snapshot checks passed.");
        std::puts("Production view disposal, retained group addresses, terminal retirement, clone and atomic publication checks passed.");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
