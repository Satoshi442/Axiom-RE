#include "Keyboard.h"
#include <android/log.h>
#include <dlfcn.h>
#include <elf.h>
#include <jni.h>
#include <link.h>
#include <unistd.h>
#include <atomic>
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
char g_status[160] = "not installed";
bool g_enabled = false;
bool g_haveTextCb = false;
bool g_shown = false;

using ShowFn = void (*)();
using RegTextFn = void (*)(pl::input::TextInputCallback);
using RegKeyFn = void (*)(pl::input::KeyCallback);
ShowFn g_show = nullptr;
ShowFn g_hide = nullptr;

struct ScanCtx { const char* needle; void* out; };

bool ScanLib(const struct dl_phdr_info* info, const char* needle, void** out) {
    const ElfW(Dyn)* dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = (const ElfW(Dyn)*)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return false;
    const ElfW(Sym)* sym = nullptr;
    const char* str = nullptr;
    size_t strsz = 0;
    for (const ElfW(Dyn)* d = dyn; d->d_tag != DT_NULL; d++) {
        if (d->d_tag == DT_SYMTAB) sym = (const ElfW(Sym)*)d->d_un.d_ptr;
        else if (d->d_tag == DT_STRTAB) str = (const char*)d->d_un.d_ptr;
        else if (d->d_tag == DT_STRSZ) strsz = d->d_un.d_val;
    }
    if (!sym || !str || !strsz) return false;
    for (size_t i = 1; i < 300000; i++) {
        ElfW(Word) n = sym[i].st_name;
        if (n == 0 || n >= strsz) continue;
        if (sym[i].st_value && strstr(str + n, needle)) {
            *out = (void*)(info->dlpi_addr + sym[i].st_value);
            return true;
        }
    }
    return false;
}

int PhdrCallback(struct dl_phdr_info* info, size_t, void* data) {
    ScanCtx* c = (ScanCtx*)data;
    if (ScanLib(info, c->needle, &c->out)) return 1;
    return 0;
}

void* FindBySubstring(const char* needle) {
    ScanCtx c{needle, nullptr};
    dl_iterate_phdr(PhdrCallback, &c);
    return c.out;
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
    } else nav(false);
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
    g_show = (ShowFn)FindBySubstring("12showKeyboardEv");
    g_hide = (ShowFn)FindBySubstring("12hideKeyboardEv");
    auto regText = (RegTextFn)FindBySubstring("25registerTextInputCallbackE");
    auto regKey = (RegKeyFn)FindBySubstring("19registerKeyCallbackE");
    if (regText) { regText(OnText); g_haveTextCb = true; }
    if (regKey) regKey(OnKey);
    snprintf(g_status, sizeof(g_status), "gate ON show:%s hide:%s text:%s key:%s",
             g_show ? "Y" : "N", g_hide ? "Y" : "N", regText ? "Y" : "N", regKey ? "Y" : "N");
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
