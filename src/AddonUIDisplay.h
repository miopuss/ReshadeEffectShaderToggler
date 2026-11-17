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

#pragma once
#include "RenderingSafety.h"
#include "AddonUITheme.h"
#include "AddonUIAbout.h"
#include "AddonUIConstants.h"
#include "ConstantManager.h"
#include "KeyData.h"
#include "ResourceManager.h"
#include "RenderingManager.h"
#include "version.h"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <format>
#include <imgui.h>
#include <optional>
#include <ranges>
#include <reshade.hpp>
#include <unordered_set>
#include <vector>

#define MAX_DESCRIPTOR_INDEX 10

enum RestCategory {
    CAT_GROUPS = 0,
    CAT_KEYBINDINGS,
    CAT_OPTIONS,
    CAT_COUNT
};

static const char* const s_restCategoryNames[CAT_COUNT] = {
    Tr("Toggle Groups"),
    Tr("Keybindings"),
    Tr("Options")
};

static bool DrawSidebarCategoryButton(int category_id, const char* label, bool selected) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetFrameHeight() + 10.0f;

    ImGui::PushStyleColor(ImGuiCol_Button, selected ? rfx_col(ImGuiCol_Header) : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rfx_col(ImGuiCol_HeaderHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rfx_col(ImGuiCol_HeaderActive));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

    ImGui::PushID(category_id);
    bool clicked = ImGui::Button("##cat", ImVec2(width, height));
    ImGui::PopID();

    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(3);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    float cy = p.y + height * 0.5f;
    float cx = p.x + 18.0f;
    ImU32 icon_col = rfx_u32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);

    switch (category_id) {
    case CAT_GROUPS: {
        draw->AddRect(ImVec2(cx - 6, cy - 5), ImVec2(cx + 3, cy + 1), icon_col, 1.0f, 0, 1.5f);
        draw->AddRect(ImVec2(cx - 2, cy - 1), ImVec2(cx + 6, cy + 5), icon_col, 1.0f, 0, 1.5f);
        break;
    }
    case CAT_KEYBINDINGS: {
        draw->AddRect(ImVec2(cx - 6, cy - 4), ImVec2(cx + 6, cy + 4), icon_col, 1.5f, 0, 1.5f);
        draw->AddLine(ImVec2(cx - 3, cy - 1), ImVec2(cx - 3, cy + 1), icon_col, 1.5f);
        draw->AddLine(ImVec2(cx + 3, cy - 1), ImVec2(cx + 3, cy + 1), icon_col, 1.5f);
        break;
    }
    case CAT_OPTIONS: {
        draw->AddLine(ImVec2(cx - 6, cy - 3), ImVec2(cx + 6, cy - 3), icon_col, 1.5f);
        draw->AddCircleFilled(ImVec2(cx - 2, cy - 3), 2.2f, icon_col);
        draw->AddLine(ImVec2(cx - 6, cy + 3), ImVec2(cx + 6, cy + 3), icon_col, 1.5f);
        draw->AddCircleFilled(ImVec2(cx + 2, cy + 3), 2.2f, icon_col);
        break;
    }
    default:
        break;
    }

    {
        const ImU32 text_col = rfx_u32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        draw->AddText(ImVec2(p.x + 34.0f, cy - ImGui::GetTextLineHeight() * 0.5f), text_col, label);
    }

    if (selected) {
        float dot_r = 3.5f;
        ImVec2 dot_pos(p.x + width - 14.0f, cy);
        draw->AddCircleFilled(dot_pos, dot_r, rfx_u32(rfx_accent()));
    }

    return clicked;
}


// From ReShade, see https://github.com/crosire/reshade/blob/main/source/imgui_widgets.cpp
static bool key_input_box(const char* name, uint32_t* keys, const reshade::api::effect_runtime* runtime) {
    char buf[160];
    buf[0] = '\0';
    if (*keys)
        buf[ShaderToggler::reshade_key_name(*keys).copy(buf, sizeof(buf) - 1)] = '\0';

    ImGui::InputTextWithHint(name,
                             "Click to set keyboard shortcut",
                             buf,
                             sizeof(buf),
                             ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_NoHorizontalScroll);

    if (ImGui::IsItemActive()) {
        const uint32_t last_key_pressed = ShaderToggler::reshade_last_key_pressed(runtime);
        if (last_key_pressed != 0) {
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                *keys = 0;

            } else if (last_key_pressed < 0x10 || last_key_pressed > 0x12) // Exclude modifier keys
            {
                *keys = last_key_pressed;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x11)) << 8;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x10)) << 16;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x12)) << 24;
            }

            return true;
        }
    } else if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(Tr("Click in the field and press any key to change the shortcut to that key."));
    }

    return false;
}

static const char* const invocationDescription[] = { Tr("BEFORE DRAW"), Tr("AFTER DRAW"), Tr("ON RENDER TARGET CHANGE") };

static void DisplayIsPartOfToggleGroup() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
    ImGui::SameLine();
    ImGui::Text(Tr(" Shader is part of this toggle group."));
    ImGui::PopStyleColor();
}

