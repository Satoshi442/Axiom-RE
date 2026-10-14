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

std::atomic<float> g_rawX{0.0f};
std::atomic<float> g_rawY{0.0f};
std::atomic<bool> g_down{false};
std::atomic<int> g_source{0};

std::atomic<float> g_dw{1080.0f};
std::atomic<float> g_dh{1920.0f};
std::atomic<int> g_rot{-1};   // -1 = auto

GumInterceptor* g_intc = nullptr;
std::vector<GumInvocationListener*> g_listeners;
std::vector<void*> g_targets;

void SetTouch(float x, float y, bool down, int src) {
    g_rawX.store(x);
    g_rawY.store(y);
    g_down.store(down);
    g_source.store(src);
}

void Transform(float vx, float vy, float& ox, float& oy) {
    float W = g_dw.load();
    float H = g_dh.load();
    int m = g_rot.load();
    if (m < 0) {
        // auto: identity if already in bounds, else assume 90° rotation
        m = (vx <= W && vy <= H) ? 0 : 2;
    }
    switch (m) {
        case 1:  ox = vy;      oy = vx;      break;
        case 2:  ox = W - vy;  oy = vx;      break;
        case 3:  ox = vy;      oy = H - vx;  break;
        default: ox = vx;      oy = vy;      break;
    }
}

bool MotionDown(int32_t maskedAction) {
    return maskedAction == AMOTION_EVENT_ACTION_DOWN ||
           maskedAction == AMOTION_EVENT_ACTION_MOVE ||
           maskedAction == AMOTION_EVENT_ACTION_POINTER_DOWN;
}

bool Attach(void* target, GumInvocationCallback enter, GumInvocationCallback leave) {
    if (!target) return false;
    if (!g_intc) {
        gum_init_embedded();
        g_intc = gum_interceptor_obtain();
    }
    if (!g_intc) return false;

    GumInvocationListener* listener = gum_make_call_listener(enter, leave, nullptr, nullptr);
    if (!listener) return false;

    gum_interceptor_begin_transaction(g_intc);
    GumAttachReturn r = gum_interceptor_attach(g_intc, target, listener, nullptr);
    gum_interceptor_end_transaction(g_intc);

    if (r != GUM_ATTACH_OK) {
        g_object_unref(listener);
        return false;
    }
    g_listeners.push_back(listener);
    g_targets.push_back(target);
    return true;
}

// ---------- Strategy 1: official pl::input API ----------
bool TryOfficialApi() {
    const char* names[] = {
        "_ZN2pl5input21registerTouchCallbackENSt6__ndk18functionIFbRKNS0_10TouchEventEEE",
        "_ZN2pl5input21registerTouchCallbackENSt3__18functionIFbRKNS0_10TouchEventEEE",
    };
    void* fn = nullptr;
    for (const char* n : names) {
        fn = dlsym(RTLD_DEFAULT, n);
        if (fn) { LOGI("Touch: found official symbol %s", n); break; }
    }
    if (!fn) return false;

    using RegFn = void (*)(pl::input::TouchCallback);
    auto reg = reinterpret_cast<RegFn>(fn);
    reg([](const pl::input::TouchEvent& e) -> bool {
        SetTouch(e.x, e.y, (e.action == 0 || e.action == 2), 1);
        return false;
    });
    return true;
}

// ---------- Strategy 2: AInputQueue_getEvent ----------
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
    LOGI("Touch: hooked AInputQueue_getEvent");
    return true;
}

// ---------- Strategy 3: JNI touch entry ----------
void OnJniTouchEnter(GumInvocationContext* ic, gpointer) {
    AInputEvent* ev = (AInputEvent*)gum_invocation_context_get_nth_argument(ic, 2);
    if (!ev) return;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return;
    int32_t a = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
    SetTouch(AMotionEvent_getX(ev, 0), AMotionEvent_getY(ev, 0), MotionDown(a), 3);
}

std::vector<std::string> LoadedLibs() {
    std::vector<std::string> libs;
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return libs;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* start = strchr(line, '/');
        if (!start) continue;
        size_t len = strlen(start);
        while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == ' ')) len--;
        std::string p(start, len);
        if (p.find(".so") == std::string::npos) continue;
        bool dup = false;
        for (auto& x : libs) if (x == p) { dup = true; break; }
        if (!dup) libs.push_back(p);
    }
    fclose(f);
    return libs;
}

bool TryJniCandidates() {
    const char* cands[] = {
        "Java_com_google_androidgamesdk_GameActivity_onTouchEvent",
        "Java_com_google_androidgamesdk_GameActivity_nativeOnTouchEvent",
        "Java_com_mojang_minecraftpe_MainActivity_nativeOnTouchEvent",
        "Java_com_mojang_minecraftpe_MainActivity_onTouchEvent",
    };
    for (const std::string& lib : LoadedLibs()) {
        void* h = dlopen(lib.c_str(), RTLD_NOLOAD | RTLD_NOW);
        if (!h) continue;
        for (const char* c : cands) {
            void* fn = dlsym(h, c);
            if (fn) {
                LOGI("Touch: found JNI symbol %s in %s", c, lib.c_str());
                dlclose(h);
                return Attach(fn, OnJniTouchEnter, nullptr);
            }
        }
        dlclose(h);
    }
    return false;
}

}

namespace TouchHook {

float RawX() { return g_rawX.load(); }
float RawY() { return g_rawY.load(); }
bool Down() { return g_down.load(); }
int Source() { return g_source.load(); }

float X() { float x, y; Transform(g_rawX.load(), g_rawY.load(), x, y); return x; }
float Y() { float x, y; Transform(g_rawX.load(), g_rawY.load(), x, y); return y; }

void SetDisplaySize(float w, float h) { g_dw.store(w); g_dh.store(h); }
void SetRotMode(int mode) { g_rot.store(mode); }
int RotMode() { return g_rot.load(); }

bool Install() {
    if (TryOfficialApi())   { LOGI("Touch source: official pl::input API"); return true; }
    if (TryInputQueue())    { LOGI("Touch source: AInputQueue");            return true; }
    if (TryJniCandidates()) { LOGI("Touch source: JNI touch entry");        return true; }
    LOGE("Touch: NO SOURCE FOUND");
    return false;
}

void Uninstall() {
    if (!g_intc) return;
    for (size_t i = 0; i < g_listeners.size(); i++) {
        gum_interceptor_detach(g_intc, g_listeners[i]);
        g_object_unref(g_listeners[i]);
    }
    g_listeners.clear();
    g_targets.clear();
}

}
