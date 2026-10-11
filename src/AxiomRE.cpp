#include <pl/Mod.hpp>
#include <android/log.h>

#include "hooks/RenderHook.h"
#include "hooks/TouchHook.h"
#include "ui/AxiomUI.h"
#include "analysis/Disasm.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

class AxiomRE {
public:
    bool load(pl::mod::ModContext &context) {
        LOGI("=== Axiom-RE LOADING ===");

        Disasm::Initialize();

        if (!RenderHook::Install()) {
            LOGE("Failed to install render hook!");
        }

        if (!TouchHook::Install()) {
            LOGE("Failed to install touch hook!");
        }

        AxiomUI::Show();

        LOGI("=== Axiom-RE LOADED ===");
        return true;
    }

    bool enable(pl::mod::ModContext &context) {
        LOGI("Axiom-RE ENABLED");
        AxiomUI::Show();
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
        TouchHook::Uninstall();
        RenderHook::Uninstall();
        LOGI("Axiom-RE UNLOADED");
        return true;
    }
};

AxiomRE g_axiomRE;
PL_REGISTER_MOD(AxiomRE, g_axiomRE)
