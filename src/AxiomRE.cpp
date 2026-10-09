#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <android/log.h>

#include "hooks/RenderHook.h"
#include "ui/AxiomUI.h"
#include "analysis/Disasm.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

bool g_buttonRegistered = false;

void OnToggleButtonPressed(
    std::string_view buttonId,
    pl::modmenu::ButtonEvent event,
    float value
) {
    LOGI("Button pressed! Event: %d", static_cast<int>(event));
    if (event == pl::modmenu::ButtonEvent::Down) {
        AxiomUI::Toggle();
        LOGI("Axiom-RE UI toggled: %s", AxiomUI::IsVisible() ? "visible" : "hidden");
    }
}

void RegisterModMenuButton() {
    LOGI("Attempting to register mod menu button...");
    
    try {
        auto result = pl::modmenu::ButtonBuilder("axiom_re.toggle", "Toggle Axiom-RE")
            .moduleId("axiom_re")
            .label("Toggle Axiom-RE")
            .behavior(pl::modmenu::ButtonBehavior::Click)
            .onEvent(OnToggleButtonPressed)
            .registerButton();
        
        g_buttonRegistered = true;
        LOGI("Mod menu button registered successfully!");
    } catch (...) {
        LOGE("Failed to register mod menu button!");
    }
}

}

class AxiomRE {
public:
    bool load(pl::mod::ModContext &context) {
        LOGI("=== Axiom-RE LOADING ===");
        
        // Initialize Capstone
        LOGI("Initializing Capstone...");
        Disasm::Initialize();
        LOGI("Capstone initialized");

        // Install render hook
        LOGI("Installing render hook...");
        if (!RenderHook::Install()) {
            LOGE("Failed to install render hook!");
        } else {
            LOGI("Render hook installed successfully");
        }

        // Register mod menu button IMMEDIATELY in load()
        LOGI("Registering mod menu button...");
        RegisterModMenuButton();

        LOGI("=== Axiom-RE LOADED ===");
        return true;
    }

    bool enable(pl::mod::ModContext &context) {
        LOGI("Axiom-RE ENABLED");
        
        // Make sure button is registered
        if (!g_buttonRegistered) {
            LOGI("Button not registered yet, registering now...");
            RegisterModMenuButton();
        }
        
        return true;
    }

    bool disable(pl::mod::ModContext &context) {
        LOGI("Axiom-RE DISABLED");
        AxiomUI::Hide();
        return true;
    }

    bool unload(pl::mod::ModContext &context) {
        LOGI("Axiom-RE UNLOADING");
        Disasm::Shutdown();
        RenderHook::Uninstall();
        LOGI("Axiom-RE UNLOADED");
        return true;
    }
};

AxiomRE g_axiomRE;
PL_REGISTER_MOD(AxiomRE, g_axiomRE)
