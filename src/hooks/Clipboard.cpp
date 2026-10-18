#include "Clipboard.h"

#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include <mutex>

#include "imgui.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

std::mutex g_mutex;
std::string g_internal;  // last text copied inside Axiom

// Attaches this thread to the JVM, finds the Application + ClipboardManager,
// runs fn(env, app, manager), then cleans up. Returns false if any step fails.
template <typename Fn>
bool WithClipboard(Fn fn) {
    using GetVmsFn = jint (*)(JavaVM**, jsize, jsize*);
    auto getVms = reinterpret_cast<GetVmsFn>(dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs"));
    JavaVM* vm = nullptr;
    jsize n = 0;
    if (!getVms || getVms(&vm, 1, &n) != JNI_OK || !vm) return false;

    JNIEnv* env = nullptr;
    bool detach = false;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        detach = true;
    }
    if (!env) return false;

    bool ok = false;
    do {
        jclass at = env->FindClass("android/app/ActivityThread");
        if (!at || env->ExceptionCheck()) break;
        jmethodID curApp = env->GetStaticMethodID(at, "currentApplication", "()Landroid/app/Application;");
        if (!curApp || env->ExceptionCheck()) break;
        jobject app = env->CallStaticObjectMethod(at, curApp);
        if (!app || env->ExceptionCheck()) break;

        jclass ctx = env->FindClass("android/content/Context");
        jmethodID getSvc = env->GetMethodID(ctx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        if (!getSvc || env->ExceptionCheck()) break;
        jstring name = env->NewStringUTF("clipboard");
        jobject mgr = env->CallObjectMethod(app, getSvc, name);
        if (!mgr || env->ExceptionCheck()) break;

        ok = fn(env, app, mgr);
    } while (false);

    if (env->ExceptionCheck()) env->ExceptionClear();
    if (detach) vm->DetachCurrentThread();
    return ok;
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
    const bool ok = WithClipboard([&](JNIEnv* env, jobject, jobject mgr) {
        jclass clipData = env->FindClass("android/content/ClipData");
        jmethodID newPlain = env->GetStaticMethodID(
            clipData, "newPlainText",
            "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;");
        if (!newPlain || env->ExceptionCheck()) return false;

        jstring label = env->NewStringUTF("Axiom-RE");
        jstring value = env->NewStringUTF(text);
        jobject clip = env->CallStaticObjectMethod(clipData, newPlain, label, value);
        if (!clip || env->ExceptionCheck()) return false;

        jclass mgrCls = env->GetObjectClass(mgr);
        jmethodID setClip = env->GetMethodID(mgrCls, "setPrimaryClip", "(Landroid/content/ClipData;)V");
        if (!setClip || env->ExceptionCheck()) return false;
        env->CallVoidMethod(mgr, setClip, clip);
        return !env->ExceptionCheck();
    });
    if (!ok) LOGE("Clipboard: system copy failed, using internal clipboard only");
    return ok;
}

std::string Get() {
    std::string result;
    WithClipboard([&](JNIEnv* env, jobject app, jobject mgr) {
        jclass mgrCls = env->GetObjectClass(mgr);
        jmethodID getClip = env->GetMethodID(mgrCls, "getPrimaryClip", "()Landroid/content/ClipData;");
        if (!getClip || env->ExceptionCheck()) return false;
        jobject clip = env->CallObjectMethod(mgr, getClip);
        if (!clip || env->ExceptionCheck()) return false;  // empty clipboard

        jclass clipCls = env->GetObjectClass(clip);
        jmethodID count = env->GetMethodID(clipCls, "getItemCount", "()I");
        jmethodID itemAt = env->GetMethodID(clipCls, "getItemAt", "(I)Landroid/content/ClipData$Item;");
        if (!count || !itemAt || env->ExceptionCheck()) return false;
        if (env->CallIntMethod(clip, count) < 1) return false;

        jobject item = env->CallObjectMethod(clip, itemAt, 0);
        if (!item || env->ExceptionCheck()) return false;
        jclass itemCls = env->GetObjectClass(item);
        jmethodID coerce = env->GetMethodID(itemCls, "coerceToText",
                                            "(Landroid/content/Context;)Ljava/lang/CharSequence;");
        if (!coerce || env->ExceptionCheck()) return false;
        jobject seq = env->CallObjectMethod(item, coerce, app);
        if (!seq || env->ExceptionCheck()) return false;

        jclass seqCls = env->GetObjectClass(seq);
        jmethodID toStr = env->GetMethodID(seqCls, "toString", "()Ljava/lang/String;");
        if (!toStr || env->ExceptionCheck()) return false;
        jstring js = static_cast<jstring>(env->CallObjectMethod(seq, toStr));
        if (!js || env->ExceptionCheck()) return false;

        const char* utf = env->GetStringUTFChars(js, nullptr);
        if (utf) {
            result = utf;
            env->ReleaseStringUTFChars(js, utf);
        }
        return true;
    });

    if (!result.empty()) return result;
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_internal;  // system clipboard unavailable or empty
}

void InstallImGui() {
    ImGuiIO& io = ImGui::GetIO();
    io.SetClipboardTextFn = ImGuiSet;   // ImGui 1.90.x API (moved to ImGuiPlatformIO in 1.91)
    io.GetClipboardTextFn = ImGuiGet;
    LOGI("Clipboard: ImGui callbacks installed");
}

}  // namespace Clipboard