static void DisplayTechniqueSelection(reshade::api::effect_runtime* runtime,
                                      AddonImGui::AddonUIData& instance,
                                      ShaderToggler::ToggleGroup* group) {
    if (group == nullptr)
        return;

    RuntimeDataContainer& runtimeData = (*runtime->get_private_data<RuntimeDataContainer>());
    static char searchBuf[256] = "\0";

    bool allowAll = group->getAllowAllTechniques();
    bool exceptions = group->getHasTechniqueExceptions();
    const bool oldAllowAll = allowAll;
    const bool oldExceptions = exceptions;
    bool selectionChanged = false;

    size_t availableCount = 0;
    {
        std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
        availableCount = runtimeData.techniqueUiCache.size();
    }

    if (BeginCard("##tech_mode_card", "TECHNIQUE ROUTING MODE")) {
        const float labelColWidth = std::max(220.0f, ImGui::CalcTextSize(Tr("Except for selected techniques   ")).x);
        if (ImGui::BeginTable("TechniqueModeTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody, ImVec2(-RFX_CARD_PAD, 0.0f))) {
            ImGui::TableSetupColumn("##col_label", ImGuiTableColumnFlags_WidthFixed, labelColWidth);
            ImGui::TableSetupColumn("##col_ctrl", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Tr("Apply all enabled techniques"));
            ImGui::TableNextColumn();
            if (DrawTableToggleSwitch("##ApplyAllTechniques", &allowAll)) {
                selectionChanged = true;
            }

            if (allowAll) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(Tr("Except for selected techniques"));
                ImGui::TableNextColumn();
                if (DrawTableToggleSwitch("##TechExceptions", &exceptions)) {
                    selectionChanged = true;
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Tr("Current Routing Mode"));
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const char* modeDesc = nullptr;
            const char* modeName = nullptr;
            ImVec4 modeCol;
            if (!allowAll) {
                modeName = Tr("INCLUSION LIST");
                modeCol = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
                modeDesc = Tr("Only checked techniques trigger for this group.");
            } else if (exceptions) {
                modeName = Tr("EXCLUSION LIST");
                modeCol = ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
                modeDesc = Tr("All active techniques trigger EXCEPT checked ones.");
            } else {
                modeName = Tr("ALL ACTIVE TECHNIQUES (Passive)");
                modeCol = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
                modeDesc = Tr("All globally enabled ReShade techniques apply.");
            }
            // Right-align with the switches above (same 8px pad as DrawTableToggleSwitch).
            const float modeW = ImGui::CalcTextSize(modeName).x;
            const float modeAvail = ImGui::GetContentRegionAvail().x;
            if (modeAvail > modeW + 8.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + modeAvail - modeW - 8.0f);
            }
            ImGui::TextColored(modeCol, "%s", modeName);

            ImGui::EndTable();
            // Description on its own full-width line so it wraps at the card edge instead of
            // being squeezed into the leftover space beside the mode name.
            ImGui::TextDisabled("%s", modeDesc);
        }
        EndCard();
    }

    ImGui::Spacing();

    if (BeginCard("##tech_list_card", "AVAILABLE TECHNIQUES")) {
        // Search bar
        ImGui::SetNextItemWidth(-RFX_CARD_PAD);
        ImGui::InputTextWithHint("##techniqueSearch", Tr("Search techniques by name..."), searchBuf, IM_ARRAYSIZE(searchBuf));

        ImGui::Spacing();

        const bool canEditCheckboxes = !allowAll || exceptions;
        if (!canEditCheckboxes) {
            ImGui::BeginDisabled();
        }

        const float selectAllW = ImGui::CalcTextSize(Tr("Select All")).x + ImGui::GetStyle().FramePadding.x * 2.0f + 14.0f;
        const float untickAllW = ImGui::CalcTextSize(Tr("Untick All")).x + ImGui::GetStyle().FramePadding.x * 2.0f + 14.0f;
        if (ImGui::Button(Tr("Select All"), ImVec2(selectAllW, 0))) {
            std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
            auto updated = group->preferredTechniques();
            std::string searchUpper(searchBuf);
            std::transform(searchUpper.begin(), searchUpper.end(), searchUpper.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            for (const auto& entry : runtimeData.techniqueUiCache) {
                if (searchUpper.empty() || entry.upperName.find(searchUpper) != std::string::npos) {
                    updated.insert(entry.name);
                }
            }
            group->setPreferredTechniques(updated);
            selectionChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("Untick All"), ImVec2(untickAllW, 0))) {
            if (searchBuf[0] == '\0') {
                const std::unordered_set<std::string> empty;
                group->setPreferredTechniques(empty);
            } else {
                std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
                auto updated = group->preferredTechniques();
                std::string searchUpper(searchBuf);
                std::transform(searchUpper.begin(), searchUpper.end(), searchUpper.begin(),
                               [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
                for (const auto& entry : runtimeData.techniqueUiCache) {
                    if (entry.upperName.find(searchUpper) != std::string::npos) {
                        updated.erase(entry.name);
                    }
                }
                group->setPreferredTechniques(updated);
            }
            selectionChanged = true;
        }

        char techCountBuf[64];
        snprintf(techCountBuf, sizeof(techCountBuf), Tr("%zu selected / %zu available"), group->preferredTechniques().size(), availableCount);
        // Only share the button row when the whole counter fits; otherwise give it its own line.
        const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - RFX_CARD_PAD;
        const float prevEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX() + ImGui::GetStyle().ItemSpacing.x;
        if (prevEnd + ImGui::CalcTextSize(techCountBuf).x <= rowRight) {
            ImGui::SameLine();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", techCountBuf);

        ImGui::Separator();

        const float tableH = std::max(120.0f, ImGui::GetContentRegionAvail().y - 8.0f);
        const int numCols = 1;

        std::string searchUpper(searchBuf);
        std::transform(searchUpper.begin(), searchUpper.end(), searchUpper.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });

        {
            std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
            if (runtimeData.techniqueUiCache.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled(Tr("No ReShade techniques detected. Make sure your effects are loaded."));
            } else {
                if (ImGui::BeginTable("TechniqueSelectionTable", numCols,
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV,
                                      ImVec2(-RFX_CARD_PAD, tableH))) {
                    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

                    int matchCount = 0;
                    for (const auto& entry : runtimeData.techniqueUiCache) {
                        if (!searchUpper.empty() && entry.upperName.find(searchUpper) == std::string::npos)
                            continue;

                        matchCount++;
                        ImGui::TableNextColumn();
                        bool enabled = group->preferredTechniques().contains(entry.name);
                        ImGui::PushID(entry.name.c_str());
                        if (ImGui::Checkbox(entry.name.c_str(), &enabled)) {
                            auto updated = group->preferredTechniques();
                            if (enabled)
                                updated.insert(entry.name);
                            else
                                updated.erase(entry.name);
                            group->setPreferredTechniques(updated);
                            selectionChanged = true;
                        }

                        if (entry.effect != nullptr && !entry.effect->enabled) {
                            ImGui::SameLine();
                            ImGui::TextDisabled(Tr("(off in ReShade)"));
                        }
                        ImGui::PopID();
                    }

                    if (matchCount == 0 && !searchUpper.empty()) {
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled(Tr("No techniques matching \"%s\""), searchBuf);
                    }

                    ImGui::EndTable();
                }
            }
        }

        if (!canEditCheckboxes) {
            ImGui::EndDisabled();
        }

        EndCard();
    }

    if (allowAll != oldAllowAll || exceptions != oldExceptions) {
        group->setHasTechniqueExceptions(exceptions);
        group->setAllowAllTechniques(allowAll);
        selectionChanged = true;
    }

    if (selectionChanged) {
        std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
        instance.AssignPreferredGroupTechniques(runtimeData.allTechniques);
    }
}

static void DrawPreview(unsigned long long textureId,
                        uint32_t srcWidth,
                        uint32_t srcHeight,
                        ImVec4 tint = ImVec4(1.0f, 1.0f, 1.0f, 1.0f)) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    float height = ImGui::GetWindowHeight();
    float width = ImGui::GetWindowWidth();

    float new_width = static_cast<float>(srcWidth);
    float new_height = static_cast<float>(srcHeight);

    float ratio = std::min(width / new_width, height / new_height);
    new_width *= ratio;
    new_height *= ratio;

    auto initialCursorPos = ImGui::GetCursorPos();
    auto centralizedCursorpos = ImVec2((width - new_width) * 0.5f, (height - new_height) * 0.5f);
    ImGui::SetCursorPos(centralizedCursorpos);

    ImGui::ImageWithBg(textureId, ImVec2(new_width, new_height), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);

    ImGui::PopStyleVar();
}

static void DisplayPreview(AddonImGui::AddonUIData& instance,
                           Rendering::ResourceManager& resManager,
                           reshade::api::effect_runtime* runtime,
                           ShaderToggler::ToggleGroup* group,
                           float width = 0) {
    if (ImGui::BeginChild("RTPreview##child", { width, 0 }, true, ImGuiWindowFlags_None)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

        DeviceDataContainer& deviceData = *runtime->get_device()->get_private_data<DeviceDataContainer>();
        reshade::api::resource_view srv = reshade::api::resource_view{ 0 };
        resManager.SetPongPreviewHandles(runtime->get_device(), nullptr, nullptr, &srv);
        bool clearAlpha = group->getClearPreviewAlpha();
        const bool vulkan = runtime->get_device()->get_api() == reshade::api::device_api::vulkan;

        ImGui::Text(Tr("Clear alpha channel"));
        ImGui::SameLine();
        if (vulkan)
            ImGui::BeginDisabled();
        ImGui::Checkbox("##Clearalpha", &clearAlpha);
        if (vulkan) {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(Tr("Vulkan safe preview currently copies the source image directly."));
        }

        if (!deviceData.huntPreview.status.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", Tr(deviceData.huntPreview.status.c_str()));
        }

        if (deviceData.huntPreview.target != 0) {
            const char* stageName = deviceData.huntPreview.hunted_stage == 0 ? "PS" :
                                    deviceData.huntPreview.hunted_stage == 1 ? "VS" : "CS";
            ImGui::Text(Tr("Shader: 0x%08x (%s)"), deviceData.huntPreview.hunted_shader_hash, stageName);
            ImGui::SameLine();
            ImGui::Text(Tr("Target: %ux%u"), deviceData.huntPreview.width, deviceData.huntPreview.height);
            ImGui::SameLine();
            ImGui::Text(Tr("Format: %s"), Rendering::RenderingManager::FormatName(deviceData.huntPreview.format).c_str());
            ImGui::SameLine();
            ImGui::Text(Tr("Address: 0x%llx"), static_cast<unsigned long long>(deviceData.huntPreview.target.handle));
            ImGui::Separator();
        }

        int& previewChannel = instance.GetHuntingUIState().previewChannel;
        if (srv != 0 && deviceData.huntPreview.matched) {
            ImGui::TextDisabled(Tr("View"));
            ImGui::SameLine();
            ImGui::RadioButton("RGB", &previewChannel, 0);
            ImGui::SameLine();
            ImGui::RadioButton("R", &previewChannel, 1);
            ImGui::SameLine();
            ImGui::RadioButton("G", &previewChannel, 2);
            ImGui::SameLine();
            ImGui::RadioButton("B", &previewChannel, 3);

            const ImVec4 previewTint =
              previewChannel == 1 ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f) :
              previewChannel == 2 ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) :
              previewChannel == 3 ? ImVec4(0.0f, 0.0f, 1.0f, 1.0f) :
                                    ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

            if (ImGui::BeginChild("RTPreview##preview", { 0, 0 }, false, ImGuiWindowFlags_None)) {
                DrawPreview(srv.handle, deviceData.huntPreview.width, deviceData.huntPreview.height, previewTint);
            }
            ImGui::EndChild();
        } else if (deviceData.huntPreview.status.empty()) {
            ImGui::TextDisabled(vulkan ? Tr("Select a shader while hunting to capture a Vulkan preview.") : Tr("No render target preview available."));
        }

        group->setClearPreviewAlpha(clearAlpha);

        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

static void DisplayBindingPreview(AddonImGui::AddonUIData& instance,
                                  Rendering::ResourceManager& resManager,
                                  reshade::api::effect_runtime* runtime,
                                  ShaderToggler::ToggleGroup* group) {
    if (ImGui::BeginChild("BindingPreview##child", { 0, 0 }, true, ImGuiWindowFlags_None)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

        DeviceDataContainer& deviceData = *runtime->get_device()->get_private_data<DeviceDataContainer>();
        ShaderToggler::GroupResource& groupResource = group->GetGroupResource(ShaderToggler::GroupResourceType::RESOURCE_BINDING);

        reshade::api::resource_view res_view = { 0 };
        if (groupResource.owning) {
            res_view = groupResource.srv;
        } else if (groupResource.g_res != nullptr) {
            res_view = groupResource.g_res->srv;
        }

        if (res_view != 0) {
            ImGui::TextUnformatted(TrFmt("Format: {} ", static_cast<uint32_t>(groupResource.target_description.texture.format)).c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(TrFmt("Width: {} ", groupResource.target_description.texture.width).c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(TrFmt("Height: {} ", groupResource.target_description.texture.height).c_str());
            ImGui::Separator();

            if (ImGui::BeginChild("BindingPreview##preview", { 0, 0 }, false, ImGuiWindowFlags_None)) {
                DrawPreview(res_view.handle, groupResource.target_description.texture.width, groupResource.target_description.texture.height);
            }
            ImGui::EndChild();
        }

        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

// Diagnostics are also recorded by the rendering path. Translate only for display and copying.
static std::string TranslateAutoStatus(const std::string& status) {
    if constexpr (RestLocalization::Chinese) {
        constexpr std::string_view prefix = "Rejected target: ";
        if (status.starts_with(prefix)) {
            std::string translated = status;
            for (const std::string_view part : {prefix, std::string_view("unsupported colour format "),
                                               std::string_view(" aspect mismatch vs "), std::string_view(" output"),
                                               std::string_view(" scale "), std::string_view(" outside automatic range")}) {
                if (const auto pos = translated.find(part); pos != std::string::npos)
                    translated.replace(pos, part.size(), RestLocalization::TranslateView(part));
            }
            return translated;
        }
    }
    return Tr(status.c_str());
}

static void DisplayRenderTargets(AddonImGui::AddonUIData& instance,
                                 Rendering::ResourceManager& resManager,
                                 reshade::api::effect_runtime* runtime,
                                 ShaderToggler::ToggleGroup* group) {
    static float height = ImGui::GetWindowHeight();

    const char* typeSelectedItem = invocationDescription[group->getInvocationLocation()];
    uint32_t selectedIndex = group->getInvocationLocation();

    const char* typeDestItems[] = { Tr("Render target"), Tr("Shader Resource View") };
    uint32_t selectedDestIndex = group->getRenderToResourceViews() ? 1 : 0;
    const char* typeSelectedDestItem = typeDestItems[selectedDestIndex];

    static const char* stageItems[] = { Tr("PIXEL"), Tr("VERTEX"), Tr("COMPUTE") };
    uint32_t selectedStageIndex = group->getRenderSRVShaderStage();
    const char* selectedStage = stageItems[selectedStageIndex];

    bool retry = group->getRequeueAfterRTMatchingFailure();
    bool tonemap = group->getToneMap();
    bool preserveAlpha = group->getPreserveAlpha();
    bool flipbuffer = group->getFlipBuffer();
    bool autoSceneColour = group->getAutoRenderSRV();

    static const char* swapchainMatchOptions[] = { Tr("RESOLUTION"), Tr("ASPECT RATIO"), Tr("EXTENDED ASPECT RATIO"), Tr("NONE") };
    uint32_t selectedSwapchainMatchMode = group->getMatchSwapchainResolution();
    const char* typesSelectedSwapchainMatchMode = swapchainMatchOptions[selectedSwapchainMatchMode];

    const reshade::api::device_api deviceApi = runtime->get_device()->get_api();
    const bool autoSceneColourSupported = ShaderToggler::IsAutoSceneColourSupported(deviceApi);
    const bool supportsSRVwrite = deviceApi < reshade::api::device_api::d3d12;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    if (ImGui::BeginChild("RenderTargets", { 0, height / 1.5f }, true, ImGuiChildFlags_AlwaysAutoResize)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

        if (ImGui::BeginTable("RenderTargetsSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody)) {
            ImGui::TableSetupColumn("##RTcolumnsetup", ImGuiTableColumnFlags_WidthFixed, ImGui::GetWindowWidth() / 3);

            ImGui::TableNextColumn();
            ImGui::Text(Tr("Auto scene colour"));
            ImGui::TableNextColumn();
            if (!autoSceneColourSupported)
                ImGui::BeginDisabled();
            DrawTableToggleSwitch("##AutoSceneColour", &autoSceneColour);
            if (!autoSceneColourSupported)
                ImGui::EndDisabled();
            if (!autoSceneColourSupported) {
                ImGui::SameLine();
                ImGui::TextDisabled(Tr("(D3D10/D3D11/D3D12/Vulkan only)"));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(Tr("Auto Scene Colour supports D3D10, D3D11, D3D12 and Vulkan. Vulkan native staging uses ReShade\'s generic image-blit path."));
            }

            const bool autoSceneColourActive = autoSceneColour && autoSceneColourSupported;
            const bool pendingVulkanShaderEdits =
              deviceApi == reshade::api::device_api::vulkan &&
              instance.GetToggleGroupIdShaderEditing().load() == group->getId();

            ImGui::TableNextRow();

            if (autoSceneColourActive) {
                // Automatic mode overlays the saved manual target configuration without
                // modifying it. The core resolver gives Auto mode precedence while active.

                ImGui::TableNextColumn();
                ImGui::Text(Tr("Target"));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Tr("Live render target (matched draw)"));

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text(Tr("Graphics API"));
                ImGui::TableNextColumn();
                if (deviceApi == reshade::api::device_api::d3d10)
                    ImGui::TextUnformatted("D3D10");
                else if (deviceApi == reshade::api::device_api::d3d11)
                    ImGui::TextUnformatted("D3D11");
                else if (deviceApi == reshade::api::device_api::d3d12)
                    ImGui::TextUnformatted("D3D12");
                else
                    ImGui::TextUnformatted("Vulkan");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text(Tr("Current attempt"));
                ImGui::TableNextColumn();
                if (pendingVulkanShaderEdits)
                    ImGui::TextUnformatted(Tr("Finish shader hunting to apply marks"));
                else if (!group->getDebugAutoStatus().empty())
                    ImGui::TextUnformatted(TranslateAutoStatus(group->getDebugAutoStatus()).c_str());
                else
                    ImGui::TextUnformatted(Tr("Waiting for matching render target..."));

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text(Tr("Current target"));
                ImGui::TableNextColumn();
                if (group->getDebugCurrentSceneWidth() > 0 && group->getDebugCurrentSceneHeight() > 0) {
                    ImGui::Text("%ux%u | %s | 0x%llx",
                                group->getDebugCurrentSceneWidth(),
                                group->getDebugCurrentSceneHeight(),
                                group->getDebugCurrentFormat().empty() ? Tr("(format unknown)") : group->getDebugCurrentFormat().c_str(),
                                static_cast<unsigned long long>(group->getDebugCurrentTarget()));
                } else {
                    ImGui::TextUnformatted(Tr("(none)"));
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text(Tr("Last successful injection"));
                ImGui::TableNextColumn();
                if (group->getDebugEffectRenderCalls() > 0) {
                    ImGui::Text("%ux%u -> %ux%u",
                                group->getDebugSceneWidth(),
                                group->getDebugSceneHeight(),
                                group->getDebugEffectWidth(),
                                group->getDebugEffectHeight());
                } else {
                    ImGui::TextUnformatted(Tr("(none yet)"));
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text(Tr("Last successful staging"));
                ImGui::TableNextColumn();
                if (group->getDebugEffectRenderCalls() == 0) {
                    ImGui::TextUnformatted(Tr("(none)"));
                } else if (group->getDebugNativeStaging() && deviceApi == reshade::api::device_api::vulkan) {
                    ImGui::TextUnformatted(Tr("Vulkan image blit"));
                } else if (group->getDebugNativeStaging()) {
                    ImGui::TextUnformatted(Tr("Fullscreen shader copy"));
                } else {
                    ImGui::TextUnformatted(Tr("Direct"));
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text(Tr("Last successful techniques"));
                ImGui::TableNextColumn();
                if (group->getDebugEffectRenderCalls() > 0)
                    ImGui::Text("%u | %s",
                                group->getDebugLastRenderedTechniqueCount(),
                                group->getDebugLastTechniqueOrder().empty() ? Tr("(none)") : group->getDebugLastTechniqueOrder().c_str());
                else
                    ImGui::TextUnformatted(Tr("(none)"));

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text(Tr("Successful renders"));
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(group->getDebugEffectRenderCalls()));

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text(Tr("Diagnostics"));
                ImGui::TableNextColumn();
                if (ImGui::Button(Tr("Copy diagnostics"))) {
                    const char* apiName = deviceApi == reshade::api::device_api::d3d10 ? "D3D10" :
                                          deviceApi == reshade::api::device_api::d3d11 ? "D3D11" :
                                          deviceApi == reshade::api::device_api::d3d12 ? "D3D12" : "Vulkan";
                    const std::string diagnostics = TrFmt(
                      "REST {}\n"
                      "Group: {}\n"
                      "API: {}\n"
                      "Auto Scene Colour: active\n"
                      "\nCurrent candidate / latest attempt\n"
                      "Status: {}\n"
                      "Target: {}x{} | {} | 0x{:x}\n"
                      "\nLast successful injection\n"
                      "Scene -> effect: {}x{} -> {}x{}\n"
                      "Staging path: {}\n"
                      "Vulkan boundary: {}\n"
                      "Techniques: {}\n"
                      "Technique order: {}\n"
                      "Successful renders: {}\n"
                      "Last successful target: 0x{:x}",
                      REST_VERSION_STRING,
                      group->getName(),
                      apiName,
                      group->getDebugAutoStatus().empty() ? Tr("(none)") : TranslateAutoStatus(group->getDebugAutoStatus()),
                      group->getDebugCurrentSceneWidth(),
                      group->getDebugCurrentSceneHeight(),
                      group->getDebugCurrentFormat().empty() ? Tr("(unknown)") : group->getDebugCurrentFormat(),
                      static_cast<unsigned long long>(group->getDebugCurrentTarget()),
                      group->getDebugSceneWidth(),
                      group->getDebugSceneHeight(),
                      group->getDebugEffectWidth(),
                      group->getDebugEffectHeight(),
                      group->getDebugEffectRenderCalls() == 0 ? Tr("(none)") :
                        (group->getDebugNativeStaging() ?
                          (deviceApi == reshade::api::device_api::vulkan ? Tr("Vulkan image blit") : Tr("Fullscreen shader copy")) :
                          Tr("Direct")),
                      deviceApi == reshade::api::device_api::vulkan ?
                        (group->getDebugLastVulkanBoundary().empty() ? Tr("(none yet)") : Tr(group->getDebugLastVulkanBoundary().c_str())) :
                        Tr("not applicable"),
                      group->getDebugLastRenderedTechniqueCount(),
                      group->getDebugLastTechniqueOrder().empty() ? Tr("(none)") : group->getDebugLastTechniqueOrder(),
                      static_cast<unsigned long long>(group->getDebugEffectRenderCalls()),
                      static_cast<unsigned long long>(group->getDebugLastRenderTarget()));
                    ImGui::SetClipboardText(diagnostics.c_str());
                }

                const auto recentCandidates = group->getDebugAutoHistory();
                if (!recentCandidates.empty() && ImGui::TreeNode(Tr("Recent candidates"))) {
                    for (auto it = recentCandidates.rbegin(); it != recentCandidates.rend(); ++it) {
                        const auto& entry = *it;
                        if (entry.successfulRenders > 0) {
                            ImGui::Text(Tr("#%llu 0x%08x | %ux%u | %s | %llu renders"),
                                        static_cast<unsigned long long>(entry.candidateId),
                                        entry.shaderHash,
                                        entry.sceneWidth,
                                        entry.sceneHeight,
                                        entry.status.empty() ? Tr("(no status)") : TranslateAutoStatus(entry.status).c_str(),
                                        static_cast<unsigned long long>(entry.successfulRenders));
                        } else {
                            ImGui::Text("#%llu 0x%08x | %ux%u | %s",
                                        static_cast<unsigned long long>(entry.candidateId),
                                        entry.shaderHash,
                                        entry.sceneWidth,
                                        entry.sceneHeight,
                                        entry.status.empty() ? Tr("(no status)") : TranslateAutoStatus(entry.status).c_str());
                        }

                        if (entry.target != 0) {
                            ImGui::TextDisabled(Tr("Target 0x%llx | %s%s%s"),
                                                static_cast<unsigned long long>(entry.target),
                                                entry.format.empty() ? Tr("(format unknown)") : entry.format.c_str(),
                                                entry.boundary.empty() ? "" : " | ",
                                                entry.boundary.empty() ? "" : Tr(entry.boundary.c_str()));
                        }
                    }
                    ImGui::TreePop();
                }
            } else {
                if (supportsSRVwrite) {
                    ImGui::TableNextColumn();
                    ImGui::Text(Tr("Render destination"));
                    ImGui::TableNextColumn();
                    if (ImGui::BeginCombo("##Renderdestination", typeSelectedDestItem, ImGuiComboFlags_None)) {
                        for (int n = 0; n < IM_ARRAYSIZE(typeDestItems); n++) {
                            const bool is_selected = (typeSelectedDestItem == typeDestItems[n]);
                            if (ImGui::Selectable(typeDestItems[n], is_selected)) {
                                typeSelectedDestItem = typeDestItems[n];
                                selectedDestIndex = n;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TableNextRow();
                } else {
                    selectedDestIndex = 0;
                }

                if (supportsSRVwrite && selectedDestIndex == 1) {
                    if (!instance.GetTrackDescriptors()) {
                        ImGui::BeginDisabled();
                        group->setRenderToResourceViews(false);
                    } else {
                        group->setRenderToResourceViews(true);
                    }

                    ImGui::TableNextColumn();
                    ImGui::Text(Tr("Shader Stage"));
                    ImGui::TableNextColumn();
                    if (ImGui::BeginCombo("##RenderShaderStage", selectedStage, ImGuiComboFlags_None)) {
                        for (int n = 0; n < IM_ARRAYSIZE(stageItems); n++) {
                            const bool is_selected = (selectedStage == stageItems[n]);
                            if (ImGui::Selectable(stageItems[n], is_selected)) {
                                selectedStageIndex = n;
                                selectedStage = stageItems[n];
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    group->setRenderSRVShaderStage(selectedStageIndex);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text(Tr("Slot"));
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", group->getRenderSRVSlotIndex());
                    ImGui::SameLine();
                    ImGui::PushID(0);
                    if (ImGui::SmallButton("+"))
                        group->setRenderSRVSlotIndex(group->getRenderSRVSlotIndex() + 1);
                    ImGui::PopID();
                    if (group->getRenderSRVSlotIndex() != 0) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("-"))
                            group->setRenderSRVSlotIndex(group->getRenderSRVSlotIndex() - 1);
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text(Tr("Binding"));
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", group->getRenderSRVDescriptorIndex());
                    ImGui::SameLine();
                    ImGui::PushID(2);
                    if (ImGui::SmallButton("+"))
                        group->setRenderSRVDescriptorIndex(group->getRenderSRVDescriptorIndex() + 1);
                    ImGui::PopID();
                    if (group->getRenderSRVDescriptorIndex() != 0) {
                        ImGui::SameLine();
                        ImGui::PushID(1);
                        if (ImGui::SmallButton("-"))
                            group->setRenderSRVDescriptorIndex(group->getRenderSRVDescriptorIndex() - 1);
                        ImGui::PopID();
                    }

                    if (!instance.GetTrackDescriptors())
                        ImGui::EndDisabled();
                } else {
                    group->setRenderToResourceViews(false);

                    ImGui::TableNextColumn();
                    ImGui::Text(Tr("Render target index"));
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", group->getRenderTargetIndex());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+"))
                        group->setRenderTargetIndex(group->getRenderTargetIndex() + 1);
                    if (group->getRenderTargetIndex() != 0) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("-"))
                            group->setRenderTargetIndex(group->getRenderTargetIndex() - 1);
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text(Tr("Invocation location"));
                    ImGui::TableNextColumn();
                    if (ImGui::BeginCombo("##Invocationlocation", typeSelectedItem, ImGuiComboFlags_None)) {
                        for (int n = 0; n < IM_ARRAYSIZE(invocationDescription); n++) {
                            const bool is_selected = (typeSelectedItem == invocationDescription[n]);
                            if (ImGui::Selectable(invocationDescription[n], is_selected)) {
                                typeSelectedItem = invocationDescription[n];
                                selectedIndex = n;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text(Tr("Retry RT assignment"));
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##RetryRTassignment", &retry);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text(Tr("Apply tone map clamping"));
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##tonemap", &tonemap);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(Tr("Optional lossy ACES compression for linear HDR effects.\n"
                                  "Clips highlights and does not preserve negative wide-gamut values.\n"
                                  "Not an HDR10/PQ or HLG conversion; leave off for those targets."));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text(Tr("Flip render target"));
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##flipbuffer", &flipbuffer);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text(Tr("Preserve target alpha channel"));
            ImGui::TableNextColumn();
            const bool preserveAlphaSupported = Rendering::SupportsShaderCopy(deviceApi);
            if (autoSceneColourActive || !preserveAlphaSupported)
                ImGui::BeginDisabled();
            DrawTableToggleSwitch("##preserveAlpha", &preserveAlpha);
            if (autoSceneColourActive || !preserveAlphaSupported) {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled(autoSceneColourActive ? Tr("ignored while Auto scene colour is active") : Tr("unavailable on this renderer"));
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text(Tr("Match swapchain"));
            ImGui::TableNextColumn();
            if (autoSceneColourActive) {
                ImGui::TextUnformatted(Tr("ASPECT RATIO (automatic)"));
            } else if (ImGui::BeginCombo("##effSwapChainMatchMode", typesSelectedSwapchainMatchMode, ImGuiComboFlags_None)) {
                for (int n = 0; n < IM_ARRAYSIZE(swapchainMatchOptions); n++) {
                    const bool is_selected = (typesSelectedSwapchainMatchMode == swapchainMatchOptions[n]);
                    if (ImGui::Selectable(swapchainMatchOptions[n], is_selected)) {
                        typesSelectedSwapchainMatchMode = swapchainMatchOptions[n];
                        selectedSwapchainMatchMode = n;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::EndTable();
        }

        group->setRequeueAfterRTMatchingFailure(retry);
        group->setMatchSwapchainResolution(selectedSwapchainMatchMode);
        group->setAutoRenderSRV(autoSceneColour);
        group->setInvocationLocation(selectedIndex);
        group->setToneMap(tonemap);
        group->setPreserveAlpha(preserveAlpha);
        group->setFlipBuffer(flipbuffer);

        ImGui::Separator();
        DisplayTechniqueSelection(runtime, instance, group);

        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    ImGui::PushID(4);
    ImGui::Button("", ImVec2(-1, 8.0f));
    ImGui::PopID();
    if (ImGui::IsItemActive())
        height += ImGui::GetIO().MouseDelta.y;

    DisplayPreview(instance, resManager, runtime, group);

    ImGui::PopStyleVar();
}

static void DisplayGroupView(AddonImGui::AddonUIData& instance,
                             Rendering::ResourceManager& resManager,
                             reshade::api::effect_runtime* runtime,
                             ShaderToggler::ToggleGroup* group,
                             ShaderToggler::ShaderManager* shaderManager) {
    if (!shaderManager->isInHuntingMode()) {
        ImGui::TextDisabled(Tr("Shader hunting is not active."));
        ImGui::TextWrapped(Tr("The group's committed shader hashes remain active while you inspect Auto Scene Colour and other settings."));
        return;
    }

    if (*instance.ActiveCollectorFrameCounter() > 0) {
        ImGui::Text(Tr("Collecting active shaders... %u frames remaining"), instance.ActiveCollectorFrameCounter()->load());
        ImGui::TextDisabled(Tr("Keep the relevant scene visible until collection finishes."));
        return;
    }

    auto& huntingUI = instance.GetHuntingUIState();
    char* shaderSearch = huntingUI.shaderSearch;
    int& filterMode = huntingUI.filterMode;
    const char* filterItems[] = { Tr("All"), Tr("Marked"), Tr("Unmarked") };

    // Search/filter controls use a table so the search box absorbs resize changes
    // instead of forcing the fixed controls off the edge of a narrow pane.
    if (ImGui::BeginTable("ShaderHuntSearch", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn(Tr("Search"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(Tr("Filter"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn(Tr("Rescan"), ImGuiTableColumnFlags_WidthFixed, 72.0f);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##shaderSearch", Tr("Search shader hash..."), shaderSearch, 64);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##shaderFilter", &filterMode, filterItems, IM_ARRAYSIZE(filterItems));

        ImGui::TableNextColumn();
        if (ImGui::Button(Tr("Rescan"), ImVec2(-1.0f, 0))) {
            auto* pixelManager = instance.GetPixelShaderManager();
            auto* vertexManager = instance.GetVertexShaderManager();
            auto* computeManager = instance.GetComputeShaderManager();
            pixelManager->startHuntingMode(pixelManager->getMarkedShaderHashes());
            vertexManager->startHuntingMode(vertexManager->getMarkedShaderHashes());
            computeManager->startHuntingMode(computeManager->getMarkedShaderHashes());
            *instance.ActiveCollectorFrameCounter() = *instance.StartValueFramecountCollectionPhase();
            instance.UpdateToggleGroupsForShaderHashes();
            ImGui::EndTable();
            return;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(Tr("Recollect active shaders for the configured number of frames."));

        ImGui::EndTable();
    }

    auto repeatButton = [](const char* label) {
        ImGui::PushButtonRepeat(true);
        const bool pressed = ImGui::Button(label, ImVec2(-1.0f, 0));
        ImGui::PopButtonRepeat();
        return pressed;
    };

    bool navigationChanged = false;

    // Navigation row.
    if (ImGui::BeginTable("ShaderHuntNavigation", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableNextColumn();
        if (repeatButton(Tr("Prev"))) {
            shaderManager->huntPreviousShader(false);
            navigationChanged = true;
        }

        ImGui::TableNextColumn();
        if (repeatButton(Tr("Next"))) {
            shaderManager->huntNextShader(false);
            navigationChanged = true;
        }

        ImGui::TableNextColumn();
        if (repeatButton(Tr("Prev marked"))) {
            shaderManager->huntPreviousShader(true);
            navigationChanged = true;
        }

        ImGui::TableNextColumn();
        if (repeatButton(Tr("Next marked"))) {
            shaderManager->huntNextShader(true);
            navigationChanged = true;
        }

        ImGui::EndTable();
    }

    const size_t markedCount = shaderManager->getMarkedShaderCount();
    const uint32_t activeHuntedHash = shaderManager->getActiveHuntedShaderHash();

    // Mark/action row. Keeping these actions together avoids attaching controls to
    // the status text and keeps the toolbar predictable as the pane is resized.
    if (ImGui::BeginTable("ShaderHuntActions", 5, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableNextColumn();
        if (ImGui::Button(Tr("Mark / unmark"), ImVec2(-1.0f, 0))) {
            shaderManager->toggleMarkOnHuntedShader();
            navigationChanged = true;
        }

        ImGui::TableNextColumn();
        if (repeatButton(Tr("Mark + Prev"))) {
            shaderManager->toggleMarkOnHuntedShader();
            shaderManager->huntPreviousShader(false);
            navigationChanged = true;
        }

        ImGui::TableNextColumn();
        if (repeatButton(Tr("Mark + Next"))) {
            shaderManager->toggleMarkOnHuntedShader();
            shaderManager->huntNextShader(false);
            navigationChanged = true;
        }

        ImGui::TableNextColumn();
        if (activeHuntedHash == 0)
            ImGui::BeginDisabled();
        if (ImGui::Button(Tr("Copy hash"), ImVec2(-1.0f, 0))) {
            const std::string hashText = std::format("0x{:08x}", activeHuntedHash);
            ImGui::SetClipboardText(hashText.c_str());
        }
        if (activeHuntedHash == 0)
            ImGui::EndDisabled();

        ImGui::TableNextColumn();
        if (markedCount == 0)
            ImGui::BeginDisabled();
        if (ImGui::Button(Tr("Clear marked"), ImVec2(-1.0f, 0))) {
            shaderManager->clearMarkedShaderHashes();
            navigationChanged = true;
        }
        if (markedCount == 0)
            ImGui::EndDisabled();

        ImGui::EndTable();
    }

    if (navigationChanged)
        instance.UpdateToggleGroupsForShaderHashes();

    const std::vector<uint32_t> hashes = shaderManager->getCollectedShaderHashesOrdered();
    const std::unordered_set<uint32_t> collectedSet(hashes.begin(), hashes.end());
    const std::unordered_set<uint32_t> markedHashes = shaderManager->getMarkedShaderHashes();

    std::vector<uint32_t> missingMarkedHashes;
    missingMarkedHashes.reserve(markedHashes.size());
    for (const uint32_t hash : markedHashes) {
        if (!collectedSet.contains(hash))
            missingMarkedHashes.push_back(hash);
    }
    std::sort(missingMarkedHashes.begin(), missingMarkedHashes.end());

    ImGui::TextDisabled(Tr("%zu collected | %zu marked | %zu not seen"),
                        hashes.size(),
                        markedCount,
                        missingMarkedHashes.size());
    if (!missingMarkedHashes.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
        ImGui::TextUnformatted(Tr("Red = marked but not observed during the latest collection pass."));
        ImGui::PopStyleColor();
    }
    ImGui::TextWrapped(Tr("Pending shader marks are applied to the group when you click Done hunting."));
    ImGui::Separator();

    const uint32_t selectedHash = shaderManager->getActiveHuntedShaderHash();

    std::string needle(shaderSearch);
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    struct ShaderListEntry {
        uint32_t hash;
        bool collected;
    };

    // Collected hashes keep ShaderManager's stable first-seen order so the list,
    // Prev/Next and marked navigation continue to agree. Marked hashes not seen in
    // the latest collection are appended for visibility only and never enter
    // hunting navigation or preview selection.
    std::vector<ShaderListEntry> visibleHashes;
    visibleHashes.reserve(hashes.size() + missingMarkedHashes.size());

    auto addIfVisible = [&](uint32_t hash, bool collected) {
        const bool marked = shaderManager->isHuntedShaderMarked(hash);
        const std::string hashText = std::format("{:#08x}", hash);

        bool visible = filterMode == 0 || (filterMode == 1 && marked) || (filterMode == 2 && collected && !marked);
        if (visible && !needle.empty()) {
            std::string haystack = hashText;
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            visible = haystack.find(needle) != std::string::npos;
        }

        if (visible)
            visibleHashes.push_back({ hash, collected });
    };

    for (const uint32_t hash : hashes)
        addIfVisible(hash, true);
    for (const uint32_t hash : missingMarkedHashes)
        addIfVisible(hash, false);

    const float listWidth = ImGui::GetContentRegionAvail().x;
    const int hashColumns = visibleHashes.size() >= 50 && listWidth >= 500.0f ? 2 : 1;
    const float listHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 6.0f);

    if (ImGui::BeginTable("ShaderHashView",
                          hashColumns,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_NoBordersInBody,
                          ImVec2(0, listHeight))) {
        auto drawHash = [&](const ShaderListEntry& entry) {
            const uint32_t hash = entry.hash;
            const bool marked = shaderManager->isHuntedShaderMarked(hash);
            const bool missing = marked && !entry.collected;
            const std::string hashText = std::format("{:#08x}", hash);

            if (missing)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
            else if (marked)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

            const bool clicked =
              ImGui::Selectable(hashText.c_str(), entry.collected && selectedHash == hash, ImGuiSelectableFlags_AllowDoubleClick);

            if (entry.collected) {
                if (clicked && shaderManager->setActiveHuntedShaderHash(hash)) {
                    if (ImGui::IsMouseDoubleClicked(0))
                        shaderManager->toggleMarkOnHuntedShader();
                    instance.UpdateToggleGroupsForShaderHashes();
                }
            } else {
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(Tr("Marked shader not observed during the latest collection pass.\nIt may be scene/state dependent rather than invalid.\nDouble-click to unmark it."));
                if (clicked && ImGui::IsMouseDoubleClicked(0) && shaderManager->removeMarkedShaderHash(hash))
                    instance.UpdateToggleGroupsForShaderHashes();
            }

            if (missing || marked)
                ImGui::PopStyleColor();
        };

        if (hashColumns == 1) {
            for (const ShaderListEntry& entry : visibleHashes) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                drawHash(entry);
            }
        } else {
            // Preserve hunting order visually: top-to-bottom in the left column,
            // then continue at the top of the right column. Missing marked hashes
            // are appended after collected hashes and are display-only.
            const size_t rows = (visibleHashes.size() + 1) / 2;
            for (size_t row = 0; row < rows; ++row) {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                drawHash(visibleHashes[row]);

                const size_t rightIndex = row + rows;
                if (rightIndex < visibleHashes.size()) {
                    ImGui::TableSetColumnIndex(1);
                    drawHash(visibleHashes[rightIndex]);
                }
            }
        }

        ImGui::EndTable();
    }
}

static void DisplayTextureBindings(AddonImGui::AddonUIData& instance,
                                   ShaderToggler::ToggleGroup* group,
                                   reshade::api::effect_runtime* runtime,
                                   Rendering::ResourceManager& resManager) {
    static float height = ImGui::GetWindowHeight();
    float width = ImGui::GetWindowWidth();

    const char* typeItems[] = { Tr("Render target"), Tr("Shader Resource View") };
    uint32_t selectedIndex = group->getExtractResourceViews() ? 1 : 0;
    const char* typeSelectedItem = typeItems[selectedIndex];
    DeviceDataContainer& deviceData = *runtime->get_device()->get_private_data<DeviceDataContainer>();

    static const char* swapchainMatchOptions[] = { Tr("RESOLUTION"), Tr("ASPECT RATIO"), Tr("EXTENDED ASPECT RATIO"), Tr("NONE") };
    uint32_t selectedSwapchainMatchMode = group->getBindingMatchSwapchainResolution();
    const char* typesSelectedSwapchainMatchMode = swapchainMatchOptions[selectedSwapchainMatchMode];

    static const char* stageItems[] = { Tr("PIXEL"), Tr("VERTEX"), Tr("COMPUTE") };
    uint32_t selectedStageIndex = group->getSRVShaderStage();
    const char* selectedStage = stageItems[selectedStageIndex];

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    if (ImGui::BeginChild("Texture bindings viewer", { 0, height / 2.0f }, true, ImGuiChildFlags_AlwaysAutoResize)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

        // Name of group
        char tmpBuffer[150];

        // Name of Binding
        bool isBindingEnabled = group->isProvidingTextureBinding();

        const std::string& bindingName = group->getTextureBindingName();
        strncpy_s(tmpBuffer, 150, bindingName.c_str(), bindingName.size());

        bool copyBinding = group->getCopyTextureBinding();
        bool clearBinding = group->getClearBindings();
        bool flipBinding = group->getFlipBufferBinding();

        if (ImGui::BeginTable("Bindingsettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody)) {
            ImGui::TableSetupColumn("##BindingColumnSetup", ImGuiTableColumnFlags_WidthFixed, ImGui::GetWindowWidth() / 3);

            ImGui::TableNextColumn();
            ImGui::Text(Tr("Texture binding enabled"));
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##Texturebindingenabled", &isBindingEnabled);

            if (!isBindingEnabled) {
                ImGui::BeginDisabled();
                group->setProvidingTextureBinding(false);
            } else {
                group->setProvidingTextureBinding(true);
            }

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text(Tr("Texture semantic"));
            ImGui::TableNextColumn();
            ImGui::InputText("##BindingName", tmpBuffer, 149);

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text(Tr("Texture source"));
            ImGui::TableNextColumn();
            if (ImGui::BeginCombo("##Bindingsource", typeSelectedItem, ImGuiComboFlags_None)) {
                for (int n = 0; n < IM_ARRAYSIZE(typeItems); n++) {
                    bool is_selected = (typeSelectedItem == typeItems[n]);
                    if (ImGui::Selectable(typeItems[n], is_selected)) {
                        typeSelectedItem = typeItems[n];
                        selectedIndex = n;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text(Tr("Create texture copy for binding"));
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##Copybinding", &copyBinding);

            ImGui::TableNextRow();

            ImGui::BeginDisabled(!copyBinding);
            ImGui::TableNextColumn();
            ImGui::Text(Tr("Flip binding texture"));
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##flipbinding", &flipBinding);
            ImGui::EndDisabled();

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text(Tr("Clear binding on hash miss"));
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##Clearbinding", &clearBinding);

            ImGui::TableNextRow();

            ImGui::TableNextColumn();

            ImGui::EndTable();
        }

        group->setTextureBindingName(tmpBuffer);
        group->setCopyTextureBinding(copyBinding);
        group->setClearBindings(clearBinding);
        group->setFlipBufferBinding(flipBinding);

        ImGui::Separator();

        if (ImGui::BeginTable("BindingSourcesettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody)) {
            ImGui::TableSetupColumn("##BindingSourceColumnSetup", ImGuiTableColumnFlags_WidthFixed, ImGui::GetWindowWidth() / 3);

            if (selectedIndex == 1) {
                if (!instance.GetTrackDescriptors()) {
                    ImGui::BeginDisabled();
                    group->setExtractResourceViews(false);
                } else {
                    group->setExtractResourceViews(true);
                }

                ImGui::TableNextColumn();
                ImGui::Text(Tr("Shader Stage"));
                ImGui::TableNextColumn();
                if (ImGui::BeginCombo("##ShaderStage", selectedStage, ImGuiComboFlags_None)) {
                    for (int n = 0; n < IM_ARRAYSIZE(stageItems); n++) {
                        bool is_selected = (selectedStage == stageItems[n]);
                        if (ImGui::Selectable(stageItems[n], is_selected)) {
                            selectedStageIndex = n;
                            selectedStage = stageItems[n];
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                group->setSRVShaderStage(selectedStageIndex);

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text(Tr("Slot"));
                ImGui::TableNextColumn();
                ImGui::Text("%u", group->getBindingSRVSlotIndex());
                ImGui::SameLine();
                ImGui::PushID(0);
                if (ImGui::SmallButton("+")) {
                    group->setBindingSRVSlotIndex(group->getBindingSRVSlotIndex() + 1);
                }
                ImGui::PopID();

                if (group->getBindingSRVSlotIndex() != 0) {
                    ImGui::SameLine();

                    if (ImGui::SmallButton("-")) {
                        group->setBindingSRVSlotIndex(group->getBindingSRVSlotIndex() - 1);
                    }
                }

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text(Tr("Binding"));
                ImGui::TableNextColumn();
                ImGui::Text("%u", group->getBindingSRVDescriptorIndex());
                ImGui::SameLine();
                ImGui::PushID(2);
                if (ImGui::SmallButton("+")) {
                    group->dispatchSRVCycle(ShaderToggler::CYCLE_UP);
                }
                ImGui::PopID();

                if (group->getBindingSRVDescriptorIndex() != 0) {
                    ImGui::SameLine();

                    ImGui::PushID(1);
                    if (ImGui::SmallButton("-")) {
                        group->dispatchSRVCycle(ShaderToggler::CYCLE_DOWN);
                    }
                    ImGui::PopID();
                }

                if (!instance.GetTrackDescriptors()) {
                    ImGui::EndDisabled();
                }
            } else {
                group->setExtractResourceViews(false);

                const char* rtTypeSelectedItem = invocationDescription[group->getBindingInvocationLocation()];
                uint32_t rtSelectedIndex = group->getBindingInvocationLocation();

                ImGui::TableNextColumn();
                ImGui::Text(Tr("Render target index"));
                ImGui::TableNextColumn();
                ImGui::Text("%u", group->getBindingRenderTargetIndex());
                ImGui::SameLine();

                ImGui::PushID(0);
                if (ImGui::SmallButton("+")) {
                    group->setBindingRenderTargetIndex(group->getBindingRenderTargetIndex() + 1);
                }
                ImGui::PopID();

                if (group->getBindingRenderTargetIndex() != 0) {
                    ImGui::SameLine();

                    if (ImGui::SmallButton("-")) {
                        group->setBindingRenderTargetIndex(group->getBindingRenderTargetIndex() - 1);
                    }
                }

                ImGui::TableNextRow();

                if (!copyBinding) {
                    ImGui::BeginDisabled();
                }

                ImGui::TableNextColumn();
                ImGui::Text(Tr("Invocation location"));
                ImGui::TableNextColumn();
                if (ImGui::BeginCombo("##Invocationlocation", rtTypeSelectedItem, ImGuiComboFlags_None)) {
                    for (int n = 0; n < IM_ARRAYSIZE(invocationDescription); n++) {
                        bool is_selected = (rtTypeSelectedItem == invocationDescription[n]);
                        if (ImGui::Selectable(invocationDescription[n], is_selected)) {
                            rtTypeSelectedItem = invocationDescription[n];
                            rtSelectedIndex = n;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (!copyBinding) {
                    ImGui::EndDisabled();
                }

                ImGui::TableNextColumn();
                ImGui::Text(Tr("Match swapchain"));
                ImGui::TableNextColumn();
                if (ImGui::BeginCombo("##swapChainMatchMode", typesSelectedSwapchainMatchMode, ImGuiComboFlags_None)) {
                    for (int n = 0; n < IM_ARRAYSIZE(swapchainMatchOptions); n++) {
                        bool is_selected = (typesSelectedSwapchainMatchMode == swapchainMatchOptions[n]);
                        if (ImGui::Selectable(swapchainMatchOptions[n], is_selected)) {
                            typesSelectedSwapchainMatchMode = swapchainMatchOptions[n];
                            selectedSwapchainMatchMode = n;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                group->setBindingInvocationLocation(rtSelectedIndex);
                group->setBindingMatchSwapchainResolution(selectedSwapchainMatchMode);
            }

            ImGui::EndTable();
        }

        if (!isBindingEnabled) {
            ImGui::EndDisabled();
        }

        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    ImGui::PushID(3);
    ImGui::Button("", ImVec2(-1, 8.0f));
    ImGui::PopID();
    if (ImGui::IsItemActive())
        height += ImGui::GetIO().MouseDelta.y;

    DisplayBindingPreview(instance, resManager, runtime, group);

    ImGui::PopStyleVar();
}

struct RegionSelectionTarget {
    int groupId;
    uint32_t stage;
    uint32_t hash;
};

static std::optional<RegionSelectionTarget> regionSelection;


// Full-screen selection mask: the boxes are drawn directly over the game. Left-drag adds a box,
// right-click (or Esc) leaves selection mode. The mask also makes sure the game area behind the
// ReShade window is reachable.
static void DisplayRegionSelectionOverlay(AddonImGui::AddonUIData& instance) {
    static bool s_dragging = false;
    static ImVec2 s_dragStart = ImVec2(0.0f, 0.0f);

    if (!regionSelection) {
        s_dragging = false;
        return;
    }

    const int groupId = instance.GetToggleGroupIdShaderEditing();
    auto groupIt = instance.GetToggleGroups().find(groupId);
    if (groupId < 0 || groupIt == instance.GetToggleGroups().end()) {
        s_dragging = false;
        regionSelection.reset();
        return;
    }
    ShaderToggler::ToggleGroup* group = &groupIt->second;

    auto& huntingUIState = instance.GetHuntingUIState();
    const uint32_t selectedIndex = std::min<uint32_t>(huntingUIState.selectedShaderType, 2);
    ShaderToggler::ShaderManager* shaderManager = selectedIndex == 0 ? instance.GetPixelShaderManager() : (selectedIndex == 1 ? instance.GetVertexShaderManager() : instance.GetComputeShaderManager());
    const uint32_t activeHash = shaderManager->getActiveHuntedShaderHash();
    if (activeHash != regionSelection->hash || selectedIndex != regionSelection->stage || groupId != regionSelection->groupId) {
        s_dragging = false;
        regionSelection.reset();
        return;
    }

    ShaderToggler::ShaderDrawSettings drawSettings;
    group->GetShaderDrawSettings(selectedIndex, activeHash, drawSettings);
    if (!drawSettings.spatialFilterEnabled) {
        s_dragging = false;
        regionSelection.reset();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    if (display.x <= 0.0f || display.y <= 0.0f)
        return;

    const auto commit = [&]() {
        group->SetShaderDrawSettings(selectedIndex, activeHash, drawSettings);

    };

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(display);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool open = ImGui::Begin("##RESTRegionSelection", nullptr,
                                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoBackground);
    if (open) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->AddRectFilled(ImVec2(0.0f, 0.0f), display, IM_COL32(0, 0, 0, 120));

        ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
        ImGui::InvisibleButton("##RegionSelectionArea", display, ImGuiButtonFlags_MouseButtonLeft);

        for (const ShaderToggler::ScreenRegion& region : drawSettings.spatialRegions) {
            const ImVec2 regionMin(region.minX * display.x, region.minY * display.y);
            const ImVec2 regionMax(region.maxX * display.x, region.maxY * display.y);
            drawList->AddRectFilled(regionMin, regionMax, IM_COL32(255, 50, 50, 90));
            drawList->AddRect(regionMin, regionMax, IM_COL32(255, 70, 70, 255), 0.0f, 0, 2.0f);
        }

        const ImVec2 mouse = io.MousePos;
        if (ImGui::IsItemActivated()) {
            s_dragging = true;
            s_dragStart = mouse;
        }
        if (s_dragging && ImGui::IsItemActive()) {
            const ImVec2 a(std::clamp(std::min(s_dragStart.x, mouse.x), 0.0f, display.x), std::clamp(std::min(s_dragStart.y, mouse.y), 0.0f, display.y));
            const ImVec2 b(std::clamp(std::max(s_dragStart.x, mouse.x), 0.0f, display.x), std::clamp(std::max(s_dragStart.y, mouse.y), 0.0f, display.y));
            drawList->AddRectFilled(a, b, IM_COL32(255, 70, 70, 80));
            drawList->AddRect(a, b, IM_COL32(255, 70, 70, 255), 0.0f, 0, 2.0f);
        }
        if (s_dragging && ImGui::IsItemDeactivated()) {
            s_dragging = false;
            ShaderToggler::ScreenRegion region;
            region.minX = std::clamp(std::min(s_dragStart.x, mouse.x) / display.x, 0.0f, 1.0f);
            region.minY = std::clamp(std::min(s_dragStart.y, mouse.y) / display.y, 0.0f, 1.0f);
            region.maxX = std::clamp(std::max(s_dragStart.x, mouse.x) / display.x, 0.0f, 1.0f);
            region.maxY = std::clamp(std::max(s_dragStart.y, mouse.y) / display.y, 0.0f, 1.0f);
            if (region.isValid() && drawSettings.spatialRegions.size() < 32 && ShaderToggler::addScreenRegion(drawSettings.spatialRegions, region))
                commit();
        }

        const char* const hint = Tr("Left-drag to select regions\nRight-click to exit selection");
        const ImVec2 hintPos(20.0f, 20.0f);
        const ImVec2 hintSize = ImGui::CalcTextSize(hint);
        drawList->AddRectFilled(ImVec2(hintPos.x - 12.0f, hintPos.y - 10.0f), ImVec2(hintPos.x + hintSize.x + 12.0f, hintPos.y + hintSize.y + 10.0f), IM_COL32(0, 0, 0, 170), 6.0f);
        ImGui::SetCursorScreenPos(hintPos);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.95f), "%s", hint);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            s_dragging = false;
            regionSelection.reset();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

static void DisplayShaderDrawSettings(AddonImGui::AddonUIData& instance, ShaderToggler::ToggleGroup* group,
                                      ShaderToggler::ShaderManager* selectedShaderManager, uint32_t stage, reshade::api::device_api api) {
    const uint32_t activeShaderHash = selectedShaderManager->getActiveHuntedShaderHash();
    const bool drawSettingsDisabled = activeShaderHash == 0;
    ShaderToggler::ShaderDrawSettings drawSettings;
    if (!drawSettingsDisabled)
        group->GetShaderDrawSettings(stage, activeShaderHash, drawSettings);

    // Stores the options for the shader currently selected in the hunting list. Enabling
    // one of them on an unmarked shader marks that shader so the setting is kept.
    const auto applyDrawSettings = [&]() {
        group->SetShaderDrawSettings(stage, activeShaderHash, drawSettings);
        if (!drawSettings.isDefault() && !selectedShaderManager->isHuntedShaderMarked(activeShaderHash))
            selectedShaderManager->toggleMarkOnHuntedShader();
        instance.UpdateToggleGroupsForShaderHashes();
    };

    if (drawSettingsDisabled)
        ImGui::BeginDisabled();

    bool suppressDraw = drawSettings.suppressDrawCall;
    if (DrawToggleRow("Suppress draw calls", &suppressDraw,
                      "When enabled and this group is active, draw calls using this shader are suppressed directly on the GPU.",
                      nullptr,
                      4.0f)) {
        drawSettings.suppressDrawCall = suppressDraw;
        applyDrawSettings();
    }

    if (stage < 2 && ImGui::TreeNode(Tr("Geometry Refine Filters"))) {
        // The geometry filters only restrict "Suppress draw calls"; without it they have
        // no effect, so keep them read-only.
        const bool filtersEnabled = drawSettings.suppressDrawCall;
        if (!filtersEnabled)
            ImGui::BeginDisabled();

        bool matchIdx = drawSettings.matchIndexCount;
        if (DrawToggleRow("Filter by index count", &matchIdx, "Only trigger for draw calls with index count in specified range.", nullptr, 4.0f)) {
            drawSettings.matchIndexCount = matchIdx;
            applyDrawSettings();
        }
        if (matchIdx) {
            ImGui::Indent(16.0f);
            int idxMin = static_cast<int>(drawSettings.indexCountMin);
            int idxMax = static_cast<int>(drawSettings.indexCountMax);
            ImGui::AlignTextToFramePadding();
            ImGui::Text(Tr("Min:"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("##IdxMinOverlay", &idxMin, 0)) {
                drawSettings.indexCountMin = static_cast<uint32_t>(std::max(0, idxMin));
                applyDrawSettings();
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::Text(Tr("Max:"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("##IdxMaxOverlay", &idxMax, 0)) {
                drawSettings.indexCountMax = static_cast<uint32_t>(std::max(0, idxMax));
                applyDrawSettings();
            }
            ImGui::Unindent(16.0f);
        }

        bool matchVtx = drawSettings.matchVertexCount;
        if (DrawToggleRow("Filter by vertex count", &matchVtx, "Only trigger for draw calls with vertex count in specified range.", nullptr, 4.0f)) {
            drawSettings.matchVertexCount = matchVtx;
            applyDrawSettings();
        }
        if (matchVtx) {
            ImGui::Indent(16.0f);
            int vtxMin = static_cast<int>(drawSettings.vertexCountMin);
            int vtxMax = static_cast<int>(drawSettings.vertexCountMax);
            ImGui::AlignTextToFramePadding();
            ImGui::Text(Tr("Min:"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("##VtxMinOverlay", &vtxMin, 0)) {
                drawSettings.vertexCountMin = static_cast<uint32_t>(std::max(0, vtxMin));
                applyDrawSettings();
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::Text(Tr("Max:"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("##VtxMaxOverlay", &vtxMax, 0)) {
                drawSettings.vertexCountMax = static_cast<uint32_t>(std::max(0, vtxMax));
                applyDrawSettings();
            }
            ImGui::Unindent(16.0f);
        }

        bool matchInst = drawSettings.matchInstanceCount;
        if (DrawToggleRow("Filter by instance count", &matchInst, "Only trigger for draw calls with instance count in specified range.", nullptr, 4.0f)) {
            drawSettings.matchInstanceCount = matchInst;
            applyDrawSettings();
        }
        if (matchInst) {
            ImGui::Indent(16.0f);
            int instMin = static_cast<int>(drawSettings.instanceCountMin);
            int instMax = static_cast<int>(drawSettings.instanceCountMax);
            ImGui::AlignTextToFramePadding();
            ImGui::Text(Tr("Min:"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("##InstMinOverlay", &instMin, 0)) {
                drawSettings.instanceCountMin = static_cast<uint32_t>(std::max(0, instMin));
                applyDrawSettings();
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::Text(Tr("Max:"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("##InstMaxOverlay", &instMax, 0)) {
                drawSettings.instanceCountMax = static_cast<uint32_t>(std::max(0, instMax));
                applyDrawSettings();
            }
            ImGui::Unindent(16.0f);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const bool supportsRegionSelection = api == reshade::api::device_api::d3d11 || api == reshade::api::device_api::d3d12;
        if (!supportsRegionSelection)
            ImGui::TextDisabled("%s", Tr("Screen selection requires D3D11 or D3D12."));
        ImGui::BeginDisabled(!supportsRegionSelection);
        bool spatialFilter = drawSettings.spatialFilterEnabled;
        if (DrawToggleRow("Hide by screen selection", &spatialFilter,
                          "Only hide the parts of this shader that are inside the selected regions. Select the regions directly over the game screen.",
                          nullptr,
                          4.0f)) {
            drawSettings.spatialFilterEnabled = spatialFilter;
            applyDrawSettings();
        }

        if (spatialFilter) {
            if (ImGui::Button(TrLabel("Select regions##SpatialRegions").c_str())) {
                regionSelection = RegionSelectionTarget{group->getId(), stage, activeShaderHash};
            }
            ImGui::SameLine();
            const bool noRegions = drawSettings.spatialRegions.empty();
            if (noRegions)
                ImGui::BeginDisabled();
            if (ImGui::Button(TrLabel("Clear regions##SpatialRegions").c_str())) {
                drawSettings.spatialRegions.clear();
                applyDrawSettings();
            }
            if (noRegions)
                ImGui::EndDisabled();

            const ImGuiIO& io = ImGui::GetIO();
            float previewAspect = 16.0f / 9.0f;
            if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f)
                previewAspect = io.DisplaySize.y / io.DisplaySize.x;

            const float canvasWidth = std::max(200.0f, ImGui::GetContentRegionAvail().x - RFX_CARD_PAD);
            const float canvasHeight = std::clamp(canvasWidth * previewAspect, 150.0f, 320.0f);
            const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
            const ImVec2 canvasEnd(canvasPos.x + canvasWidth, canvasPos.y + canvasHeight);
            ImGui::Dummy(ImVec2(canvasWidth, canvasHeight));

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(canvasPos, canvasEnd, IM_COL32(96, 96, 96, 255), 4.0f);
            for (const ShaderToggler::ScreenRegion& region : drawSettings.spatialRegions) {
                const ImVec2 regionMin(canvasPos.x + region.minX * canvasWidth, canvasPos.y + region.minY * canvasHeight);
                const ImVec2 regionMax(canvasPos.x + region.maxX * canvasWidth, canvasPos.y + region.maxY * canvasHeight);
                drawList->AddRectFilled(regionMin, regionMax, IM_COL32(255, 60, 60, 90));
                drawList->AddRect(regionMin, regionMax, IM_COL32(255, 80, 80, 240), 0.0f, 0, 1.5f);
            }
            drawList->AddRect(canvasPos, canvasEnd, rfx_u32(ImGuiCol_Border), 4.0f);
        }

        ImGui::EndDisabled();
        if (drawSettings.hasGeometryFilter()) {
            ImGui::Spacing();
            if (ImGui::Button(TrLabel("Clear Geometry Filters##Overlay").c_str())) {
                drawSettings.matchIndexCount = false;
                drawSettings.matchVertexCount = false;
                drawSettings.matchInstanceCount = false;
                drawSettings.indexCountMin = 0;
                drawSettings.indexCountMax = 0;
                drawSettings.vertexCountMin = 0;
                drawSettings.vertexCountMax = 0;
                drawSettings.instanceCountMin = 0;
                drawSettings.instanceCountMax = 0;
                applyDrawSettings();
            }
        }
        if (!filtersEnabled)
            ImGui::EndDisabled();

        ImGui::TreePop();
    }

    if (drawSettingsDisabled)
        ImGui::EndDisabled();

        const auto observedDraws = selectedShaderManager->getObservedDrawGeometries();
        const uint32_t activeHash = selectedShaderManager->getActiveHuntedShaderHash();
        if (stage < 2 && BeginCard("##observed_draws_pane", "OBSERVED DRAWS FOR ACTIVE SHADER")) {
            if (activeHash == 0) {
                ImGui::TextDisabled(Tr("No active shader selected in list."));
            } else if (observedDraws.empty()) {
                ImGui::TextDisabled(Tr("No draw calls observed for 0x%08X yet."), activeHash);
            } else {
                ImGui::TextDisabled(Tr("%zu draw variation(s) observed for 0x%08X:"), observedDraws.size(), activeHash);
            }

            const float boxHeight = 85.0f;
            if (ImGui::BeginChild("##observedDrawsScroll", ImVec2(0, boxHeight), true)) {
                if (activeHash == 0) {
                    ImGui::TextDisabled(Tr("Select a shader from the left hunting list."));
                } else if (observedDraws.empty()) {
                    ImGui::TextDisabled(Tr("Draw geometry stats will appear here when this shader renders."));
                } else {
                    for (size_t di = 0; di < observedDraws.size(); di++) {
                        const auto& d = observedDraws[di];
                        ImGui::PushID(static_cast<int>(di));
                        ImGui::BulletText(Tr("%s: %u %s (inst: %u, x%u)"),
                            d.isIndexed ? Tr("Indexed") : Tr("Non-idx"),
                            d.count,
                            d.isIndexed ? Tr("idx") : Tr("vtx"),
                            d.instanceCount,
                            d.invocations);
                        ImGui::SameLine();
                        if (ImGui::SmallButton(Tr("Set Filter"))) {
                            drawSettings.suppressDrawCall = true;
                            drawSettings.matchInstanceCount = true;
                            drawSettings.instanceCountMin = d.instanceCount;
                            drawSettings.instanceCountMax = d.instanceCount;
                            if (d.isIndexed) {
                                drawSettings.matchIndexCount = true;
                                drawSettings.indexCountMin = d.count;
                                drawSettings.indexCountMax = d.count;
                                drawSettings.matchVertexCount = false;
                            } else {
                                drawSettings.matchVertexCount = true;
                                drawSettings.vertexCountMin = d.count;
                                drawSettings.vertexCountMax = d.count;
                                drawSettings.matchIndexCount = false;
                            }
                            applyDrawSettings();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(Tr("Lock group geometry filter to exactly %u %s."), d.count, d.isIndexed ? Tr("indices") : Tr("vertices"));
                        }
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndChild();
            EndCard();
        }
}

static void DisplayOverlay(AddonImGui::AddonUIData& instance, Rendering::ResourceManager& resManager, reshade::api::effect_runtime* runtime) {
    if (regionSelection) {
        DisplayRegionSelectionOverlay(instance);
        return;
    }
    if (instance.GetToggleGroupIdSettingsOpen() >= 0) {
        std::string editingGroupName = "";
        const int idx = instance.GetToggleGroupIdSettingsOpen();
        ShaderToggler::ToggleGroup* group = nullptr;
        if (instance.GetToggleGroups().find(idx) != instance.GetToggleGroups().end()) {
            editingGroupName = instance.GetToggleGroups()[idx].getName();
            group = &instance.GetToggleGroups()[idx];
        }

        if (group == nullptr)
            return;

        RfxThemeScope theme;
        ImGui::SetNextWindowBgAlpha(1.0);
        ImGui::SetNextWindowSize({ 1280, 800 }, ImGuiCond_Once);
        bool wndOpen = true;

        auto& huntingUIState = instance.GetHuntingUIState();
        const char* typeItems[] = { Tr("Pixel shader"), Tr("Vertex shader"), Tr("Compute Shader") };
        uint32_t& selectedIndex = huntingUIState.selectedShaderType;
        selectedIndex = std::min<uint32_t>(selectedIndex, 2);
        const char* typeSelectedItem = typeItems[selectedIndex];

        ShaderToggler::ShaderManager* selectedShaderManager =
          selectedIndex == 0 ? instance.GetPixelShaderManager() : (selectedIndex == 1 ? instance.GetVertexShaderManager() : instance.GetComputeShaderManager());

        if (ImGui::Begin((TrFmt("Group settings ({})", editingGroupName) + "###rest_group_settings_" + std::to_string(idx)).c_str(), &wndOpen)) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

            const float splitterWidth = 8.0f;
            const float horizontalSpacing = ImGui::GetStyle().ItemSpacing.x * 2.0f;
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float maxShaderPaneWidth = std::max(300.0f, availableWidth - 420.0f - splitterWidth - horizontalSpacing);
            huntingUIState.shaderPaneWidth = std::clamp(huntingUIState.shaderPaneWidth, 300.0f, maxShaderPaneWidth);

            if (ImGui::BeginChild("GroupView", { huntingUIState.shaderPaneWidth, 0 }, true, ImGuiWindowFlags_NoScrollbar)) {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

                const bool huntingThisGroup = instance.GetToggleGroupIdShaderEditing().load() == group->getId();
                if (huntingThisGroup) {
                    if (ImGui::Button(Tr("Done hunting"))) {
                        instance.EndShaderEditing(true, *group);
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled(Tr("Marks will be committed to this group."));
                } else {
                    if (ImGui::Button(Tr("Start shader hunting")))
                        instance.StartShaderEditing(*group);
                    ImGui::SameLine();
                    ImGui::TextDisabled(Tr("Settings remain open when hunting finishes."));
                }

                DisplayGroupView(instance, resManager, runtime, group, selectedShaderManager);

                ImGui::PushItemWidth(ImGui::GetWindowWidth() - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x * 2);
                if (ImGui::BeginCombo("##shaderType", typeSelectedItem, ImGuiComboFlags_None)) {
                    for (int n = 0; n < IM_ARRAYSIZE(typeItems); n++) {
                        bool is_selected = (typeSelectedItem == typeItems[n]);
                        if (ImGui::Selectable(typeItems[n], is_selected)) {
                            if (n != selectedIndex) {
                                // Reset hunting selections in other managers on switch
                                switch (n) {
                                    case 0: {
                                        instance.GetVertexShaderManager()->resetActiveHuntedShader();
                                        instance.GetComputeShaderManager()->resetActiveHuntedShader();
                                    } break;
                                    case 1: {
                                        instance.GetPixelShaderManager()->resetActiveHuntedShader();
                                        instance.GetComputeShaderManager()->resetActiveHuntedShader();
                                    } break;
                                    case 2: {
                                        instance.GetPixelShaderManager()->resetActiveHuntedShader();
                                        instance.GetVertexShaderManager()->resetActiveHuntedShader();
                                    } break;
                                    default:
                                        break;
                                }

                                instance.UpdateToggleGroupsForShaderHashes();
                            }

                            typeSelectedItem = typeItems[n];
                            selectedIndex = n;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ImGui::PopStyleVar();
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::PushID(0);
            ImGui::Button("", ImVec2(splitterWidth, -1));
            ImGui::PopID();
            if (ImGui::IsItemActive()) {
                huntingUIState.shaderPaneWidth =
                  std::clamp(huntingUIState.shaderPaneWidth + ImGui::GetIO().MouseDelta.x, 300.0f, maxShaderPaneWidth);
            }

            ImGui::SameLine();

            if (ImGui::BeginChild("GroupSettings", { 0, 0 }, true, ImGuiChildFlags_AlwaysAutoResize)) {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

                bool hideMarkedShaders = group->getHideMarkedShaders();
                if (DrawToggleRow("Hide marked shaders", &hideMarkedShaders, "Suppress graphics draws matching this group's marked pixel or vertex shaders while the group is active.\nUseful for HUD-free screenshots, shader identification and configuration testing.\nCompute dispatches are not suppressed."))
                    group->setHideMarkedShaders(hideMarkedShaders);

                if (hideMarkedShaders)
                    ImGui::TextDisabled(Tr("Matching graphics draws are suppressed while this group is active."));

                ImGui::Separator();

                if (instance.GetToggleGroupIdShaderEditing() == idx && ImGui::CollapsingHeader(Tr("Per-shader hiding"))) {
                    ImGui::TextWrapped("%s", Tr("An enabled per-shader rule takes precedence over the group hide switch for that shader."));
                    DisplayShaderDrawSettings(instance, group, selectedShaderManager, selectedIndex, runtime->get_device()->get_api());
                    ImGui::Separator();
                }

                ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
                if (ImGui::BeginTabBar("MyTabBar", tab_bar_flags)) {
                    if (ImGui::BeginTabItem(Tr("Effects"))) {
                        instance.SetCurrentTabType(AddonImGui::TAB_RENDER_TARGET);
                        DisplayRenderTargets(instance, resManager, runtime, group);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem(Tr("Constant bindings"))) {
                        instance.SetCurrentTabType(AddonImGui::TAB_CONSTANT_BUFFER);
                        DisplayConstantTab(instance, group, runtime->get_device());
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem(Tr("Texture bindings"))) {
                        instance.SetCurrentTabType(AddonImGui::TAB_TEXTURE_BINDING);
                        DisplayTextureBindings(instance, group, runtime, resManager);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }

                ImGui::PopStyleVar();
            }
            ImGui::EndChild();

            ImGui::PopStyleVar();
        }
        ImGui::End();

        if (!wndOpen) {
            instance.SetCurrentTabType(AddonImGui::TAB_NONE);
            instance.CloseGroupSettings(true, *group);
        }
    } else {
        instance.SetCurrentTabType(AddonImGui::TAB_NONE);
    }
}

static void CheckHotkeys(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime) {
    if (*instance.ActiveCollectorFrameCounter() > 0)
        --(*instance.ActiveCollectorFrameCounter());

    auto pressedKeys = [&](uint32_t keys) {
        if (keys == 0 || !ShaderToggler::areKeysPressed(keys, runtime))
            return false;

        const bool wantsCtrl = ((keys >> 8) & 0xFF) != 0;
        const bool wantsShift = ((keys >> 16) & 0xFF) != 0;
        const bool wantsAlt = ((keys >> 24) & 0xFF) != 0;

        return wantsCtrl == runtime->is_key_down(VK_CONTROL) &&
               wantsShift == runtime->is_key_down(VK_SHIFT) &&
               wantsAlt == runtime->is_key_down(VK_MENU);
    };

    auto pressed = [&](AddonImGui::Keybind binding) {
        return pressedKeys(instance.GetKeybinding(binding));
    };

    auto handleHunting = [&](ShaderToggler::ShaderManager* manager,
                             AddonImGui::Keybind previous,
                             AddonImGui::Keybind next,
                             AddonImGui::Keybind mark,
                             AddonImGui::Keybind previousMarked,
                             AddonImGui::Keybind nextMarked,
                             AddonImGui::Keybind markPrevious,
                             AddonImGui::Keybind markNext) {
        bool changed = false;

        if (pressed(markPrevious)) {
            manager->toggleMarkOnHuntedShader();
            manager->huntPreviousShader(false);
            return true;
        }
        if (pressed(markNext)) {
            manager->toggleMarkOnHuntedShader();
            manager->huntNextShader(false);
            return true;
        }

        if (pressed(previousMarked)) {
            manager->huntPreviousShader(true);
            changed = true;
        } else if (pressed(nextMarked)) {
            manager->huntNextShader(true);
            changed = true;
        } else if (pressed(previous)) {
            manager->huntPreviousShader(false);
            changed = true;
        } else if (pressed(next)) {
            manager->huntNextShader(false);
            changed = true;
        }

        if (pressed(mark)) {
            manager->toggleMarkOnHuntedShader();
            changed = true;
        }

        return changed;
    };

    if (instance.GetToggleGroupIdShaderEditing() >= 0) {
        bool changed = false;
        changed |= handleHunting(instance.GetPixelShaderManager(),
                                 AddonImGui::PIXEL_SHADER_DOWN,
                                 AddonImGui::PIXEL_SHADER_UP,
                                 AddonImGui::PIXEL_SHADER_MARK,
                                 AddonImGui::PIXEL_SHADER_MARKED_DOWN,
                                 AddonImGui::PIXEL_SHADER_MARKED_UP,
                                 AddonImGui::PIXEL_SHADER_MARK_PREV,
                                 AddonImGui::PIXEL_SHADER_MARK_NEXT);
        changed |= handleHunting(instance.GetVertexShaderManager(),
                                 AddonImGui::VERTEX_SHADER_DOWN,
                                 AddonImGui::VERTEX_SHADER_UP,
                                 AddonImGui::VERTEX_SHADER_MARK,
                                 AddonImGui::VERTEX_SHADER_MARKED_DOWN,
                                 AddonImGui::VERTEX_SHADER_MARKED_UP,
                                 AddonImGui::VERTEX_SHADER_MARK_PREV,
                                 AddonImGui::VERTEX_SHADER_MARK_NEXT);
        changed |= handleHunting(instance.GetComputeShaderManager(),
                                 AddonImGui::COMPUTE_SHADER_DOWN,
                                 AddonImGui::COMPUTE_SHADER_UP,
                                 AddonImGui::COMPUTE_SHADER_MARK,
                                 AddonImGui::COMPUTE_SHADER_MARKED_DOWN,
                                 AddonImGui::COMPUTE_SHADER_MARKED_UP,
                                 AddonImGui::COMPUTE_SHADER_MARK_PREV,
                                 AddonImGui::COMPUTE_SHADER_MARK_NEXT);

        if (changed)
            instance.UpdateToggleGroupsForShaderHashes();

        return;
    }

    for (auto& [_, group] : instance.GetToggleGroups()) {
        const uint32_t toggleKey = group.getToggleKey();
        if (!pressedKeys(toggleKey))
            continue;

        group.toggleActive();
        if (!group.isActive() && instance.GetConstantHandler() != nullptr)
            instance.GetConstantHandler()->RemoveGroup(&group, runtime->get_device());
    }
}

static void DrawCategoryGroups(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime) {
    static std::string groupClipboardStatus;

    if (ImGui::Button(Tr("New Group"))) {
        instance.AddDefaultGroup(Tr("Default"));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(Tr("Create a new toggle group with default settings."));

    ImGui::SameLine();
    if (ImGui::Button(Tr("Import Group"))) {
        const char* clipboard = ImGui::GetClipboardText();
        if (clipboard != nullptr && instance.ImportToggleGroup(clipboard) != nullptr) {
            RuntimeDataContainer& runtimeData = (*runtime->get_private_data<RuntimeDataContainer>());
            std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
            instance.AssignPreferredGroupTechniques(runtimeData.allTechniques);
            groupClipboardStatus = Tr("Group imported from clipboard.");
        } else {
            groupClipboardStatus = Tr("Clipboard does not contain a valid REST group.");
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(Tr("Import a REST group serialized string from the Windows clipboard."));

    ImGui::SameLine();
    if (instance.IsConfigDirty()) {
        ImGui::TextColored(rfx_warn(), Tr("Unsaved changes"));
    } else {
        ImGui::TextColored(rfx_ok(), Tr("All changes saved"));
    }

    if (!groupClipboardStatus.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", groupClipboardStatus.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<int> toRemove;
    std::vector<int> toClone;
    for (auto& [_, group] : instance.GetToggleGroups()) {

        ImGui::PushID(group.getId());

        std::string cardId = std::format("##group_card_{}", group.getId());
        if (BeginCard(cardId.c_str())) {
            bool groupActive = group.isActive();
            if (DrawToggleSwitch("##active_sw", &groupActive)) {
                group.toggleActive();
                if (!groupActive && instance.GetConstantHandler() != nullptr) {
                    instance.GetConstantHandler()->RemoveGroup(&group, runtime->get_device());
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(Tr("Toggle group on or off"));

            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(rfx_col(ImGuiCol_Text), "%s", group.getName().c_str());

            const std::string keyStr = group.getToggleKey() > 0 ? ShaderToggler::reshade_key_name(group.getToggleKey()) : "";
            if (!keyStr.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(rfx_accent(), "[%s]", keyStr.c_str());
            }

            // Open the settings window independently of shader hunting.
            const int openGroup = instance.GetToggleGroupIdSettingsOpen();
            const bool settingsOpen = openGroup == group.getId();
            const char* settingsLabel = settingsOpen ? Tr("Close settings") : Tr("Settings");
            const char* editLabel = group.isEditing() ? Tr("Close") : Tr("Edit");

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float padX = ImGui::GetStyle().FramePadding.x * 2.0f;
            const float totalBtnsW = (ImGui::CalcTextSize(settingsLabel).x + padX) +
                                     (ImGui::CalcTextSize(editLabel).x + padX) +
                                     spacing + RFX_CARD_PAD;

            const float avail = ImGui::GetContentRegionAvail().x;
            SameLineIfFits(ImGui::GetCursorPosX() + avail - totalBtnsW);

            ImGui::BeginDisabled(openGroup >= 0 && !settingsOpen);
            if (ImGui::Button(settingsLabel)) {
                if (settingsOpen)
                    instance.CloseGroupSettings(true, group);
                else
                    instance.OpenGroupSettings(group);
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(Tr("Open the shader hunting & binding settings window for this group."));

            ImGui::SameLine();
            if (ImGui::Button(editLabel)) {
                group.setEditing(!group.isEditing());
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(Tr("Edit the group name or keyboard shortcut."));

            // Summary text row
            const bool shaderEditingThisGroup = instance.GetToggleGroupIdShaderEditing().load() == group.getId();
            const size_t psCount = shaderEditingThisGroup ? instance.GetPixelShaderManager()->getMarkedShaderCount() : group.getPixelShaderHashCount();
            const size_t vsCount = shaderEditingThisGroup ? instance.GetVertexShaderManager()->getMarkedShaderCount() : group.getVertexShaderHashCount();
            const size_t csCount = shaderEditingThisGroup ? instance.GetComputeShaderManager()->getMarkedShaderCount() : group.getComputeShaderHashCount();
            const size_t fxCount = group.preferredTechniques().size();

            ImGui::Spacing();
            ImGui::TextDisabled(Tr("Shaders: PS %zu | VS %zu | CS %zu | Effects: %zu%s%s%s"),
                                psCount,
                                vsCount,
                                csCount,
                                fxCount,
                                group.getAutoRenderSRV() ? Tr(" | Auto Scene Colour") : "",
                                group.getHideMarkedShaders() ? Tr(" | Hide shaders") : "",
                                shaderEditingThisGroup ? Tr(" | Hunting Active") : "");

            // Conflict warnings
            if (group.getToggleKey() != 0) {
                bool conflictShown = false;
                for (const auto& [otherId, otherGroup] : instance.GetToggleGroups()) {
                    if (otherId != group.getId() && otherGroup.getToggleKey() == group.getToggleKey()) {
                        ImGui::TextColored(rfx_warn(), Tr("Warning: group shortcut conflicts with '%s'."), otherGroup.getName().c_str());
                        conflictShown = true;
                        break;
                    }
                }

                if (!conflictShown) {
                    constexpr uint32_t activeKeybindCount = static_cast<uint32_t>(AddonImGui::INVOCATION_DOWN);
                    for (uint32_t i = 0; i < activeKeybindCount; ++i) {
                        if (instance.GetKeybinding(static_cast<AddonImGui::Keybind>(i)) == group.getToggleKey()) {
                            ImGui::TextColored(rfx_warn(), Tr("Warning: shortcut conflicts with REST action '%s'."), Tr(AddonImGui::KeybindDisplayNames[i]));
                            break;
                        }
                    }
                }
            }

            // Inline edit section
            if (group.isEditing()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(rfx_accent(), Tr("Configure Group #%d"), group.getId());
                ImGui::Spacing();

                const float clearBtnW = std::max(56.0f, ImGui::CalcTextSize(Tr("Clear")).x + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f);
                const float inputW = std::clamp(ImGui::GetContentRegionAvail().x * 0.45f - clearBtnW, 60.0f, 200.0f);
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float totalFieldW = inputW + spacing + clearBtnW;

                if (ImGui::BeginTable("##group_edit_fields", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody, ImVec2(-RFX_CARD_PAD, 0.0f))) {
                    ImGui::TableSetupColumn("##field_lbl", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##field_val", ImGuiTableColumnFlags_WidthFixed, totalFieldW);

                    // Name field
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(Tr("Name:"));
                    ImGui::TableNextColumn();
                    char tmpBuffer[150] = {};
                    const std::string name = group.getName();
                    strncpy_s(tmpBuffer, name.c_str(), _TRUNCATE);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##Name", tmpBuffer, 149)) {
                        group.setName(tmpBuffer);
                    }

                    // Keyboard Shortcut
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(Tr("Keyboard Shortcut:"));
                    ImGui::TableNextColumn();
                    uint32_t keys = group.getToggleKey();
                    ImGui::SetNextItemWidth(inputW);
                    if (key_input_box("##KbShortcut", &keys, runtime)) {
                        group.setToggleKey(keys);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(TrLabel("Clear##ClearKbGroup").c_str(), ImVec2(clearBtnW, 0))) {
                        group.setToggleKey(0);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(Tr("Clear keyboard shortcut"));

                    ImGui::EndTable();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button(TrLabel("Done Editing##OK").c_str())) {
                    group.setEditing(false);
                }

                // Action buttons on the right side of the edit panel
                const float cloneW = ImGui::CalcTextSize(Tr("Clone")).x + padX;
                const float copyW = ImGui::CalcTextSize(Tr("Copy")).x + padX;
                const float deleteW = ImGui::CalcTextSize(Tr("Delete")).x + padX;
                const float rightActionsW = cloneW + copyW + deleteW + spacing * 2.0f + RFX_CARD_PAD;

                const float availRight = ImGui::GetContentRegionAvail().x;
                SameLineIfFits(ImGui::GetCursorPosX() + availRight - rightActionsW);

                if (ImGui::Button(Tr("Clone"))) {
                    toClone.push_back(group.getId());
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(Tr("Duplicate this group and its assigned shaders."));

                ImGui::SameLine();
                if (ImGui::Button(Tr("Copy"))) {
                    const std::string serialized = instance.ExportToggleGroup(group);
                    ImGui::SetClipboardText(serialized.c_str());
                    groupClipboardStatus = Tr("Group copied to clipboard.");
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(Tr("Copy serialized group to Windows clipboard."));

                ImGui::SameLine();
                if (ImGui::Button(Tr("Delete"))) {
                    ImGui::OpenPopup(TrLabel("Delete group?###rest_delete_group").c_str());
                }

                if (ImGui::BeginPopupModal(TrLabel("Delete group?###rest_delete_group").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text(Tr("Delete '%s'?"), group.getName().c_str());
                    ImGui::TextDisabled(Tr("This takes effect immediately. Save changes to write to disk."));
                    ImGui::Spacing();
                    if (ImGui::Button(Tr("Delete"))) {
                        toRemove.push_back(group.getId());
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(Tr("Cancel"))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }

            EndCard();
        }

        ImGui::PopID();
    }

    for (const int sourceId : toClone) {
        auto* clone = instance.CloneToggleGroup(sourceId);
        if (clone != nullptr && RestLocalization::Chinese)
            clone->setName(TrFmt("{} Copy", instance.GetToggleGroups().at(sourceId).getName()));
    }

    if (!toRemove.empty()) {
        instance.GetToggleGroupIdEffectEditing() = -1;
        instance.GetToggleGroupIdSettingsOpen() = -1;
        instance.GetToggleGroupIdShaderEditing() = -1;
        instance.GetToggleGroupIdConstantEditing() = -1;
        instance.StopHuntingMode();
    }

    for (const int id : toRemove) {
        instance.SignalToggleGroupRemoved(runtime, &instance.GetToggleGroups().at(id));
        instance.RetireToggleGroup(id);
    }

    if (!toRemove.empty()) {
        instance.UpdateToggleGroupsForShaderHashes();
        instance.MarkConfigDirty();
    }
}

static void DrawCategoryKeybindings(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime) {
    if (BeginCard("##cat_keys", "KEYBOARD HUNTING & SHORTCUTS")) {
        bool duplicateBinding = false;
        constexpr uint32_t totalKeybindCount = static_cast<uint32_t>(AddonImGui::INVOCATION_DOWN);

        if (ImGui::BeginTable("##keybinds_table", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp, ImVec2(-RFX_CARD_PAD, 0.0f))) {
            ImGui::TableSetupColumn(Tr("Action"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(Tr("Shortcut"), ImGuiTableColumnFlags_WidthFixed, std::min(240.0f, ImGui::GetContentRegionAvail().x * 0.5f));
            ImGui::TableHeadersRow();

            for (uint32_t i = 0; i < totalKeybindCount; i++) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(Tr(AddonImGui::KeybindDisplayNames[i]));

                ImGui::TableNextColumn();
                uint32_t keys = instance.GetKeybinding(static_cast<AddonImGui::Keybind>(i));
                ImGui::SetNextItemWidth(-1.0f);
                if (key_input_box(std::format("##kb_{}", i).c_str(), &keys, runtime))
                    instance.SetKeybinding(static_cast<AddonImGui::Keybind>(i), keys);

                if (keys != 0) {
                    for (uint32_t j = 0; j < i; ++j) {
                        if (instance.GetKeybinding(static_cast<AddonImGui::Keybind>(j)) == keys) {
                            duplicateBinding = true;
                            break;
                        }
                    }
                }
            }
            ImGui::EndTable();
        }

        if (duplicateBinding) {
            ImGui::Spacing();
            ImGui::TextColored(rfx_warn(), Tr("Warning: two or more REST actions use the same shortcut."));
        }

        EndCard();
    }
}

static void DrawCategoryOptions(AddonImGui::AddonUIData& instance) {
    auto DrawRightAlignedRow = [](const char* label, const char* tooltip, auto drawControl, float ctrlW = 220.0f) {
        label = Tr(label);
        tooltip = Tr(tooltip);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);

        const float avail = ImGui::GetContentRegionAvail().x;
        SameLineIfFits(ImGui::GetCursorPosX() + avail - ctrlW - RFX_CARD_PAD);
        ImGui::SetNextItemWidth(std::min(ctrlW, ImGui::GetContentRegionAvail().x));
        drawControl();
        if (tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
        ImGui::Spacing();
    };

    if (BeginCard("##opt_hunting", "SHADER HUNTING & SELECTION")) {
        DrawRightAlignedRow("Overlay opacity:", "Opacity of the shader hunting overlay window.", [&]() {
            if (ImGui::SliderFloat("##OverlayOpacity", instance.OverlayOpacity(), 0.0f, 1.0f))
                instance.MarkConfigDirty();
        });

        DrawRightAlignedRow("# of frames to collect:", "The number of frames the addon will collect active shaders. Set this to a high number if the shader you want to mark is only used occasionally.", [&]() {
            if (ImGui::SliderInt("##FramesToCollect", instance.StartValueFramecountCollectionPhase(), 10, 1000))
                instance.MarkConfigDirty();
        });

        EndCard();
    }

    if (BeginCard("##opt_pipeline", "PIPELINE & RESOURCE COMPATIBILITY")) {
        std::string varSelectedItem = instance.GetResourceShim();
        DrawRightAlignedRow("Resource Shim:",
                            "none: default.\n"
                            "srgb: make sRGB targets typeless so linear views work (try it if targets are not matched or colours look wrong).\n"
                            "ffxiv: FFXIV only.",
                            [&]() {
            if (ImGui::BeginCombo("##ResourceShim", Tr(varSelectedItem.c_str()), ImGuiComboFlags_None)) {
                for (auto& v : Rendering::ResourceShimNames) {
                    bool is_selected = (varSelectedItem == v);
                    if (ImGui::Selectable(Tr(v.c_str()), is_selected)) {
                        varSelectedItem = v;
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        });
        instance.SetResourceShim(varSelectedItem);

        std::string varSelectedCopyMethod = instance.GetConstHookCopyType();
        DrawRightAlignedRow("Constant Buffer Copy Method:",
                            "none: off, no constant bindings.\n"
                            "gpu_readback: default, read back from the GPU.\n"
                            "memcpy_singular/nested: CPU intercept for DX9-DX11.\n"
                            "ffxiv/nier_replicant: those games only.",
                            [&]() {
            if (ImGui::BeginCombo("##ConstCopyType", Tr(varSelectedCopyMethod.c_str()), ImGuiComboFlags_None)) {
                for (auto& v : Shim::Constants::ConstantCopyTypeNames) {
                    bool is_selected = (varSelectedCopyMethod == v);
                    if (ImGui::Selectable(Tr(v.c_str()), is_selected)) {
                        varSelectedCopyMethod = v;
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        });
        instance.SetConstHookCopyType(varSelectedCopyMethod);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool trackDescriptors = instance.GetTrackDescriptors();
        if (DrawToggleRow("Track descriptors", &trackDescriptors,
                          "Enables descriptor set tracking for advanced shader binding matching.")) {
            instance.SetTrackDescriptors(trackDescriptors);
        }

        bool runtimeReload = instance.GetPreventRuntimeReload();
        if (DrawToggleRow("Prevent runtime reload", &runtimeReload,
                          "Prevents ReShade from reloading effects during runtime if the game swaps pipelines.")) {
            instance.SetPreventRuntimeReload(runtimeReload);
        }

        EndCard();
    }

    if (ImGui::CollapsingHeader(Tr("General info and help"))) {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(
          Tr("Create toggle groups and assign an optional keyboard shortcut to each group. Mark shaders in Group settings. "
             "To hide matching graphics draws while a group is active, turn on Hide marked shaders."));
        ImGui::TextUnformatted(
          Tr("\nShader hunting can be controlled with the buttons in Group settings or with the shortcuts under Shader hunting keybindings. Pixel and vertex hunting keep the "
          "traditional numpad defaults; compute hunting is unassigned by default so it does not steal an existing shortcut. All hunting shortcuts can be changed "
          "for laptops, compact keyboards or personal preference."));
        ImGui::TextUnformatted(
          Tr("\nWhen you step through shaders, the currently selected shader is disabled in the scene so you can identify it. Double-click a hash, use Mark / unmark, "
          "or use the configured shortcut to add or remove it from the group."));
        ImGui::TextUnformatted(
          Tr("Configuration changes are tracked automatically. Use Save changes when the Unsaved changes indicator appears."));
        ImGui::PopTextWrapPos();
    }
}

static int s_currentRestCategory = CAT_GROUPS;

static void DisplayRestTab(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime) {
    RfxThemeScope theme;

    // Top Header Banner (regular layout items, so nothing can overlap the panes below)
    ImGui::Spacing();
    {
        size_t activeCount = 0;
        const auto& groups = instance.GetToggleGroups();
        for (const auto& [_, g] : groups) {
            if (g.isActive()) ++activeCount;
        }
        char status_str[64];
        snprintf(status_str, sizeof(status_str), Tr("%zu Groups (%zu Active)"), groups.size(), activeCount);
        const std::string ver_text = "v" REST_VERSION_STRING;
        const ImGuiStyle& style = ImGui::GetStyle();

        // Visible right edge of the window, independent of any horizontal content overflow.
        const float visible_right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - style.WindowPadding.x -
                                    ((ImGui::GetScrollMaxY() > 0.0f) ? style.ScrollbarSize : 0.0f);

        ImGui::TextColored(rfx_accent(), "%s", Tr("Reshade Effect Shader Toggler"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ver_text.c_str());

        const float status_w = ImGui::CalcTextSize(status_str).x;
        const float status_x = visible_right - status_w;
        if (status_x >= ImGui::GetItemRectMax().x + style.ItemSpacing.x * 3.0f) {
            ImGui::SameLine(status_x - ImGui::GetWindowPos().x + ImGui::GetScrollX());
        }
        ImGui::TextColored(activeCount > 0 ? rfx_ok() : rfx_col(ImGuiCol_TextDisabled), "%s", status_str);
    }
    ImGui::Separator();
    ImGui::Spacing();

    // Two-pane layout: Left Sidebar + Right Content Area
    float max_label_w = 0.0f;
    for (int i = 0; i < CAT_COUNT; ++i) {
        max_label_w = std::max(max_label_w, ImGui::CalcTextSize(s_restCategoryNames[i]).x);
    }
    const float sidebar_width = std::min(std::max(150.0f, max_label_w + 58.0f), ImGui::GetContentRegionAvail().x * 0.42f);

    {
        ImVec4 sb = rfx_col(ImGuiCol_FrameBg);
        sb.w *= 0.35f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, sb);
    }
    if (ImGui::BeginChild("##rest_sidebar", ImVec2(sidebar_width, 0), ImGuiChildFlags_AlwaysUseWindowPadding)) {
        for (int i = 0; i < CAT_COUNT; ++i) {
            if (DrawSidebarCategoryButton(i, s_restCategoryNames[i], s_currentRestCategory == i)) {
                s_currentRestCategory = i;
            }
        }

        ImGui::Separator();
        ImGui::Spacing();
        const bool dirty = instance.IsConfigDirty();
        if (!dirty) ImGui::BeginDisabled();
        if (ImGui::Button(Tr("Save Changes"), ImVec2(-1, 0))) {
            instance.SaveShaderTogglerIniFile();
        }
        if (!dirty) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(Tr("Save toggle groups and keybindings to ReshadeEffectShaderToggler.ini"));
        DisplayAbout();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(1);

    ImGui::SameLine();

    // Right Content Area
    if (ImGui::BeginChild("##rest_content", ImVec2(0, 0))) {
        static const char* const cat_titles[CAT_COUNT] = {
            Tr("Toggle Groups"),
            Tr("Keybindings & Shortcuts"),
            Tr("General & Pipeline Options")
        };

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::Indent(6.0f);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
        ImGui::TextUnformatted(cat_titles[s_currentRestCategory]);
        ImGui::PopFont();
        ImGui::Unindent(6.0f);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        switch (s_currentRestCategory) {
        case CAT_GROUPS:
            DrawCategoryGroups(instance, runtime);
            break;
        case CAT_KEYBINDINGS:
            DrawCategoryKeybindings(instance, runtime);
            break;
        case CAT_OPTIONS:
            DrawCategoryOptions(instance);
            break;
        default:
            break;
        }
    }
    ImGui::EndChild();
}

static void DisplayAddonsTab() {
    ImGui::TextWrapped("%s", Tr("Open the REST tab in the ReShade overlay to manage toggle groups, shader hunting, keyboard shortcuts and options."));
}
