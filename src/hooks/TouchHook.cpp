#include "TouchHook.h"
#include <android/log.h>
#include <android/input.h>
#include <dlfcn.h>
#include <jni.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <frida-gum.h>
#include <pl/Input.hpp>

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
std::atomic<float> g_x{0.0f};
std::atomic<float> g_y{0.0f};
std::atomic<bool> g_down{false};
std::atomic<int> g_source{0};
GumInterceptor* g_intc = nullptr;
std::vector<GumInvocationListener*> g_listeners;
std::vector<void*> g_targets;

void SetTouch(float x, float y, bool down, int src) {
    g_x.store(x); g_y.store(y); g_down.store(down); g_source.store(src);
}

bool MotionDown(int32_t maskedAction) {
    return maskedAction == AMOTION_EVENT_ACTION_DOWN || maskedAction == AMOTION_EVENT_ACTION_MOVE || maskedAction == AMOTION_EVENT_ACTION_POINTER_DOWN;
}

bool Attach(void* target, GumInvocationCallback enter, GumInvocationCallback leave) {
    if (!target) return false;
    if (!g_intc) { gum_init_embedded(); g_intc = gum_interceptor_obtain(); }
    if (!g_intc) return false;
    GumInvocationListener* listener = gum_make_call_listener(enter, leave, nullptr, nullptr);
    if (!listener) return false;
    gum_interceptor_begin_transaction(g_intc);
    GumAttachReturn r = gum_interceptor_attach(g_intc, target, listener, nullptr);
    gum_interceptor_end_transaction(g_intc);
    if (r != GUM_ATTACH_OK) { g_object_unref(listener); return false; }
    g_listeners.push_back(listener); g_targets.push_back(target); return true;
}

// ---------------------------------------------------------------------------
// Official path: pl::input::registerTouchCallback (the launcher calls it for
// every Android touch event).
//
// The mangled names below are the exact libc++ (NDK) manglings, verified with
// c++filt. The previous strings were each missing a trailing 'E', so dlsym()
// never found them, this path silently failed, and Source() stayed 0.
// ---------------------------------------------------------------------------
bool OnOfficialTouch(const pl::input::TouchEvent& e) {
    const int a = e.action & 0xFF;  // strip pointer-index bits
    const bool down = (a == 0 || a == 2 || a == 5);  // DOWN, MOVE, POINTER_DOWN
    SetTouch(e.x, e.y, down, 1);
    return false;  // false = the game still receives the touch
}

void* FindPreloaderSymbol(const char* name) {
    // The launcher already loaded libpreloader.so; NOLOAD just returns it.
    if (void* h = dlopen("libpreloader.so", RTLD_NOW | RTLD_NOLOAD)) {
        if (void* fn = dlsym(h, name)) return fn;
    }
    return dlsym(RTLD_DEFAULT, name);
}

bool TryOfficialApi() {
    // std::function<bool(const TouchEvent&)> version (preloader main)
    const char* kFunctionSym = "_ZN2pl5input21registerTouchCallbackENSt6__ndk18functionIFbRKNS0_10TouchEventEEEE";
    // plain function-pointer version (older preloader)
    const char* kPointerSym = "_ZN2pl5input21registerTouchCallbackEPFbRKNS0_10TouchEventEE";

    if (void* fn = FindPreloaderSymbol(kFunctionSym)) {
        using RegFn = void (*)(std::function<bool(const pl::input::TouchEvent&)>);
        reinterpret_cast<RegFn>(fn)(OnOfficialTouch);
        LOGI("Touch: registered via std::function symbol");
        return true;
    }
    if (void* fn = FindPreloaderSymbol(kPointerSym)) {
        using RegFn = void (*)(bool (*)(const pl::input::TouchEvent&));
        reinterpret_cast<RegFn>(fn)(OnOfficialTouch);
        LOGI("Touch: registered via function-pointer symbol");
        return true;
    }
    LOGE("Touch: registerTouchCallback not found in libpreloader.so");
    return false;
}

