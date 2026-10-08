#include "AxiomUI.h"

#include <atomic>

#include "imgui.h"

namespace {

std::atomic<bool> g_visible{false};

void ApplyAxiomStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 14.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 10.0f;
    style.PopupRounding = 10.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 8.0f;

    style.WindowPadding = ImVec2(14.0f, 14.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;

    ImVec4* colors = ImGui::GetStyle().Colors;

    colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.10f, 0.85f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.24f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.96f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.26f, 0.38f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.34f, 0.34f, 0.48f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.32f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.55f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.42f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.34f, 0.34f, 0.50f, 1.00f);
}

}

namespace AxiomUI {

bool IsVisible() {
    return g_visible.load();
}

void Toggle() {
    g_visible = !g_visible.load();
}

void Show() {
    g_visible = true;
}

void Hide() {
    g_visible = false;
}

void Draw() {
    if (!g_visible.load()) {
        return;
    }

    static bool styleApplied = false;
    if (!styleApplied) {
        ApplyAxiomStyle();
        styleApplied = true;
    }

    ImGui::SetNextWindowPos(ImVec2(30.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 220.0f), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("Axiom-RE", nullptr, flags);

    ImGui::TextColored(ImVec4(0.45f, 0.70f, 1.00f, 1.00f), "Phase 1 active");

    ImGui::Separator();

    ImGui::Text("Mod: Axiom-RE");
    ImGui::Text("Type: Reverse Engineering Framework");
    ImGui::Text("UI: Floating in-game overlay");

    ImGui::Spacing();
    ImGui::TextWrapped(
        "Toggle this window from the Mod Menu. "
        "Touch input for dragging/resizing will be added in the next phase."
    );

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextDisabled("Build: 0.1.0");

    ImGui::End();
}

}
