#pragma once
#include "Localization.h"
#include <algorithm>
#include <imgui.h>

// Theme and layout adapted from the REST2 overlay.
static ImVec4 rfx_col(ImGuiCol idx) { return ImGui::GetStyle().Colors[idx]; }
static ImU32 rfx_u32(ImVec4 c, float alpha_mul = 1.0f) {
    auto b = [](float v) { return static_cast<ImU32>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return b(c.x) | (b(c.y) << 8) | (b(c.z) << 16) | (b(c.w * alpha_mul * ImGui::GetStyle().Alpha) << 24);
}
static ImU32 rfx_u32(ImGuiCol idx, float alpha_mul = 1.0f) { return rfx_u32(rfx_col(idx), alpha_mul); }
static bool rfx_light_theme() {
    const ImVec4 c = rfx_col(ImGuiCol_WindowBg);
    return c.x * 0.299f + c.y * 0.587f + c.z * 0.114f > 0.5f;
}
static ImVec4 rfx_accent() { return rfx_col(ImGuiCol_CheckMark); }
static ImVec4 rfx_ok()   { return rfx_light_theme() ? ImVec4(0.10f, 0.48f, 0.22f, 1.0f) : ImVec4(0.35f, 0.85f, 0.50f, 1.0f); }
static ImVec4 rfx_warn() { return rfx_light_theme() ? ImVec4(0.66f, 0.40f, 0.00f, 1.0f) : ImVec4(1.00f, 0.75f, 0.35f, 1.0f); }

struct RfxThemeScope {
    RfxThemeScope() {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,     5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,      6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,     6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,      ImVec2(8.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,       ImVec2(8.0f, 6.0f));
    }

    ~RfxThemeScope() {
        ImGui::PopStyleVar(5);
    }
};

static constexpr float RFX_CARD_PAD = 16.0f;

static bool BeginCard(const char* str_id, const char* title = nullptr) {
    ImGui::PushID(str_id);
    ImGui::Spacing();

    ImVec2 p_start = ImGui::GetCursorScreenPos();
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGuiID height_id = ImGui::GetID("##card_h");
    float prev_h = ImGui::GetStateStorage()->GetFloat(height_id, 0.0f);

    if (prev_h > 0.0f) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p_end(p_start.x + avail_w, p_start.y + prev_h);
        dl->AddRectFilled(p_start, p_end, rfx_u32(ImGuiCol_FrameBg, 0.5f), 6.0f);
        dl->AddRect(p_start, p_end, rfx_u32(ImGuiCol_Border), 6.0f, 0, 1.0f);
    }

    ImGui::GetStateStorage()->SetFloat(ImGui::GetID("##card_top_y"), p_start.y);

    ImGui::Dummy(ImVec2(0, 8.0f));
    // Wrap all plain text inside the card at the card's inner right edge so long
    // labels and status lines never run past the border.
    const float wrap_local_x = ImGui::GetCursorPosX() + avail_w - RFX_CARD_PAD;
    ImGui::Indent(RFX_CARD_PAD);
    ImGui::PushTextWrapPos(wrap_local_x);
    if (title != nullptr && title[0] != '\0') {
        ImGui::TextColored(rfx_accent(), "%s", Tr(title));
        ImGui::Dummy(ImVec2(0, 4.0f));
    }
    return true;
}

static void EndCard() {
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 8.0f));
    ImGui::Unindent(RFX_CARD_PAD);

    float top_y = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("##card_top_y"), 0.0f);
    float current_y = ImGui::GetCursorScreenPos().y;
    float card_h = current_y - top_y;
    if (card_h > 0.0f) {
        ImGui::GetStateStorage()->SetFloat(ImGui::GetID("##card_h"), card_h);
    }

    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 4.0f));
}

static bool DrawToggleSwitch(const char* id, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float height = ImGui::GetFrameHeight() * 0.70f;
    float width = height * 1.85f;
    float radius = height * 0.5f;

    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, height), ImGuiButtonFlags_EnableNav);
    if (clicked) *v = !(*v);

    float t = *v ? 1.0f : 0.0f;
    ImU32 col_bg;
    if (*v) {
        ImVec4 on = rfx_accent();
        if (ImGui::IsItemHovered()) on.w *= 0.85f;
        col_bg = rfx_u32(on);
    } else {
        col_bg = rfx_u32(ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    }

    draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, radius);
    float circle_x = p.x + radius + t * (width - 2.0f * radius);
    draw_list->AddCircleFilled(ImVec2(circle_x, p.y + radius), radius - 1.5f,
                               rfx_u32(*v ? ImGuiCol_WindowBg : ImGuiCol_TextDisabled));
    return clicked;
}

static bool DrawTableToggleSwitch(const char* id, bool* v, float rightPad = 8.0f) {
    float avail = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetFrameHeight() * 0.70f;
    float switch_w = height * 1.85f;
    if (avail > switch_w + rightPad) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - switch_w - rightPad);
    }
    return DrawToggleSwitch(id, v);
}

// Places the next control at local x `ctrl_x` on the same line as the previous item when it
// clears that item; otherwise lets it flow onto the next line instead of overlapping.
static void SameLineIfFits(float ctrl_x) {
    const float prev_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX() +
                           ImGui::GetStyle().ItemSpacing.x;
    if (ctrl_x >= prev_end) {
        ImGui::SameLine(ctrl_x);
    }
}

static bool DrawToggleRow(const char* label, bool* v, const char* tooltip = nullptr, const char* subtext = nullptr, float rightPad = RFX_CARD_PAD) {
    ImGui::PushID(label);
    // Labels, tooltips and status lines reach the user, so translate them here instead of at
    // every call site.
    label = Tr(label);
    tooltip = Tr(tooltip);
    subtext = Tr(subtext);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float switch_w = ImGui::GetFrameHeight() * 0.70f * 1.85f;
    const float switch_x = ImGui::GetCursorPosX() + avail - switch_w - rightPad;
    // Keep the label column clear of the switch so long labels wrap instead of overlapping it.
    const float label_wrap_x = switch_x - ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const bool wrap_label = label_wrap_x > ImGui::GetCursorPosX() + 60.0f;
    if (wrap_label) ImGui::PushTextWrapPos(label_wrap_x);
    ImGui::BeginGroup();
    ImGui::Text("%s", label);
    if (subtext != nullptr && subtext[0] != '\0') {
        ImGui::TextDisabled("%s", subtext);
    }
    ImGui::EndGroup();
    if (wrap_label) ImGui::PopTextWrapPos();

    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);

    SameLineIfFits(switch_x);
    bool changed = DrawToggleSwitch("##sw", v);
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    ImGui::Spacing();
    return changed;
}

