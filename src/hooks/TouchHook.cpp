#include "TouchHook.h"
#include <pl/Input.hpp>
#include <android/log.h>
#include <atomic>
#include "Keyboard.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {
std::atomic<float> g_x{0.0f};
std::atomic<float> g_y{0.0f};
std::atomic<bool> g_down{false};

bool TouchCallback(const pl::input::TouchEvent& event) {
    g_x.store(event.x);
    g_y.store(event.y);
    // Android Action codes: 0 = DOWN, 1 = UP, 2 = MOVE
    if (event.action == 0 || event.action == 2) {
        g_down.store(true);
    } else if (event.action == 1) {
        g_down.store(false);
    }
    return false; // false = don't block the game from receiving the touch
}
reg([](const pl::input::TouchEvent& e) -> bool {
        Keyboard::PumpMainThreadJobs();     // <-- ADD THIS LINE
        SetTouch(e.x, e.y, (e.action == 0 || e.action == 2), 1);
        return false;
    });
}

namespace TouchHook {
float X() { return g_x.load(); }
float Y() { return g_y.load(); }
bool Down() { return g_down.load(); }
int Source() { return 1; } 

bool Install() {
    LOGI("Registering touch callback via pl::input...");
    pl::input::registerTouchCallback(TouchCallback);
    LOGI("Touch callback registered successfully!");
    return true;
}

void Uninstall() {}
}
