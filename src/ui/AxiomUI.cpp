#include "AxiomUI.h"
#include <atomic>
#include <cstdio>
#include <vector>
#include "imgui.h"
#include "../analysis/Disasm.h"
#include "../hooks/TouchHook.h"

namespace {

std::atomic<bool> g_visible{true}; // Start visible

char g_addrInput[32] = "0x0";
std::vector<DisasmInstruction> g_instructions;

void ApplyAxiomStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.TouchExtraPadding = ImVec2(15.0f, 15.0f); // Extra fat touch targets

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.95f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.25f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.95f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.45f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
}

void DrawFloatingToggle() {
    // Force to top right using absolute screen coordinates
    ImGui::SetNextWindowPos(ImVec2(800.0f, 50.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(80.0f, 80.0f));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("##AxiomBall", nullptr, flags);

    // Large button for fat fingers
    if (ImGui::Button("AX", ImVec2(80.0f, 80.0f))) {
        g_visible = !g_visible.load();
    }

    ImGui::End();
}

}

namespace AxiomUI {

bool IsVisible() { return g_visible.load(); }
void Toggle() { g_visible = !g_visible.load(); }
void Show() { g_visible = true; }
void Hide() { g_visible = false; }

void Draw() {
    static bool styleApplied = false;
    if (!styleApplied) {
        ApplyAxiomStyle();
        styleApplied = true;
    }

    // ALWAYS draw the floating toggle ball
    DrawFloatingToggle();

    if (!g_visible.load()) return;

    ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f), ImGuiCond_FirstUseEver);

    ImGui::Begin("Axiom-RE Framework", nullptr, ImGuiWindowFlags_NoCollapse);

    if (ImGui::Button("Hide Window")) {
        g_visible = false;
    }

    ImGui::SameLine();
    ImGui::Text("Touch src:%d  x:%.0f y:%.0f down:%d",
        TouchHook::Source(), TouchHook::X(), TouchHook::Y(), (int)TouchHook::Down());

    ImGui::Separator();

    if (ImGui::BeginTabBar("AxiomTabs")) {
        if (ImGui::BeginTabItem("Dashboard")) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Status: ACTIVE");
            ImGui::Separator();
            ImGui::Text("Frida GUM: Injected");
            ImGui::Text("Capstone: Ready");
            ImGui::Text("ImGui: Rendering");
            ImGui::Text("Touch API: %s", TouchHook::Source() > 0 ? "CONNECTED" : "NONE");
            ImGui::Spacing();
            ImGui::TextWrapped("Drag this window by its title bar. Tap the AX button to hide/show.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Disassembler")) {
            ImGui::Text("Address (hex):");
            ImGui::InputText("##addr", g_addrInput, sizeof(g_addrInput));
            ImGui::SameLine();
            if (ImGui::Button("Disassemble")) {
                uint64_t addr = 0;
                if (sscanf(g_addrInput, "%llx", &addr) == 1 || sscanf(g_addrInput, "0x%llx", &addr) == 1) {
                    if (addr > 0) {
                        g_instructions = Disasm::Disassemble(addr, 256, 15);
                    }
                }
            }

            ImGui::Separator();

            if (ImGui::BeginChild("DisasmScroll", ImVec2(0, 0), true)) {
                if (g_instructions.empty()) {
                    ImGui::TextDisabled("No instructions loaded.");
                } else {
                    for (const auto& inst : g_instructions) {
                        ImVec4 color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
                        if (inst.mnemonic == "bl" || inst.mnemonic == "b") {
                            color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                        } else if (inst.mnemonic == "ret") {
                            color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                        }
                        ImGui::PushStyleColor(ImGuiCol_Text, color);
                        ImGui::Text("0x%llX:  %-6s %s", inst.address, inst.mnemonic.c_str(), inst.opStr.c_str());
                        ImGui::PopStyleColor();
                    }
                }
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

}
