#include "ShaderManager.h"
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdio>

static ShaderToggler::ShaderManager* current;
static uint32_t expectedPrevious;
static std::vector<std::string> lines;
static bool logOrderValid = true;
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* text) {
    logOrderValid = logOrderValid && current->getActiveHuntedShaderHash() == expectedPrevious;
    lines.emplace_back(text);
}
int main() {
    try {
        ShaderToggler::ShaderManager manager("PS"); current = &manager;
        manager.addHashHandlePair(0x12345678, 11); manager.addHashHandlePair(0xabcdef01, 22);
        manager.startHuntingMode({});
        manager.addActivePipelineHandle(11); manager.addActivePipelineHandle(22);
        expectedPrevious = 0;
        if (!manager.setActiveHuntedShaderHash(0xabcdef01)) return 1;
        if (lines.size() != 1 || lines.back().find("PS shader 0xabcdef01") == std::string::npos) return 1;
        manager.setActiveHuntedShaderHash(0xabcdef01);
        if (lines.size() != 1) return 1; // No per-frame/repeated-selection flood.
        expectedPrevious = 0xabcdef01;
        manager.huntPreviousShader(false);
        if (lines.size() != 2 || lines.back().find("PS shader 0x12345678") == std::string::npos) return 1;
        manager.toggleMarkOnHuntedShader();
        expectedPrevious = 0x12345678;
        manager.huntNextShader(false);
        expectedPrevious = 0xabcdef01;
        manager.huntNextShader(true);
        if (lines.size() != 4 || lines.back().find("0x12345678") == std::string::npos) return 1;
        if (!logOrderValid) return 1;
        for (uint32_t count = 1; count <= 128; ++count)
            manager.recordDrawGeometry(0x12345678, true, count, 1);
        if (manager.getObservedDrawGeometries().size() != 128) return 1;
        manager.clearObservedDrawGeometries();
        for (uint32_t count = 1; count <= 1000; ++count)
            manager.recordDrawGeometry(0x12345678, true, count, count % 4 + 1, std::nullopt, 101, 11, 0);
        manager.recordDrawGeometry(0x12345678, true, 6, 1, std::nullopt, 102, 22, 1);
        const auto observed = manager.getObservedDrawGeometries();
        if (observed.size() != 2 || observed.front().count != 1000 || observed.front().vertexBufferId != 101)
            return 1;
        std::puts("Production hunting logs preserve stage/hash before direct, keyboard and marked selection publication; repeat selections do not flood logs.");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