void OnQueueLeave(GumInvocationContext* ic, gpointer) {
    gint32 ret = (gint32)(gintptr)gum_invocation_context_get_return_value(ic);
    if (ret < 0) return;
    AInputEvent** pp = (AInputEvent**)gum_invocation_context_get_nth_argument(ic, 1);
    if (!pp || !*pp) return;
    AInputEvent* ev = *pp;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return;
    int32_t a = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
    SetTouch(AMotionEvent_getX(ev, 0), AMotionEvent_getY(ev, 0), MotionDown(a), 2);
}

bool TryInputQueue() {
    void* h = dlopen("libandroid.so", RTLD_NOW);
    void* fn = dlsym(h ? h : RTLD_DEFAULT, "AInputQueue_getEvent");
    if (!fn) return false;
    if (!Attach(fn, nullptr, OnQueueLeave)) return false;
    LOGI("Touch: hooked AInputQueue_getEvent"); return true;
}

void OnJniTouchEnter(GumInvocationContext* ic, gpointer) {
    AInputEvent* ev = (AInputEvent*)gum_invocation_context_get_nth_argument(ic, 2);
    if (!ev) return;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return;
    int32_t a = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
    SetTouch(AMotionEvent_getX(ev, 0), AMotionEvent_getY(ev, 0), MotionDown(a), 3);
}

std::vector<std::string> LoadedLibs() {
    std::vector<std::string> libs; FILE* f = fopen("/proc/self/maps", "r"); if (!f) return libs; char line[512];
    while (fgets(line, sizeof(line), f)) { char* start = strchr(line, '/'); if (!start) continue; size_t len = strlen(start); while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == ' ')) len--; std::string p(start, len); if (p.find(".so") == std::string::npos) continue; bool dup = false; for (auto& x : libs) if (x == p) { dup = true; break; } if (!dup) libs.push_back(p); } fclose(f); return libs;
}

bool TryJniCandidates() {
    const char* cands[] = { "Java_com_google_androidgamesdk_GameActivity_onTouchEvent", "Java_com_google_androidgamesdk_GameActivity_nativeOnTouchEvent", "Java_com_mojang_minecraftpe_MainActivity_nativeOnTouchEvent", "Java_com_mojang_minecraftpe_MainActivity_onTouchEvent" };
    for (const std::string& lib : LoadedLibs()) { void* h = dlopen(lib.c_str(), RTLD_NOLOAD | RTLD_NOW); if (!h) continue; for (const char* c : cands) { void* fn = dlsym(h, c); if (fn) { LOGI("Touch: found JNI symbol %s in %s", c, lib.c_str()); dlclose(h); return Attach(fn, OnJniTouchEnter, nullptr); } } dlclose(h); } return false;
}
}

namespace TouchHook {
float X() { return g_x.load(); }
float Y() { return g_y.load(); }
bool Down() { return g_down.load(); }
int Source() { return g_source.load(); }
bool Install() {
    // Official API first. If it is found we are done: the fallbacks below
    // hook functions the Java-driven launcher never calls, so they would only
    // look "installed" while delivering nothing.
    if (TryOfficialApi()) { LOGI("Touch source: official pl::input API"); return true; }

    // Official API missing: install every fallback (do NOT stop at the first
    // one that attaches, because attaching does not prove it ever fires).
    bool any = false;
    if (TryInputQueue()) { LOGI("Touch fallback: AInputQueue"); any = true; }
    if (TryJniCandidates()) { LOGI("Touch fallback: JNI touch entry"); any = true; }
    if (!any) LOGE("Touch: NO SOURCE FOUND");
    return any;
}
void Uninstall() {
    if (!g_intc) return;
    for (size_t i = 0; i < g_listeners.size(); i++) { gum_interceptor_detach(g_intc, g_listeners[i]); g_object_unref(g_listeners[i]); }
    g_listeners.clear(); g_targets.clear();
}
}
