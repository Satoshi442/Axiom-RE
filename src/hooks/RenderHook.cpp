#include "RenderHook.h"

#include <dlfcn.h>
#include <android/log.h>
#include <atomic>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include <frida-gum.h>

#include "../ui/AxiomUI.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

using EGLSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);

GumInterceptor* g_interceptor = nullptr;
GumInvocationListener* g_listener = nullptr;
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

void DrawOverlay(EGLDisplay display, EGLSurface surface) {
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
}

// Frida GUM invocation callbacks
void OnEnter(GumInvocationContext* ic, gpointer user_data) {
    EGLDisplay display = (EGLDisplay)gum_invocation_context_get_nth_argument(ic, 0);
    EGLSurface surface = (EGLSurface)gum_invocation_context_get_nth_argument(ic, 1);
    DrawOverlay(display, surface);
}

void OnLeave(GumInvocationContext* ic, gpointer user_data) {
    // Nothing needed on leave
}

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

    // Create a call listener with enter/leave callbacks
    g_listener = gum_make_call_listener(OnEnter, OnLeave, nullptr);
    if (!g_listener) {
        LOGE("Failed to create invocation listener");
        return false;
    }

    gum_interceptor_begin_transaction(g_interceptor);
    GumAttachReturn result = gum_interceptor_attach(
        g_interceptor,
        target,
        g_listener,
        nullptr
    );
    gum_interceptor_end_transaction(g_interceptor);

    if (result != GUM_ATTACH_OK) {
        LOGE("gum_interceptor_attach failed: %d", result);
        return false;
    }

    LOGI("Frida GUM render hook installed");
    return true;
}

void Uninstall() {
    if (g_interceptor && g_listener) {
        gum_interceptor_begin_transaction(g_interceptor);
        gum_interceptor_detach(g_interceptor, g_listener);
        gum_interceptor_end_transaction(g_interceptor);

        g_object_unref(g_listener);
        g_listener = nullptr;
    }

    if (g_interceptor) {
        g_object_unref(g_interceptor);
        g_interceptor = nullptr;
    }

    gum_deinit_embedded();
    LOGI("Render hook removed");
}

}
