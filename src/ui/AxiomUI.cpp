#include "AxiomUI.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "imgui.h"
#include "../analysis/Disasm.h"
#include "../analysis/Memory.h"
#include "../hooks/TouchHook.h"
#include "../hooks/Keyboard.h"
#include "../hooks/Clipboard.h"

namespace {
    std::atomic<bool> g_visible{true};
    struct Settings { float ballX=-1, ballY=-1, scale=1, alpha=0.95f, accent[3]={0.3f,0.7f,1.0f}; bool resizeFromEdges=true, maximize=false; };
    Settings g_settings;
    std::string g_settingsPath, g_iniPath;

    void LoadSettings() {
        if (g_settingsPath.empty()) return;
        FILE* f = fopen(g_settingsPath.c_str(), "r"); if (!f) return;
        int re=1, mx=0;
        if (fscanf(f, "%f %f %f %f %f %f %f %d %d", &g_settings.ballX, &g_settings.ballY, &g_settings.scale, &g_settings.alpha, &g_settings.accent[0], &g_settings.accent[1], &g_settings.accent[2], &re, &mx) == 9) {
            g_settings.resizeFromEdges = (re != 0); g_settings.maximize = (mx != 0);
        }
        fclose(f);
    }
    void SaveSettings() {
        if (g_settingsPath.empty()) return;
        FILE* f = fopen(g_settingsPath.c_str(), "w"); if (!f) return;
        fprintf(f, "%.2f %.2f %.2f %.2f %.3f %.3f %.3f %d %d", g_settings.ballX, g_settings.ballY, g_settings.scale, g_settings.alpha, g_settings.accent[0], g_settings.accent[1], g_settings.accent[2], g_settings.resizeFromEdges?1:0, g_settings.maximize?1:0);
        fclose(f);
    }
    uint32_t AccentU32() { return IM_COL32((int)(g_settings.accent[0]*255), (int)(g_settings.accent[1]*255), (int)(g_settings.accent[2]*255), 255); }
    void ApplyStyle() {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding=12; s.ChildRounding=8; s.FrameRounding=6; s.WindowPadding={12,12}; s.FramePadding={8,4}; s.TouchExtraPadding={12,12}; s.WindowBorderSize=1;
        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg]={0.08f,0.08f,0.12f,g_settings.alpha}; c[ImGuiCol_TitleBg]={0.1f,0.1f,0.15f,1}; c[ImGuiCol_TitleBgActive]={g_settings.accent[0]*0.3f,g_settings.accent[1]*0.3f,g_settings.accent[2]*0.3f,1};
        c[ImGuiCol_Text]={0.9f,0.9f,0.95f,1}; c[ImGuiCol_Button]={0.2f,0.2f,0.3f,1}; c[ImGuiCol_ButtonHovered]={0.3f,0.3f,0.45f,1}; c[ImGuiCol_ButtonActive]={g_settings.accent[0]*0.6f,g_settings.accent[1]*0.6f,g_settings.accent[2]*0.6f,1};
        c[ImGuiCol_FrameBg]={0.12f,0.12f,0.18f,1}; c[ImGuiCol_ResizeGrip]={g_settings.accent[0],g_settings.accent[1],g_settings.accent[2],0.9f};
    }

    char g_addrInput[32]="0x0"; std::vector<DisasmInstruction> g_instructions;
    int g_modIdx=-1; char g_memAddr[32]="0x0"; uint64_t g_viewAddr=0;
    char g_inspAddr[32]="0x0"; uint64_t g_inspAddrVal=0; int g_inspType=2; char g_inspWrite[32]="";
    float g_demoValue=100.0f; bool g_demoAnimate=true; float g_dragDist=0.0f;

    // Screen rectangles of our windows, published by the render thread and read by
    // the touch thread (TouchHook) to decide which touches to swallow.
    struct Rect {
        std::atomic<float> x0{0}, y0{0}, x1{0}, y1{0};
        std::atomic<bool> valid{false};
        void set(const ImVec2& pos, const ImVec2& size) { x0 = pos.x; y0 = pos.y; x1 = pos.x + size.x; y1 = pos.y + size.y; valid = true; }
        bool hit(float x, float y, float m) const { return valid.load() && x >= x0.load() - m && x <= x1.load() + m && y >= y0.load() - m && y <= y1.load() + m; }
    };
    Rect g_windowRect, g_ballRect;

    // ---------------------------------------------------------------- clipboard helpers ----

    // Accepts: "0x7F12AB", "7f12ab", text with spaces/newlines around it,
    // "libminecraftpe.so+0x1234" / "libminecraftpe.so + 1234" (module base + offset),
    // and "libfoo.so @ 7F12AB" (the label from the module list).
    bool ParseAddress(const std::string& in, uint64_t& out) {
        std::string s;
        for (char c : in) if (c != ' ' && c != '\t' && c != '\r' && c != '\n') s += c;
        if (s.empty()) return false;
        size_t at = s.find('@');
        if (at != std::string::npos) s = s.substr(at + 1);
        size_t plus = s.find('+');
        if (plus != std::string::npos && plus > 0) {
            std::string modName = s.substr(0, plus), off = s.substr(plus + 1);
            char* end = nullptr;
            uint64_t o = strtoull(off.c_str(), &end, 16);
            if (end == off.c_str()) return false;
            for (const auto& m : Memory::Modules())
                if (m.name.find(modName) != std::string::npos) { out = m.base + o; return true; }
            return false;
        }
        char* end = nullptr;
        uint64_t v = strtoull(s.c_str(), &end, 16);  // base 16 also accepts a 0x prefix
        if (end == s.c_str() || *end != '\0') return false;
        out = v;
        return true;
    }

    std::string g_toast; double g_toastUntil = 0;
    void CopyText(const std::string& t) {
        const bool system = Clipboard::Set(t.c_str());
        const std::string shown = t.size() > 48 ? t.substr(0, 48) + "..." : t;
        g_toast = (system ? "Copied: " : "Copied (Axiom only, system copy failed): ") + shown;
        g_toastUntil = ImGui::GetTime() + (system ? 1.5 : 4.0);
    }
    void CopyAddr(uint64_t a) { char b[32]; snprintf(b, sizeof(b), "0x%llX", (unsigned long long)a); CopyText(b); }

    // Text box + Copy button. Paste with the long-press menu (or the keyboard's paste);
    // ParseAddress() cleans the text up when Go / Read / Disassemble is pressed:
    // "0x7F12AB", "7f12ab", "libminecraftpe.so+0x1234", "libfoo.so @ 7F12AB" all work.
    void AddrField(const char* id, char* buf, size_t cap, float width) {
        ImGui::SetNextItemWidth(width);
        ImGui::InputText(id, buf, cap);
        ImGui::SameLine();
        if (ImGui::Button((std::string("Copy##") + id).c_str())) CopyText(buf);
    }

    // Touch has no right mouse button, so a ~0.5 s press without moving becomes a
    // right click. That opens the context menus: "Copy address..." on the lines below
    // and the Cut/Copy/Paste menu ImGui puts on every text box.
    void HandleLongPress() {
        ImGuiIO& io = ImGui::GetIO();
        static bool fired = false, release = false;
        if (release) { io.AddMouseButtonEvent(1, false); release = false; }
        if (!io.MouseDown[0]) { fired = false; return; }
        if (!fired && io.MouseDownDuration[0] > 0.5f && io.MouseDragMaxDistanceSqr[0] < 100.0f) {
            io.AddMouseButtonEvent(1, true);
            fired = true; release = true;
        }
    }

