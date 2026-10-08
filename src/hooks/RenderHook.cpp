#include "RenderHook.h"

#include <dlfcn.h>
#include <android/log.h>
#include <atomic>

#include <EGL/egl.h>

#define IMGUI_IMPL_OPENGL_ES3
#include <GLES3/gl3.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "../ui/AxiomUI.h"
#include "dobby.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

using EGLSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);

void* g_target = nullptr;
EGLSwapBuffersFn g_original = nullptr;

std::atomic<bool> g_imguiInitialized{false};

void InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui::GetIO().Fonts->AddFontDefault();

    ImGui_ImplOpenGL3_Init("#version 300 es");

    g_imguiInitialized = true;

    LOGI("ImGui initialized");
}

EGLBoolean HookedEglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    if (!g_imguiInitialized.load()) {
        InitImGui();
    }

    if (g_imguiInitialized.load() && AxiomUI::IsVisible()) {
        EGLint width = 0;
        EGLint height = 0;

        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);

        if (width > 0 && height > 0) {
            ImGui_ImplOpenGL3_NewFrame();

            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
            io.DeltaTime = 1.0f / 60.0f;

            ImGui::NewFrame();

            AxiomUI::Draw();

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
    }

    if (g_original) {
        return g_original(display, surface);
    }

    return EGL_FALSE;
}

}

namespace RenderHook {

bool Install() {
    if (g_target) {
        return true;
    }

    g_target = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!g_target) {
        LOGE("Failed to find eglSwapBuffers");
        return false;
    }

    int result = DobbyHook(
        g_target,
        reinterpret_cast<void*>(&HookedEglSwapBuffers),
        reinterpret_cast<void**>(&g_original)
    );

    if (result != 0) {
        LOGE("DobbyHook failed: %d", result);
        return false;
    }

    LOGI("Render hook installed");
    return true;
}

void Uninstall() {
    if (g_target) {
        DobbyUnhook(g_target);
        g_target = nullptr;
        g_original = nullptr;
        LOGI("Render hook removed");
    }
}

}
