#include "TouchHook.h"

#include <android/log.h>
#include <android/input.h>
#include <dlfcn.h>
#include <atomic>

#include <frida-gum.h>

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

std::atomic<float> g_x{0.0f};
std::atomic<float> g_y{0.0f};
std::atomic<bool> g_down{false};
std::atomic<int> g_source{0};

GumInterceptor* g_interceptor = nullptr;
GumInvocationListener* g_listener = nullptr;
void* g_target = nullptr;

// Called AFTER AInputQueue_getEvent returns, so the event pointer is valid
void OnGetEventLeave(GumInvocationContext* ic, gpointer user_data) {
    gint32 ret = (gint32)(gintptr)gum_invocation_context_get_return_value(ic);
    if (ret < 0) return;

    AInputEvent** ppEvent = (AInputEvent**)gum_invocation_context_get_nth_argument(ic, 1);
    if (!ppEvent || !*ppEvent) return;

    AInputEvent* event = *ppEvent;
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return;

    int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    float x = AMotionEvent_getX(event, 0);
    float y = AMotionEvent_getY(event, 0);

    bool down = (action == AMOTION_EVENT_ACTION_DOWN ||
                 action == AMOTION_EVENT_ACTION_MOVE ||
                 action == AMOTION_EVENT_ACTION_POINTER_DOWN);

    g_x.store(x);
    g_y.store(y);
    g_down.store(down);
    g_source.store(1);
}

}

namespace TouchHook {

float X() { return g_x.load(); }
float Y() { return g_y.load(); }
bool Down() { return g_down.load(); }
int Source() { return g_source.load(); }

bool Install() {
    gum_init_embedded();

    g_interceptor = gum_interceptor_obtain();
    if (!g_interceptor) {
        LOGE("TouchHook: failed to obtain interceptor");
        return false;
    }

    g_target = dlsym(RTLD_DEFAULT, "AInputQueue_getEvent");
    if (!g_target) {
        LOGE("TouchHook: AInputQueue_getEvent NOT FOUND - no touch source");
        return false;
    }

    LOGI("TouchHook: found AInputQueue_getEvent at %p", g_target);

    g_listener = gum_make_call_listener(nullptr, OnGetEventLeave, nullptr, nullptr);
    if (!g_listener) {
        LOGE("TouchHook: failed to create listener");
        return false;
    }

    gum_interceptor_begin_transaction(g_interceptor);
    GumAttachReturn result = gum_interceptor_attach(
        g_interceptor,
        g_target,
        g_listener,
        nullptr
    );
    gum_interceptor_end_transaction(g_interceptor);

    if (result != GUM_ATTACH_OK) {
        LOGE("TouchHook: attach failed: %d", result);
        return false;
    }

    LOGI("TouchHook: installed successfully");
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
    g_target = nullptr;
}

}
