#include "Keyboard.h"

#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include <pl/Input.hpp>

#include "imgui.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

using ShowFn = void (*)();
using HideFn = void (*)();
using RegTextFn = void (*)(pl::input::TextInputCallback);
using RegKeyFn = void (*)(pl::input::KeyCallback);

ShowFn g_show = nullptr;
HideFn g_hide = nullptr;
bool g_haveTextCb = false;
bool g_shown = false;

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
            case 66: io.AddKeyEvent(ImGuiKey_Enter, down);     return true;
            case 21: io.AddKeyEvent(ImGuiKey_LeftArrow, down); return true;
            case 22: io.AddKeyEvent(ImGuiKey_RightArrow, down);return true;
            case 19: io.AddKeyEvent(ImGuiKey_UpArrow, down);   return true;
            case 20: io.AddKeyEvent(ImGuiKey_DownArrow, down); return true;
            default: return false;
        }
    };

    if (e.isKeyDown) {
        if (!nav(true)) {
            // Only insert chars here if we have no committed-text callback
            if (!g_haveTextCb && e.unicodeChar != 0) {
                io.AddInputCharacter(e.unicodeChar);
            }
        }
    } else {
        nav(false);
    }
    return false;
}

// ---------- JNI fallback: InputMethodManager.toggleSoftInput ----------
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

void JniToggleKeyboard() {
    bool detach = false;
    JNIEnv* env = GetEnv(&detach);
    if (!env) return;

    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (!atClass || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jmethodID curApp = env->GetStaticMethodID(
        atClass, "currentApplication", "()Landroid/app/Application;");
    if (!curApp || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jobject app = env->CallStaticObjectMethod(atClass, curApp);
    if (!app || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jclass ctxClass = env->FindClass("android/content/Context");
    jmethodID gesm = env->GetMethodID(
        ctxClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!gesm || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jstring name = env->NewStringUTF("input_method");
    jobject imm = env->CallObjectMethod(app, gesm, name);
    if (!imm || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jclass immClass = env->GetObjectClass(imm);
    jmethodID toggle = env->GetMethodID(immClass, "toggleSoftInput", "(II)V");
    if (toggle && !env->ExceptionCheck()) {
        env->CallVoidMethod(imm, toggle, 0, 0);
    }
    env->ExceptionClear();

    if (detach) {
        JavaVM* vm = nullptr;
        env->GetJavaVM(&vm);
        if (vm) vm->DetachCurrentThread();
    }
}

void Show() {
    if (g_show) g_show();
    else JniToggleKeyboard();
}

void Hide() {
    if (g_hide) g_hide();
    else JniToggleKeyboard();
}

}

namespace Keyboard {

bool Install() {
    g_show = FindSym<ShowFn>("_ZN2pl5input12showKeyboardEv", nullptr);
    g_hide = FindSym<HideFn>("_ZN2pl5input12hideKeyboardEv", nullptr);

    auto regText = FindSym<RegTextFn>(
        "_ZN2pl5input25registerTextInputCallbackENSt6__ndk18functionIFbRKNS0_14TextInputEventEEE",
        "_ZN2pl5input25registerTextInputCallbackENSt3__18functionIFbRKNS0_14TextInputEventEEE");
    auto regKey = FindSym<RegKeyFn>(
        "_ZN2pl5input19registerKeyCallbackENSt6__ndk18functionIFbRKNS0_8KeyEventEEE",
        "_ZN2pl5input19registerKeyCallbackENSt3__18functionIFbRKNS0_8KeyEventEEE");

    if (regText) {
        regText(OnText);
        g_haveTextCb = true;
        LOGI("Keyboard: text input callback registered");
    }
    if (regKey) {
        regKey(OnKey);
        LOGI("Keyboard: key callback registered");
    }

    if (g_show) LOGI("Keyboard: official show/hide API found");
    else        LOGI("Keyboard: using JNI InputMethodManager fallback");

    if (!regText && !regKey) {
        LOGE("Keyboard: no char input source - keyboard shows but typing may not reach ImGui");
    }
    return true;
}

void Frame() {
    ImGuiIO& io = ImGui::GetIO();
    bool want = io.WantTextInput;

    if (want && !g_shown) {
        Show();
        g_shown = true;
    } else if (!want && g_shown) {
        Hide();
        g_shown = false;
    }
}

}