void DrawFloatingToggle() {
    ImGuiIO& io = ImGui::GetIO(); ImVec2 ds = io.DisplaySize; if (ds.x < 10) return;
    if (g_settings.ballX < 0) { g_settings.ballX = ds.x - 110; g_settings.ballY = 60; }
    if (g_settings.ballX < 0) g_settings.ballX = 0; if (g_settings.ballY < 0) g_settings.ballY = 0;
    if (g_settings.ballX > ds.x - 80) g_settings.ballX = ds.x - 80; if (g_settings.ballY > ds.y - 80) g_settings.ballY = ds.y - 80;

    ImGui::SetNextWindowPos({g_settings.ballX, g_settings.ballY}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({80, 80});
    ImGui::Begin("##AxiomBall", nullptr, ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoBackground);
    ImGui::InvisibleButton("##ballbtn", {80, 80});

    static bool wasActive = false;
    static ImVec2 startPos = {0, 0};
    bool isActive = ImGui::IsItemActive();

    if (ImGui::IsItemActivated()) {
        startPos = io.MousePos;
        g_dragDist = 0;
    }

    if (isActive) {
        float dx = io.MousePos.x - startPos.x;
        float dy = io.MousePos.y - startPos.y;
        g_dragDist = sqrtf(dx*dx + dy*dy);
        if (g_dragDist > 15) {  // Only move if dragged more than 15 pixels
            g_settings.ballX += io.MouseDelta.x;
            g_settings.ballY += io.MouseDelta.y;
        }
    }

    if (wasActive && !isActive) {
        if (g_dragDist < 20) {  // Tap = less than 20 pixels movement
            g_visible = !g_visible.load();
        } else {
            SaveSettings();
        }
    }
    wasActive = isActive;

    g_ballRect.set(ImGui::GetWindowPos(), ImGui::GetWindowSize());
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImVec2 center((mn.x+mx.x)*0.5f, (mn.y+mx.y)*0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(center, 36, AccentU32());
    dl->AddCircle(center, 36, IM_COL32(255,255,255,200), 0, 2);
    dl->AddText(nullptr, 20, {center.x-14, center.y-11}, IM_COL32(0,0,0,255), "AX");
    ImGui::End();
}

    void DrawMemoryTab() {
        if (g_demoAnimate) g_demoValue = 100.0f + 50.0f * sinf((float)ImGui::GetTime() * 2.0f);
        const auto& mods = Memory::Modules();
        if (ImGui::Button("Refresh Modules")) { Memory::Refresh(); g_modIdx = -1; }
        ImGui::SameLine();
        const char* preview = (g_modIdx >= 0 && g_modIdx < (int)mods.size()) ? mods[g_modIdx].name.c_str() : "Select module...";
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("##mods", preview)) {
            for (int i = 0; i < (int)mods.size(); i++) {
                char label[320]; snprintf(label, sizeof(label), "%s @ %llX", mods[i].name.c_str(), (unsigned long long)mods[i].base);
                if (ImGui::Selectable(label)) { g_modIdx = i; g_viewAddr = mods[i].base; snprintf(g_memAddr, sizeof(g_memAddr), "%llX", (unsigned long long)g_viewAddr); }
            }
            ImGui::EndCombo();
        }
        if (g_modIdx >= 0 && g_modIdx < (int)mods.size()) {
            ImGui::SameLine(); if (ImGui::Button("Copy base")) CopyAddr(mods[g_modIdx].base);
        }
        AddrField("##memaddr", g_memAddr, sizeof(g_memAddr), 160);
        ImGui::SameLine(); if (ImGui::Button("Go")) { uint64_t a = 0; if (ParseAddress(g_memAddr, a) && a) g_viewAddr = a; }
        ImGui::Separator();
        if (ImGui::BeginChild("##hex", {0, 220}, true)) {
            char line[160];
            for (int row = 0; row < 16; row++) {
                uint64_t addr = g_viewAddr + (uint64_t)row * 16; uint8_t buf[16]; bool ok = Memory::Read(addr, buf, 16);
                int off = snprintf(line, sizeof(line), "%012llX  ", (unsigned long long)addr);
                for (int i = 0; i < 16; i++) off += snprintf(line + off, sizeof(line) - off, ok ? "%02X " : "?? ", ok ? buf[i] : 0);
                off += snprintf(line + off, sizeof(line) - off, " ");
                for (int i = 0; i < 16; i++) { char ch = ok ? (char)buf[i] : '?'; if (ch < 32 || ch > 126 || ch == '#') ch = '.'; line[off++] = ch; }
                line[off] = 0;

                ImGui::PushID(row);
                if (ImGui::Selectable(line)) { g_inspAddrVal = addr; snprintf(g_inspAddr, sizeof(g_inspAddr), "%llX", (unsigned long long)addr); }
                if (ImGui::BeginPopupContextItem("rowctx")) {   // long-press a row
                    if (ImGui::MenuItem("Copy address")) CopyAddr(addr);
                    if (ImGui::MenuItem("Copy bytes (hex)")) {
                        std::string hex;
                        for (int i = 0; i < 16; i++) { char b[4]; snprintf(b, sizeof(b), ok ? "%02X" : "??", ok ? buf[i] : 0); hex += b; if (i < 15) hex += ' '; }
                        CopyText(hex);
                    }
                    if (ImGui::MenuItem("Copy line")) CopyText(line);
                    if (ImGui::MenuItem("Disassemble here")) { snprintf(g_addrInput, sizeof(g_addrInput), "%llX", (unsigned long long)addr); g_instructions = Disasm::Disassemble(addr, 256, 15); }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::Separator(); ImGui::Text("Inspector:");
        AddrField("##inspaddr", g_inspAddr, sizeof(g_inspAddr), 160);
        ImGui::SameLine(); if (ImGui::Button("Read")) { uint64_t a = 0; if (ParseAddress(g_inspAddr, a) && a) g_inspAddrVal = a; }
        const char* types[] = { "u8", "u16", "u32", "u64", "f32", "f64" }; ImGui::SetNextItemWidth(100); ImGui::Combo("##type", &g_inspType, types, 6);
        if (g_inspAddrVal) {
            char valText[64] = "?";
            switch (g_inspType) {
                case 0: { uint8_t v; if (Memory::Read(g_inspAddrVal, &v, 1)) snprintf(valText, sizeof(valText), "%u", v); break; }
                case 1: { uint16_t v; if (Memory::Read(g_inspAddrVal, &v, 2)) snprintf(valText, sizeof(valText), "%u", v); break; }
                case 2: { uint32_t v; if (Memory::Read(g_inspAddrVal, &v, 4)) snprintf(valText, sizeof(valText), "%u (0x%X)", v, v); break; }
                case 3: { uint64_t v; if (Memory::Read(g_inspAddrVal, &v, 8)) snprintf(valText, sizeof(valText), "%llu (0x%llX)", (unsigned long long)v, (unsigned long long)v); break; }
                case 4: { float v; if (Memory::Read(g_inspAddrVal, &v, 4)) snprintf(valText, sizeof(valText), "%.4f", v); break; }
                case 5: { double v; if (Memory::Read(g_inspAddrVal, &v, 8)) snprintf(valText, sizeof(valText), "%.4f", v); break; }
            }
            ImGui::Text("Live value: %s", valText);
            ImGui::SameLine(); if (ImGui::Button("Copy value")) CopyText(valText);
            AddrField("##writeval", g_inspWrite, sizeof(g_inspWrite), 140);
            ImGui::SameLine(); if (ImGui::Button("Write")) {
                bool done = false;
                switch (g_inspType) {
                    case 0: { uint8_t v = (uint8_t)strtoul(g_inspWrite, nullptr, 0); done = Memory::Write(g_inspAddrVal, &v, 1); break; }
                    case 1: { uint16_t v = (uint16_t)strtoul(g_inspWrite, nullptr, 0); done = Memory::Write(g_inspAddrVal, &v, 2); break; }
                    case 2: { uint32_t v = (uint32_t)strtoul(g_inspWrite, nullptr, 0); done = Memory::Write(g_inspAddrVal, &v, 4); break; }
                    case 3: { uint64_t v = strtoull(g_inspWrite, nullptr, 0); done = Memory::Write(g_inspAddrVal, &v, 8); break; }
                    case 4: { float v = strtof(g_inspWrite, nullptr); done = Memory::Write(g_inspAddrVal, &v, 4); break; }
                    case 5: { double v = strtod(g_inspWrite, nullptr); done = Memory::Write(g_inspAddrVal, &v, 8); break; }
                }
                if (!done) ImGui::TextColored({1, 0.3f, 0.3f, 1}, "Write failed (not writable)");
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Demo Var (writable test)")) { g_inspAddrVal = (uint64_t)&g_demoValue; snprintf(g_inspAddr, sizeof(g_inspAddr), "%llX", (unsigned long long)g_inspAddrVal); g_inspType = 4; }
        ImGui::SameLine(); ImGui::Checkbox("Animate", &g_demoAnimate);
    }

    void DrawSettingsTab() {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::SliderFloat("UI Scale", &g_settings.scale, 0.8f, 2.0f, "%.2f")) io.FontGlobalScale = g_settings.scale;
        if (ImGui::SliderFloat("Window Alpha", &g_settings.alpha, 0.30f, 1.0f, "%.2f")) ApplyStyle();
        if (ImGui::ColorEdit3("Accent Color", g_settings.accent)) ApplyStyle();
        if (ImGui::Checkbox("Resize From Edges", &g_settings.resizeFromEdges)) io.ConfigWindowsResizeFromEdges = g_settings.resizeFromEdges;
        ImGui::Checkbox("Maximize Main Window", &g_settings.maximize);
        if (ImGui::Button("Reset Ball Position")) { g_settings.ballX = io.DisplaySize.x - 110; g_settings.ballY = 60; }
        ImGui::SameLine(); if (ImGui::Button("Save Settings")) SaveSettings();
        ImGui::Separator();
    ImGui::TextWrapped("Clipboard: %s", Clipboard::Status());
    ImGui::Text("Keyboard: %s", Keyboard::Status());
    if (ImGui::Button("KB Show")) Keyboard::Show();
    ImGui::SameLine();
    if (ImGui::Button("KB Hide")) Keyboard::Hide();
    }
}

namespace AxiomUI {
    void Init(const std::string& modRoot) { g_settingsPath = modRoot + "/axiom_settings.txt"; g_iniPath = modRoot + "/axiom_ui.ini"; LoadSettings(); }
    bool HitTest(float x, float y) {
        if (g_ballRect.hit(x, y, 4.0f)) return true;
        return g_visible.load() && g_windowRect.hit(x, y, 4.0f);
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
            Clipboard::InstallImGui();
            first = false;
        }
        
        Keyboard::Frame();

        DrawFloatingToggle();
        if (!g_visible.load()) { g_windowRect.valid = false; return; }   // hidden window must not swallow touches
        HandleLongPress();

        ImGuiIO& io = ImGui::GetIO();
        if (g_settings.maximize) { ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always); ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always); }
        else { ImGui::SetNextWindowSize({520, 420}, ImGuiCond_FirstUseEver); }

        ImGui::Begin("Axiom-RE Framework", nullptr, ImGuiWindowFlags_NoCollapse);
        g_windowRect.set(ImGui::GetWindowPos(), ImGui::GetWindowSize());
        if (ImGui::Button("Hide")) g_visible = false;
        ImGui::SameLine(); ImGui::Text("Touch src:%d  x:%.0f y:%.0f", TouchHook::Source(), TouchHook::X(), TouchHook::Y());
        ImGui::Separator();

        if (ImGui::BeginTabBar("AxiomTabs")) {
            if (ImGui::BeginTabItem("Dashboard")) {
                ImGui::TextColored({0.3f, 1, 0.3f, 1}, "Status: ACTIVE");
                ImGui::Text("Hook engine: Frida GUM");
                ImGui::Text("Modules: %zu", Memory::Modules().size());
                ImGui::TextDisabled("Tip: long-press a line or text box for Copy / Paste.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Memory")) { DrawMemoryTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Disassembler")) {
                AddrField("##addr", g_addrInput, sizeof(g_addrInput), 150);
                ImGui::SameLine(); if (ImGui::Button("Disassemble")) { uint64_t a = 0; if (ParseAddress(g_addrInput, a) && a) g_instructions = Disasm::Disassemble(a, 256, 15); }
                ImGui::SameLine(); if (ImGui::Button("Demo Fn")) { uint64_t fn = (uint64_t)(void*)&Memory::Refresh; snprintf(g_addrInput, sizeof(g_addrInput), "%llX", (unsigned long long)fn); g_instructions = Disasm::Disassemble(fn, 256, 15); }
                ImGui::TextWrapped("%s", Disasm::Status());
                ImGui::Separator();
                if (ImGui::BeginChild("##disasm", {0, 0}, true)) {
                    uint64_t jumpTo = 0;   // applied after the loop: replacing g_instructions inside it would invalidate the iteration
                    int idx = 0;
                    for (const auto& inst : g_instructions) {
                        ImVec4 color = {0.9f, 0.9f, 0.9f, 1};
                        if (inst.mnemonic == "bl" || inst.mnemonic == "b") color = {1, 0.8f, 0.2f, 1};
                        else if (inst.mnemonic == "ret") color = {1, 0.3f, 0.3f, 1};
                        else if (inst.mnemonic == ".word" || inst.mnemonic == ".byte") color = {0.55f, 0.55f, 0.6f, 1};   // undecodable data
                        char row[256]; snprintf(row, sizeof(row), "0x%llX:  %-6s %s", (unsigned long long)inst.address, inst.mnemonic.c_str(), inst.opStr.c_str());

                        ImGui::PushID(idx++);
                        ImGui::PushStyleColor(ImGuiCol_Text, color);
                        bool clicked = ImGui::Selectable(row);
                        ImGui::PopStyleColor();
                        if (clicked) { snprintf(g_addrInput, sizeof(g_addrInput), "%llX", (unsigned long long)inst.address); jumpTo = inst.address; }
                        if (ImGui::BeginPopupContextItem("insnctx")) {   // long-press an instruction
                            if (ImGui::MenuItem("Copy address")) CopyAddr(inst.address);
                            if (ImGui::MenuItem("Copy instruction")) CopyText(inst.mnemonic + " " + inst.opStr);
                            if (ImGui::MenuItem("Copy line")) CopyText(row);
                            if (ImGui::MenuItem("Disassemble from here")) { snprintf(g_addrInput, sizeof(g_addrInput), "%llX", (unsigned long long)inst.address); jumpTo = inst.address; }
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                    }
                    if (jumpTo) g_instructions = Disasm::Disassemble(jumpTo, 256, 15);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings")) { DrawSettingsTab(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        if (ImGui::GetTime() < g_toastUntil) { ImGui::Separator(); ImGui::TextColored({0.4f, 1, 0.5f, 1}, "%s", g_toast.c_str()); }
        ImGui::End();
    }
}
