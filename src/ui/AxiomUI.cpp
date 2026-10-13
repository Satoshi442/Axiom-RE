#include "AxiomUI.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "../hooks/TouchHook.h"
#include "../hooks/Keyboard.h"   // <-- MUST exist for the Settings keyboard panel
#include "../analysis/Memory.h"
#include "../analysis/Disasm.h"

namespace {

std::atomic<bool> g_visible{true};

// ---- Settings (persisted) ----
struct Settings {
    float ballX = -1.0f;
    float ballY = -1.0f;
    float scale = 1.0f;
    float alpha = 0.95f;
    float accent[3] = {0.30f, 0.70f, 1.00f};
    bool resizeFromEdges = true;
    bool maximize = false;
};
Settings g_settings;
std::string g_settingsPath;
std::string g_iniPath;

void LoadSettings() {
    if (g_settingsPath.empty()) return;
    FILE* f = fopen(g_settingsPath.c_str(), "r");
    if (!f) return;
    fscanf(f, "%f %f %f %f %f %f %f %d %d",
           &g_settings.ballX, &g_settings.ballY,
           &g_settings.scale, &g_settings.alpha,
           &g_settings.accent[0], &g_settings.accent[1], &g_settings.accent[2],
           (int*)&g_settings.resizeFromEdges, (int*)&g_settings.maximize);
    fclose(f);
}

void SaveSettings() {
    if (g_settingsPath.empty()) return;
    FILE* f = fopen(g_settingsPath.c_str(), "w");
    if (!f) return;
    fprintf(f, "%.2f %.2f %.2f %.2f %.3f %.3f %.3f %d %d",
            g_settings.ballX, g_settings.ballY,
            g_settings.scale, g_settings.alpha,
            g_settings.accent[0], g_settings.accent[1], g_settings.accent[2],
            g_settings.resizeFromEdges ? 1 : 0, g_settings.maximize ? 1 : 0);
    fclose(f);
}

uint32_t AccentU32() {
    return IM_COL32((int)(g_settings.accent[0]*255),
                    (int)(g_settings.accent[1]*255),
                    (int)(g_settings.accent[2]*255), 255);
}

void ApplyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.TouchExtraPadding = ImVec2(12.0f, 12.0f);
    style.WindowBorderSize = 1.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, g_settings.alpha);
    c[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.15f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(g_settings.accent[0]*0.3f, g_settings.accent[1]*0.3f, g_settings.accent[2]*0.3f, 1.0f);
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.95f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.30f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.45f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(g_settings.accent[0]*0.6f, g_settings.accent[1]*0.6f, g_settings.accent[2]*0.6f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
    c[ImGuiCol_ResizeGrip] = ImVec4(g_settings.accent[0], g_settings.accent[1], g_settings.accent[2], 0.9f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(g_settings.accent[0], g_settings.accent[1], g_settings.accent[2], 1.0f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

// ---- Disassembler state ----
char g_addrInput[32] = "0x0";
std::vector<DisasmInstruction> g_instructions;

// ---- Memory tab state ----
static int g_modIdx = -1;
static char g_memAddr[32] = "0x0";
static uint64_t g_viewAddr = 0;
static char g_inspAddr[32] = "0x0";
static uint64_t g_inspAddrVal = 0;
static int g_inspType = 2;
static char g_inspWrite[32] = "";
static float g_demoValue = 100.0f;
static bool g_demoAnimate = true;

// ---- Draggable ball state ----
float g_dragDist = 0.0f;

void DrawFloatingToggle() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 ds = io.DisplaySize;
    if (ds.x < 10.0f) return;

    if (g_settings.ballX < 0.0f) {
        g_settings.ballX = ds.x - 110.0f;
        g_settings.ballY = 60.0f;
    }

    // clamp inside screen
    if (g_settings.ballX < 0.0f) g_settings.ballX = 0.0f;
    if (g_settings.ballY < 0.0f) g_settings.ballY = 0.0f;
    if (g_settings.ballX > ds.x - 80.0f) g_settings.ballX = ds.x - 80.0f;
    if (g_settings.ballY > ds.y - 80.0f) g_settings.ballY = ds.y - 80.0f;

    ImGui::SetNextWindowPos(ImVec2(g_settings.ballX, g_settings.ballY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(80.0f, 80.0f));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("##AxiomBall", nullptr, flags);

    ImGui::InvisibleButton("##ballbtn", ImVec2(80.0f, 80.0f));

    if (ImGui::IsItemActivated()) g_dragDist = 0.0f;

    if (ImGui::IsItemActive()) {
        g_settings.ballX += io.MouseDelta.x;
        g_settings.ballY += io.MouseDelta.y;
        g_dragDist += fabsf(io.MouseDelta.x) + fabsf(io.MouseDelta.y);
    }

    static bool wasActive = false;
    bool isActive = ImGui::IsItemActive();

    if (wasActive && !isActive) {
        // finger just lifted off the ball
        if (g_dragDist < 12.0f) {
            g_visible = !g_visible.load();   // tap = toggle
        } else {
            SaveSettings();                  // drag end = save position
        }
    }
    wasActive = isActive;

    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 mx = ImGui::GetItemRectMax();
    ImVec2 center((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(center, 36.0f, AccentU32());
    dl->AddCircle(center, 36.0f, IM_COL32(255, 255, 255, 200), 0, 2.0f);
    dl->AddText(nullptr, 20.0f, ImVec2(center.x - 14.0f, center.y - 11.0f),
                IM_COL32(0, 0, 0, 255), "AX");

    ImGui::End();
}

void DrawMemoryTab() {
    if (g_demoAnimate) {
        g_demoValue = 100.0f + 50.0f * sinf((float)ImGui::GetTime() * 2.0f);
    }
    const auto& mods = Memory::Modules();

    // ---- Region picker for the selected module ----
    if (g_modIdx >= 0 && g_modIdx < (int)mods.size()) {
        const std::string& modPath = mods[g_modIdx].path;

        static std::vector<int> regionIds;
        static std::vector<std::string> regionLabels;
        static std::string builtFor;
        static int regionIdx = -1;

        if (builtFor != modPath) {
            regionIds.clear();
            regionLabels.clear();
            int i = 0;
            for (const auto& r : Memory::Ranges()) {
                if (r.path == modPath) {
                    regionIds.push_back(i);
                    char buf[160];
                    snprintf(buf, sizeof(buf), "%s  %llX-%llX",
                             r.perms.c_str(),
                             (unsigned long long)r.start,
                             (unsigned long long)r.end);
                    regionLabels.push_back(buf);
                }
                i++;
            }
            regionIdx = -1;
            builtFor = modPath;
        }

        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::BeginCombo("Region",
                regionIdx >= 0 ? regionLabels[regionIdx].c_str() : "pick region...")) {
            for (int i = 0; i < (int)regionLabels.size(); i++) {
                if (ImGui::Selectable(regionLabels[i].c_str())) regionIdx = i;
            }
            ImGui::EndCombo();
        }

        if (regionIdx >= 0 && regionIdx < (int)regionIds.size()) {
            const auto& r = Memory::Ranges()[regionIds[regionIdx]];

            ImGui::SameLine();
            if (ImGui::Button("Hex here")) {
                g_viewAddr = r.start;
                snprintf(g_memAddr, sizeof(g_memAddr), "%llX", (unsigned long long)r.start);
            }

            ImGui::SameLine();
            if (ImGui::Button("Disasm here")) {
                snprintf(g_addrInput, sizeof(g_addrInput), "%llX", (unsigned long long)r.start);
                g_instructions = Disasm::Disassemble(r.start, 256, 20);
            }
        }
}

    if (ImGui::Button("Refresh Modules")) {
        Memory::Refresh();
        g_modIdx = -1;
    }

    ImGui::SameLine();
    const char* preview = (g_modIdx >= 0 && g_modIdx < (int)mods.size())
                          ? mods[g_modIdx].name.c_str() : "Select module...";
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("##mods", preview)) {
        for (int i = 0; i < (int)mods.size(); i++) {
            char label[320];
            snprintf(label, sizeof(label), "%s @ %llX", mods[i].name.c_str(),
                     (unsigned long long)mods[i].base);
            if (ImGui::Selectable(label)) {
                g_modIdx = i;
                g_viewAddr = mods[i].base;
                snprintf(g_memAddr, sizeof(g_memAddr), "%llX",
                         (unsigned long long)g_viewAddr);
            }
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##memaddr", g_memAddr, sizeof(g_memAddr));
    ImGui::SameLine();
    if (ImGui::Button("Go")) {
        uint64_t a = strtoull(g_memAddr, nullptr, 16);
        if (a) g_viewAddr = a;
    }

    ImGui::Separator();

    // Hex dump
    if (ImGui::BeginChild("##hex", ImVec2(0, 220.0f), true)) {
        char line[160];
        for (int row = 0; row < 16; row++) {
            uint64_t addr = g_viewAddr + (uint64_t)row * 16;
            uint8_t buf[16];
            bool ok = Memory::Read(addr, buf, 16);

            int off = snprintf(line, sizeof(line), "%012llX  ", (unsigned long long)addr);
            for (int i = 0; i < 16; i++) {
                if (ok) off += snprintf(line + off, sizeof(line) - off, "%02X ", buf[i]);
                else    off += snprintf(line + off, sizeof(line) - off, "?? ");
            }
            off += snprintf(line + off, sizeof(line) - off, " ");
            for (int i = 0; i < 16; i++) {
                char ch = ok ? (char)buf[i] : '?';
                if (ch < 32 || ch > 126) ch = '.';
                line[off++] = ch;
            }
            line[off] = 0;

            ImGui::TextUnformatted(line);
            if (ImGui::IsItemClicked()) {
                g_inspAddrVal = addr;
                snprintf(g_inspAddr, sizeof(g_inspAddr), "%llX", (unsigned long long)addr);
            }
        }
        ImGui::EndChild();
    }

    ImGui::Separator();

    // Value inspector / writer
    ImGui::Text("Inspector:");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##inspaddr", g_inspAddr, sizeof(g_inspAddr));
    ImGui::SameLine();
    if (ImGui::Button("Read")) {
        uint64_t a = strtoull(g_inspAddr, nullptr, 16);
        if (a) g_inspAddrVal = a;
    }

    const char* types[] = { "u8", "u16", "u32", "u64", "f32", "f64" };
    ImGui::SetNextItemWidth(100.0f);
    ImGui::Combo("##type", &g_inspType, types, 6);

    if (g_inspAddrVal) {
        char valText[64] = "?";
        switch (g_inspType) {
            case 0: { uint8_t v;  if (Memory::Read(g_inspAddrVal, &v, 1)) snprintf(valText, sizeof(valText), "%u", v); break; }
            case 1: { uint16_t v; if (Memory::Read(g_inspAddrVal, &v, 2)) snprintf(valText, sizeof(valText), "%u", v); break; }
            case 2: { uint32_t v; if (Memory::Read(g_inspAddrVal, &v, 4)) snprintf(valText, sizeof(valText), "%u (0x%X)", v, v); break; }
            case 3: { uint64_t v; if (Memory::Read(g_inspAddrVal, &v, 8)) snprintf(valText, sizeof(valText), "%llu (0x%llX)", (unsigned long long)v, (unsigned long long)v); break; }
            case 4: { float v;    if (Memory::Read(g_inspAddrVal, &v, 4)) snprintf(valText, sizeof(valText), "%.4f", v); break; }
            case 5: { double v;   if (Memory::Read(g_inspAddrVal, &v, 8)) snprintf(valText, sizeof(valText), "%.4f", v); break; }
        }
        ImGui::Text("Live value: %s", valText);

        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##writeval", g_inspWrite, sizeof(g_inspWrite));
        ImGui::SameLine();
        if (ImGui::Button("Write")) {
            bool done = false;
            switch (g_inspType) {
                case 0: { uint8_t v = (uint8_t)strtoul(g_inspWrite, nullptr, 0);   done = Memory::Write(g_inspAddrVal, &v, 1); break; }
                case 1: { uint16_t v = (uint16_t)strtoul(g_inspWrite, nullptr, 0); done = Memory::Write(g_inspAddrVal, &v, 2); break; }
                case 2: { uint32_t v = (uint32_t)strtoul(g_inspWrite, nullptr, 0); done = Memory::Write(g_inspAddrVal, &v, 4); break; }
                case 3: { uint64_t v = strtoull(g_inspWrite, nullptr, 0);          done = Memory::Write(g_inspAddrVal, &v, 8); break; }
                case 4: { float v = strtof(g_inspWrite, nullptr);                  done = Memory::Write(g_inspAddrVal, &v, 4); break; }
                case 5: { double v = strtod(g_inspWrite, nullptr);                 done = Memory::Write(g_inspAddrVal, &v, 8); break; }
            }
            if (!done) ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Write failed (not writable)");
        }
    }
    ImGui::Separator();
    if (ImGui::Button("Demo Var (writable test)")) {
        g_inspAddrVal = (uint64_t)&g_demoValue;
        snprintf(g_inspAddr, sizeof(g_inspAddr), "%llX", (unsigned long long)g_inspAddrVal);
        g_inspType = 4; // f32
    }
    ImGui::SameLine();
    ImGui::Checkbox("Animate", &g_demoAnimate);
}

void DrawSettingsTab() {
    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::SliderFloat("UI Scale", &g_settings.scale, 0.8f, 2.0f, "%.2f")) {
        io.FontGlobalScale = g_settings.scale;
    }

    if (ImGui::SliderFloat("Window Alpha", &g_settings.alpha, 0.30f, 1.0f, "%.2f")) {
        ApplyStyle();
    }

    if (ImGui::ColorEdit3("Accent Color", g_settings.accent)) {
        ApplyStyle();
    }

    if (ImGui::Checkbox("Resize From Edges", &g_settings.resizeFromEdges)) {
        io.ConfigWindowsResizeFromEdges = g_settings.resizeFromEdges;
    }

    ImGui::Checkbox("Maximize Main Window", &g_settings.maximize);

    if (ImGui::Button("Reset Ball Position")) {
        ImVec2 ds = io.DisplaySize;
        g_settings.ballX = ds.x - 110.0f;
        g_settings.ballY = 60.0f;
    }

    ImGui::SameLine();
    if (ImGui::Button("Save Settings")) {
        SaveSettings();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "Saved!");
    }

    ImGui::Separator();
    ImGui::TextWrapped("Drag the main window by its title bar. Resize it by dragging any edge or the bottom-right corner. Drag the AX ball to move it, tap it to toggle.");
}
ImGui::Separator();
    ImGui::Text("Keyboard: %s", Keyboard::Status());
    ImGui::Text("WantTextInput: %d   ShowCalls: %d",
                (int)ImGui::GetIO().WantTextInput, Keyboard::ShowCalls());
    if (ImGui::Button("Show (official)")) Keyboard::ShowOfficial();
    ImGui::SameLine();
    if (ImGui::Button("Show (JNI)")) Keyboard::ShowJni();
    if (ImGui::Button("Hide (official)")) Keyboard::HideOfficial();
    ImGui::SameLine();
    if (ImGui::Button("Hide (JNI)")) Keyboard::HideJni();
}

namespace AxiomUI {

void Init(const std::string& modRoot) {
    g_settingsPath = modRoot + "/axiom_settings.txt";
    g_iniPath = modRoot + "/axiom_ui.ini";
    LoadSettings();
}

bool IsVisible() { return g_visible.load(); }
void Toggle() { g_visible = !g_visible.load(); }
void Show() { g_visible = true; }
void Hide() { g_visible = false; }

void Draw() {
    static bool first = true;
    if (first) {
        ApplyStyle();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigWindowsResizeFromEdges = g_settings.resizeFromEdges;
        io.FontGlobalScale = g_settings.scale;
        if (!g_iniPath.empty()) io.IniFilename = g_iniPath.c_str();
        first = false;
    }

    DrawFloatingToggle();
    Keyboard::Frame();

    if (!g_visible.load()) return;

    ImGuiIO& io = ImGui::GetIO();
    if (g_settings.maximize) {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_FirstUseEver);
    }

    ImGui::Begin("Axiom-RE Framework", nullptr, ImGuiWindowFlags_NoCollapse);

    if (ImGui::Button("Hide")) g_visible = false;
    ImGui::SameLine();
    ImGui::Text("Touch src:%d  x:%.0f y:%.0f",
                TouchHook::Source(), TouchHook::X(), TouchHook::Y());

    ImGui::Separator();

    if (ImGui::BeginTabBar("AxiomTabs")) {

        if (ImGui::BeginTabItem("Dashboard")) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Status: ACTIVE");
            ImGui::Separator();
            ImGui::Text("Hook engine: Frida GUM");
            ImGui::Text("Disassembler: Capstone");
            ImGui::Text("Modules loaded: %zu", Memory::Modules().size());
            ImGui::Text("Touch: %s", TouchHook::Source() > 0 ? "CONNECTED" : "NONE");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Memory")) {
            DrawMemoryTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Disassembler")) {
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("##addr", g_addrInput, sizeof(g_addrInput));
            ImGui::SameLine();
            if (ImGui::Button("Disassemble")) {
                uint64_t addr = strtoull(g_addrInput, nullptr, 16);
                if (addr) g_instructions = Disasm::Disassemble(addr, 256, 15);
            }

            ImGui::Separator();

            if (ImGui::BeginChild("##disasm", ImVec2(0, 0), true)) {
                for (const auto& inst : g_instructions) {
                    ImVec4 color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
                    if (inst.mnemonic == "bl" || inst.mnemonic == "b")
                        color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                    else if (inst.mnemonic == "ret")
                        color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::Text("0x%llX:  %-6s %s",
                                (unsigned long long)inst.address,
                                inst.mnemonic.c_str(), inst.opStr.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Disassembler")) {
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("##addr", g_addrInput, sizeof(g_addrInput));
            ImGui::SameLine();
            if (ImGui::Button("Disassemble")) {
                uint64_t addr = strtoull(g_addrInput, nullptr, 16);
                if (addr) g_instructions = Disasm::Disassemble(addr, 256, 15);
            }
            ImGui::SameLine();
            if (ImGui::Button("Demo Fn")) {
                uint64_t fn = (uint64_t)(void*)&Memory::Refresh;
                snprintf(g_addrInput, sizeof(g_addrInput), "%llX", (unsigned long long)fn);
                g_instructions = Disasm::Disassemble(fn, 256, 15);
            }

            ImGui::Separator();

            if (ImGui::BeginChild("##disasm", ImVec2(0, 0), true)) {
                for (const auto& inst : g_instructions) {
                    ImVec4 color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
                    if (inst.mnemonic == "bl" || inst.mnemonic == "b")
                        color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                    else if (inst.mnemonic == "ret")
                        color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::Text("0x%llX:  %-6s %s",
                                (unsigned long long)inst.address,
                                inst.mnemonic.c_str(), inst.opStr.c_str());
                    ImGui::PopStyleColor();

                    // ---- 4d GOES HERE: tap a line to jump to that address ----
                    if (ImGui::IsItemClicked()) {
                        snprintf(g_addrInput, sizeof(g_addrInput), "%llX",
                                 (unsigned long long)inst.address);
                        g_instructions = Disasm::Disassemble(inst.address, 256, 15);
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
