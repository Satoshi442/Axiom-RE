#include "RenderHook.h"

#include <dlfcn.h>
#include <android/log.h>
#include <atomic>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include <frida-gum.h>

#include "TouchHook.h"
#include "../ui/AxiomUI.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

using EGLSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);

GumInterceptor* g_interceptor = nullptr;
GumInvocationListener* g_listener = nullptr;
std::atomic<bool> g_imguiInitialized{false};
std::atomic<long long> g_frames{0};

void InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    // Fat-finger friendly touch padding
    ImGui::GetStyle().TouchExtraPadding = ImVec2(10.0f, 10.0f);

    ImGui::GetIO().Fonts->AddFontDefault();
    ImGui_ImplOpenGL3_Init("#version 300 es");
    
    g_imguiInitialized = true;
    LOGI("ImGui initialized");
}

void DrawOverlay(EGLDisplay display, EGLSurface surface) {
    g_frames++;
    if (!g_imguiInitialized.load()) {
        InitImGui();
    }

    EGLint width = 0;
    EGLint height = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &height);
    if (width <= 0 || height <= 0) return;

    ImGui_ImplOpenGL3_NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = 1.0f / 60.0f;

    // ---- PHASE 1.5: feed touch into ImGui ----
    io.AddMousePosEvent(TouchHook::X(), TouchHook::Y());
    io.AddMouseButtonEvent(0, TouchHook::Down());

    ImGui::NewFrame();
    AxiomUI::Draw();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void OnEnter(GumInvocationContext* ic, gpointer user_data) {
    EGLDisplay display = (EGLDisplay)gum_invocation_context_get_nth_argument(ic, 0);
    EGLSurface surface = (EGLSurface)gum_invocation_context_get_nth_argument(ic, 1);
    DrawOverlay(display, surface);
}

void OnLeave(GumInvocationContext* ic, gpointer user_data) {}

}

namespace RenderHook {

bool Install() {
    gum_init_embedded();

    g_interceptor = gum_interceptor_obtain();
    if (!g_interceptor) {
        LOGE("Failed to obtain GumInterceptor");
        return false;
    }

    void* target = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!target) {
        LOGE("Failed to find eglSwapBuffers");
        return false;
    }

    g_listener = gum_make_call_listener(OnEnter, OnLeave, nullptr, nullptr);
    if (!g_listener) {
        LOGE("Failed to create listener");
        return false;
    }

    gum_interceptor_begin_transaction(g_interceptor);
    GumAttachReturn result = gum_interceptor_attach(
        g_interceptor, target, g_listener, nullptr);
    gum_interceptor_end_transaction(g_interceptor);

    if (result != GUM_ATTACH_OK) {
        LOGE("gum_interceptor_attach failed: %d", result);
        return false;
    }

    LOGI("Render hook installed");
    return true;
}

void Uninstall() {
    if (g_interceptor && g_listener) {
        gum_interceptor_detach(g_interceptor, g_listener);
        g_object_unref(g_listener);
        g_listener = nullptr;
    }
    if (g_interceptor) {
        g_object_unref(g_interceptor);
        g_interceptor = nullptr;
    }
    gum_deinit_embedded();
}
long long FrameCount() { return g_frames.load(); }

}
