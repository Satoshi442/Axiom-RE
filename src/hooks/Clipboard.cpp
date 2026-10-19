#include "Clipboard.h"

#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include <cstdio>
#include <mutex>
#include <string_view>

#include "imgui.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

std::mutex g_mutex;
std::string g_internal;        // last text copied inside Axiom
bool g_systemInSync = false;   // did our last Set reach the system clipboard?
char g_status[256] = "unused";

void SetStatus(const char* fmt, const char* a = "", const char* b = "") {
    std::lock_guard<std::mutex> lock(g_mutex);
    snprintf(g_status, sizeof(g_status), fmt, a, b);
    LOGI("Clipboard: %s", g_status);
}

// ------------------------------------------------------------------ JNI ----

// Turns a pending Java exception into text (and clears it).
std::string TakeException(JNIEnv* env) {
    jthrowable ex = env->ExceptionOccurred();
    if (!ex) return "";
    env->ExceptionClear();
    std::string out = "exception";
    jclass tc = env->FindClass("java/lang/Throwable");
    jmethodID ts = tc ? env->GetMethodID(tc, "toString", "()Ljava/lang/String;") : nullptr;
    if (ts) {
        jstring js = static_cast<jstring>(env->CallObjectMethod(ex, ts));
        if (js && !env->ExceptionCheck()) {
            const char* u = env->GetStringUTFChars(js, nullptr);
            if (u) { out = u; env->ReleaseStringUTFChars(js, u); }
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    return out;
}

// Runs fn(env, app, mgr). `err` receives which step failed.
template <typename Fn>
bool WithClipboard(std::string& err, Fn fn) {
    using GetVmsFn = jint (*)(JavaVM**, jsize, jsize*);
    auto getVms = reinterpret_cast<GetVmsFn>(dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs"));
    JavaVM* vm = nullptr;
    jsize n = 0;
    if (!getVms || getVms(&vm, 1, &n) != JNI_OK || !vm) { err = "no JavaVM"; return false; }

    JNIEnv* env = nullptr;
    bool detach = false;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) { err = "attach failed"; return false; }
        detach = true;
    }
    if (!env) { err = "no JNIEnv"; return false; }

    bool ok = false;
    do {
        jclass at = env->FindClass("android/app/ActivityThread");
        if (!at || env->ExceptionCheck()) { err = "ActivityThread: " + TakeException(env); break; }
        jmethodID curApp = env->GetStaticMethodID(at, "currentApplication", "()Landroid/app/Application;");
        if (!curApp || env->ExceptionCheck()) { err = "currentApplication missing: " + TakeException(env); break; }
        jobject app = env->CallStaticObjectMethod(at, curApp);
        if (!app || env->ExceptionCheck()) { err = "no Application: " + TakeException(env); break; }

        jclass ctx = env->FindClass("android/content/Context");
        jmethodID getSvc = env->GetMethodID(ctx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        if (!getSvc || env->ExceptionCheck()) { err = "getSystemService: " + TakeException(env); break; }
        jstring name = env->NewStringUTF("clipboard");
        jobject mgr = env->CallObjectMethod(app, getSvc, name);
        if (!mgr || env->ExceptionCheck()) { err = "no ClipboardManager: " + TakeException(env); break; }

        ok = fn(env, app, mgr, err);
    } while (false);

    if (env->ExceptionCheck()) env->ExceptionClear();
    if (detach) vm->DetachCurrentThread();
    return ok;
}

bool JniSet(const char* text, std::string& err) {
    return WithClipboard(err, [&](JNIEnv* env, jobject, jobject mgr, std::string& e) {
        jclass clipData = env->FindClass("android/content/ClipData");
        jmethodID newPlain = env->GetStaticMethodID(
            clipData, "newPlainText",
            "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;");
        if (!newPlain || env->ExceptionCheck()) { e = "newPlainText: " + TakeException(env); return false; }
        jstring label = env->NewStringUTF("Axiom-RE");
        jstring value = env->NewStringUTF(text);
        jobject clip = env->CallStaticObjectMethod(clipData, newPlain, label, value);
        if (!clip || env->ExceptionCheck()) { e = "ClipData: " + TakeException(env); return false; }
        jclass mgrCls = env->GetObjectClass(mgr);
        jmethodID setClip = env->GetMethodID(mgrCls, "setPrimaryClip", "(Landroid/content/ClipData;)V");
        if (!setClip || env->ExceptionCheck()) { e = "setPrimaryClip missing: " + TakeException(env); return false; }
        env->CallVoidMethod(mgr, setClip, clip);
        if (env->ExceptionCheck()) { e = "setPrimaryClip: " + TakeException(env); return false; }
        return true;
    });
}

bool JniGet(std::string& result, std::string& err) {
    return WithClipboard(err, [&](JNIEnv* env, jobject app, jobject mgr, std::string& e) {
        jclass mgrCls = env->GetObjectClass(mgr);
        jmethodID getClip = env->GetMethodID(mgrCls, "getPrimaryClip", "()Landroid/content/ClipData;");
        if (!getClip || env->ExceptionCheck()) { e = "getPrimaryClip missing: " + TakeException(env); return false; }
        jobject clip = env->CallObjectMethod(mgr, getClip);
        if (env->ExceptionCheck()) { e = "getPrimaryClip: " + TakeException(env); return false; }
        if (!clip) { e = "system clipboard empty"; return false; }

        jclass clipCls = env->GetObjectClass(clip);
        jmethodID count = env->GetMethodID(clipCls, "getItemCount", "()I");
        jmethodID itemAt = env->GetMethodID(clipCls, "getItemAt", "(I)Landroid/content/ClipData$Item;");
        if (!count || !itemAt || env->ExceptionCheck()) { e = "ClipData methods: " + TakeException(env); return false; }
        if (env->CallIntMethod(clip, count) < 1) { e = "no clip items"; return false; }

        jobject item = env->CallObjectMethod(clip, itemAt, 0);
        if (!item || env->ExceptionCheck()) { e = "getItemAt: " + TakeException(env); return false; }
        jclass itemCls = env->GetObjectClass(item);
        jmethodID coerce = env->GetMethodID(itemCls, "coerceToText", "(Landroid/content/Context;)Ljava/lang/CharSequence;");
        if (!coerce || env->ExceptionCheck()) { e = "coerceToText missing: " + TakeException(env); return false; }
        jobject seq = env->CallObjectMethod(item, coerce, app);
        if (!seq || env->ExceptionCheck()) { e = "coerceToText: " + TakeException(env); return false; }

        jclass seqCls = env->GetObjectClass(seq);
        jmethodID toStr = env->GetMethodID(seqCls, "toString", "()Ljava/lang/String;");
        if (!toStr || env->ExceptionCheck()) { e = "toString: " + TakeException(env); return false; }
        jstring js = static_cast<jstring>(env->CallObjectMethod(seq, toStr));
        if (!js || env->ExceptionCheck()) { e = "toString call: " + TakeException(env); return false; }
        const char* utf = env->GetStringUTFChars(js, nullptr);
        if (!utf) { e = "GetStringUTFChars failed"; return false; }
        result = utf;
        env->ReleaseStringUTFChars(js, utf);
        return true;
    });
}

// -------------------------------------------------------- preloader copy ----

// The preloader already exports pl::platform::setClipboardText, implemented
// with the launcher's real Activity. Look it up at runtime (no link-time
// dependency on libpreloader).
bool PreloaderSet(const char* text) {
    using SetFn = bool (*)(std::string_view);
    static SetFn fn = [] () -> SetFn {
        void* h = dlopen("libpreloader.so", RTLD_NOW | RTLD_NOLOAD);
        const char* names[] = {
            "_ZN2pl8platform16setClipboardTextENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEE",
            "_ZN2pl8platform16setClipboardTextENSt3__117basic_string_viewIcNS1_11char_traitsIcEEEE",
        };
        for (const char* n : names) {
            void* p = h ? dlsym(h, n) : nullptr;
            if (!p) p = dlsym(RTLD_DEFAULT, n);
            if (p) return reinterpret_cast<SetFn>(p);
        }
        return nullptr;
    }();
    return fn && fn(std::string_view(text));
}

const char* ImGuiGet(void*) {
    static std::string s;
    s = Clipboard::Get();
    return s.c_str();
}
void ImGuiSet(void*, const char* text) { Clipboard::Set(text); }

}  // namespace

namespace Clipboard {

bool Set(const char* text) {
    if (!text) return false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_internal = text;
    }

    bool ok = PreloaderSet(text);
    if (ok) {
        SetStatus("copy ok (preloader)");
    } else {
        std::string err;
        ok = JniSet(text, err);
        if (ok) SetStatus("copy ok (JNI)");
        else SetStatus("copy FAILED, app-only: %s", err.c_str());
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_systemInSync = ok;
    return ok;
}

std::string Get() {
    bool inSync;
    std::string internal;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        inSync = g_systemInSync;
        internal = g_internal;
    }

    // If our last copy never reached the system clipboard, the system text is
    // stale, so the in-app copy is the right answer.
    if (!inSync && !internal.empty()) {
        SetStatus("paste from app copy (system copy not available)");
        return internal;
    }

    std::string sys, err;
    if (JniGet(sys, err) && !sys.empty()) {
        SetStatus("paste ok (system)");
        return sys;
    }
    if (!internal.empty()) {
        SetStatus("paste from app copy (system read: %s)", err.c_str());
        return internal;
    }
    SetStatus("paste: nothing to paste (%s)", err.c_str());
    return "";
}

const char* Status() { return g_status; }

void InstallImGui() {
    ImGuiIO& io = ImGui::GetIO();
    io.SetClipboardTextFn = ImGuiSet;   // ImGui 1.90.x API (moved to ImGuiPlatformIO in 1.91)
    io.GetClipboardTextFn = ImGuiGet;
    LOGI("Clipboard: ImGui callbacks installed");
}

}  // namespace Clipboard
