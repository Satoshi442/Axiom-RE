#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

#include <android/log.h>

#include "hooks/RenderHook.h"
#include "ui/AxiomUI.h"

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
    if (event == pl::modmenu::ButtonEvent::Down) {
        AxiomUI::Toggle();

        LOGI(
            "Axiom-RE UI toggled: %s",
            AxiomUI::IsVisible() ? "visible" : "hidden"
        );
    }
}

void RegisterModMenuButton() {
    pl::modmenu::ButtonBuilder("axiom_re.toggle", "Toggle Axiom-RE")
        .moduleId("axiom_re")
        .label("Toggle floating UI")
        .behavior(pl::modmenu::ButtonBehavior::Click)
        .onEvent(OnToggleButtonPressed)
        .registerButton();

    g_buttonRegistered = true;

    LOGI("Mod menu button registered");
}

void UnregisterModMenuButton() {
    pl::modmenu::unregisterButton("axiom_re.toggle");
    g_buttonRegistered = false;

    LOGI("Mod menu button unregistered");
}

}

class AxiomRE {
public:
    bool load(pl::mod::ModContext &context) {
        LOGI("Loading Axiom-RE...");

        if (!RenderHook::Install()) {
            LOGE("Failed to install render hook");
        }

        LOGI("Axiom-RE loaded");
        return true;
    }

    bool enable(pl::mod::ModContext &context) {
        LOGI("Enabling Axiom-RE...");

        if (!g_buttonRegistered) {
            RegisterModMenuButton();
        }

        return true;
    }

    bool disable(pl::mod::ModContext &context) {
        LOGI("Disabling Axiom-RE...");

        if (g_buttonRegistered) {
            UnregisterModMenuButton();
        }

        AxiomUI::Hide();

        return true;
    }

    bool unload(pl::mod::ModContext &context) {
        LOGI("Unloading Axiom-RE...");

        RenderHook::Uninstall();

        LOGI("Axiom-RE unloaded");
        return true;
    }
};

AxiomRE g_axiomRE;

PL_REGISTER_MOD(AxiomRE, g_axiomRE)
