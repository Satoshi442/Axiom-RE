#include "RenderHook.h"

#include <dlfcn.h>
#include <android/log.h>
#include <atomic>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include <dobby.h>

#include "../ui/AxiomUI.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

using EGLSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
EGLSwapBuffersFn orig_eglSwapBuffers = nullptr;
std::atomic<bool> g_imguiInitialized{false};

void InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().TouchExtraPadding = ImVec2(10.0f, 10.0f);
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
        EGLint width = 0, height = 0;
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);

        if (width > 0 && height > 0) {
            ImGui_ImplOpenGL3_NewFrame();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2((float)width, (float)height);
            io.DeltaTime = 1.0f / 60.0f;

            ImGui::NewFrame();
            AxiomUI::Draw();
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
    }

    return orig_eglSwapBuffers(display, surface);
}

}

namespace RenderHook {

bool Install() {
    void* target = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!target) {
        LOGE("Failed to find eglSwapBuffers");
        return false;
    }

    int ret = DobbyHook(target, (void*)HookedEglSwapBuffers, (void**)&orig_eglSwapBuffers);
    if (ret != 0) {
        LOGE("DobbyHook failed for eglSwapBuffers: %d", ret);
        return false;
    }

    LOGI("Render hook installed via Dobby");
    return true;
}

void Uninstall() {
    void* target = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (target) {
        DobbyDestroy(target);
    }
}

}
