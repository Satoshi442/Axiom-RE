#include "Keyboard.h"

#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include <atomic>
#include <cstdio>
#include <pl/Input.hpp>

#include "imgui.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

using ShowFn = void (*)();
using RegTextFn = void (*)(pl::input::TextInputCallback);
using RegKeyFn = void (*)(pl::input::KeyCallback);

ShowFn g_show = nullptr;
ShowFn g_hide = nullptr;
bool g_haveTextCb = false;
bool g_shown = false;

std::atomic<int> g_showCalls{0};
std::atomic<bool> g_pendingJniToggle{false};
char g_status[192] = "not installed";

template <typename T>
T FindSym(const char* a, const char* b) {
    void* p = dlsym(RTLD_DEFAULT, a);
    if (!p && b) p = dlsym(RTLD_DEFAULT, b);
    return reinterpret_cast<T>(p);
}

bool OnText(const pl::input::TextInputEvent& e) {
    ImGuiIO& io = ImGui::GetIO();
    if (!e.text.empty()) io.AddInputCharactersUTF8(e.text.c_str());
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

JNIEnv* GetEnv(bool* detach) {
    JavaVM* vm = nullptr;
    jsize n = 0;
    using GetVmsFn = jint (*)(JavaVM**, jsize, jsize*);
    auto getVms = (GetVmsFn)dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
    if (!getVms || getVms(&vm, 1, &n) != JNI_OK || !vm) return nullptr;
    JNIEnv* env = nullptr;
    jint rc = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (rc == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
        *detach = true;
    }
    return env;
}

void JniToggle() {
    bool detach = false;
    JNIEnv* env = GetEnv(&detach);
    if (!env) { LOGE("KB: JNI env failed"); return; }

    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (!atClass || env->ExceptionCheck()) { env->ExceptionClear(); LOGE("KB: no ActivityThread"); return; }
    jmethodID curApp = env->GetStaticMethodID(atClass, "currentApplication", "()Landroid/app/Application;");
    if (!curApp || env->ExceptionCheck()) { env->ExceptionClear(); LOGE("KB: no currentApplication"); return; }
    jobject app = env->CallStaticObjectMethod(atClass, curApp);
    if (!app || env->ExceptionCheck()) { env->ExceptionClear(); LOGE("KB: no app"); return; }

    jclass ctxClass = env->FindClass("android/content/Context");
    jmethodID gesm = env->GetMethodID(ctxClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring name = env->NewStringUTF("input_method");
    jobject imm = env->CallObjectMethod(app, gesm, name);
    if (!imm || env->ExceptionCheck()) { env->ExceptionClear(); LOGE("KB: no IMM"); return; }

    jclass immClass = env->GetObjectClass(imm);
    jmethodID toggle = env->GetMethodID(immClass, "toggleSoftInput", "(II)V");
    if (toggle && !env->ExceptionCheck()) {
        env->CallVoidMethod(imm, toggle, 0, 0);
        LOGI("KB: JNI toggleSoftInput called");
    } else {
        LOGE("KB: no toggleSoftInput");
    }
    env->ExceptionClear();
    if (detach) { JavaVM* vm = nullptr; env->GetJavaVM(&vm); if (vm) vm->DetachCurrentThread(); }
}

void DoShow() {
    g_showCalls++;
    if (g_show) { g_show(); LOGI("KB: official showKeyboard called"); }
    else { g_pendingJniToggle = true; JniToggle(); }
}

void DoHide() {
    if (g_hide) { g_hide(); LOGI("KB: official hideKeyboard called"); }
    else { g_pendingJniToggle = true; }
}

}

namespace Keyboard {

bool Install() {
    g_show = FindSym<ShowFn>("_ZN2pl5input12showKeyboardEv", nullptr);
    g_hide = FindSym<ShowFn>("_ZN2pl5input12hideKeyboardEv", nullptr);

    auto regText = FindSym<RegTextFn>(
        "_ZN2pl5input25registerTextInputCallbackENSt6__ndk18functionIFbRKNS0_14TextInputEventEEE",
        "_ZN2pl5input25registerTextInputCallbackENSt3__18functionIFbRKNS0_14TextInputEventEEE");
    auto regKey = FindSym<RegKeyFn>(
        "_ZN2pl5input19registerKeyCallbackENSt6__ndk18functionIFbRKNS0_8KeyEventEEE",
        "_ZN2pl5input19registerKeyCallbackENSt3__18functionIFbRKNS0_8KeyEventEEE");

    if (regText) { regText(OnText); g_haveTextCb = true; }
    if (regKey)  { regKey(OnKey); }

    snprintf(g_status, sizeof(g_status),
             "show:%s hide:%s text:%s key:%s",
             g_show ? "YES" : "no", g_hide ? "YES" : "no",
             regText ? "YES" : "no", regKey ? "YES" : "no");
    LOGI("Keyboard: %s", g_status);
    return true;
}

void Frame() {
    // auto show/hide disabled during touch diagnosis; use manual buttons in Settings
}

void PumpMainThreadJobs() {
    if (g_pendingJniToggle.exchange(false)) JniToggle();
}

const char* Status() { return g_status; }
int ShowCalls() { return g_showCalls.load(); }

void ShowOfficial() { if (g_show) g_show(); }
void HideOfficial() { if (g_hide) g_hide(); }
void ShowJni() { JniToggle(); }
void HideJni() { JniToggle(); }

}
