#include "Keyboard.h"

#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <pl/Input.hpp>

#include "imgui.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

const char* GATE_PATHS[] = {
    "/storage/emulated/0/Android/media/org.levimc.launcher/axiom_kb_enable.txt",
    "/sdcard/Android/media/org.levimc.launcher/axiom_kb_enable.txt",
};

char g_status[192] = "not installed";
bool g_enabled = false;
bool g_haveTextCb = false;
bool g_shown = false;

using ShowFn = void (*)();
using RegTextFn = void (*)(pl::input::TextInputCallback);
using RegKeyFn = void (*)(pl::input::KeyCallback);

ShowFn g_show = nullptr;
ShowFn g_hide = nullptr;

void* Try(const char* a, const char* b = nullptr) {
    void* p = dlsym(RTLD_DEFAULT, a);
    if (!p && b) p = dlsym(RTLD_DEFAULT, b);
    return p;
}

bool OnText(const pl::input::TextInputEvent& e) {
    if (!e.text.empty()) ImGui::GetIO().AddInputCharactersUTF8(e.text.c_str());
    return false;
}

bool OnKey(const pl::input::KeyEvent& e) {
    ImGuiIO& io = ImGui::GetIO();
    auto nav = [&](bool down) {
        switch (e.keyCode) {
            case 67: io.AddKeyEvent(ImGuiKey_Backspace, down); return true;
            case 66: io.AddKeyEvent(ImGuiKey_Enter, down); return true;
            case 21: io.AddKeyEvent(ImGuiKey_LeftArrow, down); return true;
            case 22: io.AddKeyEvent(ImGuiKey_RightArrow, down); return true;
            case 19: io.AddKeyEvent(ImGuiKey_UpArrow, down); return true;
            case 20: io.AddKeyEvent(ImGuiKey_DownArrow, down); return true;
            default: return false;
        }
    };
    if (e.isKeyDown) {
        if (!nav(true) && !g_haveTextCb && e.unicodeChar != 0)
            io.AddInputCharacter(e.unicodeChar);
    } else {
        nav(false);
    }
    return false;
}

void JniToggle() {
    JavaVM* vm = nullptr; jsize n = 0;
    using GetVmsFn = jint (*)(JavaVM**, jsize, jsize*);
    auto getVms = (GetVmsFn)dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
    if (!getVms || getVms(&vm, 1, &n) != JNI_OK || !vm) return;
    JNIEnv* env = nullptr; bool detach = false;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        detach = true;
    }
    if (!env) return;
    jclass at = env->FindClass("android/app/ActivityThread");
    if (!at || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jmethodID curApp = env->GetStaticMethodID(at, "currentApplication", "()Landroid/app/Application;");
    if (!curApp || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jobject app = env->CallStaticObjectMethod(at, curApp);
    if (!app || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jclass ctx = env->FindClass("android/content/Context");
    jmethodID gesm = env->GetMethodID(ctx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring nm = env->NewStringUTF("input_method");
    jobject imm = env->CallObjectMethod(app, gesm, nm);
    if (imm && !env->ExceptionCheck()) {
        jclass ic = env->GetObjectClass(imm);
        jmethodID toggle = env->GetMethodID(ic, "toggleSoftInput", "(II)V");
        if (toggle) env->CallVoidMethod(imm, toggle, 0, 0);
    }
    env->ExceptionClear();
    if (detach) vm->DetachCurrentThread();
}

}

namespace Keyboard {

bool Enabled() {
    for (const char* p : GATE_PATHS) if (access(p, F_OK) == 0) return true;
    return false;
}

bool Install() {
    g_enabled = Enabled();
    if (!g_enabled) {
        snprintf(g_status, sizeof(g_status), "gate OFF (create axiom_kb_enable.txt to enable)");
        LOGI("Keyboard: %s", g_status);
        return true;
    }

    g_show = (ShowFn)Try("_ZN2pl5input12showKeyboardEv");
    g_hide = (ShowFn)Try("_ZN2pl5input12hideKeyboardEv");

    auto regText = (RegTextFn)Try(
        "_ZN2pl5input25registerTextInputCallbackENSt6__ndk18functionIFbRKNS0_14TextInputEventEEEE",
        "_ZN2pl5input25registerTextInputCallbackENSt3__18functionIFbRKNS0_14TextInputEventEEEE");
    auto regKey = (RegKeyFn)Try(
        "_ZN2pl5input19registerKeyCallbackENSt6__ndk18functionIFbRKNS0_8KeyEventEEEE",
        "_ZN2pl5input19registerKeyCallbackENSt3__18functionIFbRKNS0_8KeyEventEEEE");

    if (regText) { regText(OnText); g_haveTextCb = true; }
    if (regKey)  { regKey(OnKey); }

    snprintf(g_status, sizeof(g_status), "gate ON show:%s hide:%s text:%s key:%s",
             g_show ? "Y" : "N", g_hide ? "Y" : "N",
             regText ? "Y" : "N", regKey ? "Y" : "N");
    LOGI("Keyboard: %s", g_status);
    return true;
}

void Frame() {
    if (!g_enabled) return;
    bool want = ImGui::GetIO().WantTextInput;
    if (want && !g_shown) { Show(); g_shown = true; }
    else if (!want && g_shown) { Hide(); g_shown = false; }
}

void Show() { if (g_show) g_show(); else JniToggle(); }
void Hide() { if (g_hide) g_hide(); else JniToggle(); }
const char* Status() { return g_status; }

}
