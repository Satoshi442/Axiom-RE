#include <jni.h>
#include <string>
#include <imgui.h>

// These should be populated when your mod loads (e.g. in AxiomRE::load)
extern JavaVM* g_jvm;
extern jobject g_activity; // Must be a GlobalRef!

std::string GetClipboardTextJNI() {
    if (!g_jvm || !g_activity) return "";

    JNIEnv* env = nullptr;
    bool isAttached = false;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return "";
        isAttached = true;
    }

    std::string result = "";
    jclass contextClass = env->FindClass("android/content/Context");
    jmethodID getSystemService = env->GetMethodID(contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring clipStr = env->NewStringUTF("clipboard");
    jobject clipboardManager = env->CallObjectMethod(g_activity, getSystemService, clipStr);

    if (clipboardManager) {
        jclass cmClass = env->FindClass("android/content/ClipboardManager");
        jmethodID getText = env->GetMethodID(cmClass, "getText", "()Ljava/lang/CharSequence;");
        jobject charSeq = env->CallObjectMethod(clipboardManager, getText);

        if (charSeq) {
            jclass csClass = env->FindClass("java/lang/CharSequence");
            jmethodID toString = env->GetMethodID(csClass, "toString", "()Ljava/lang/String;");
            jstring strObj = (jstring)env->CallObjectMethod(charSeq, toString);

            if (strObj) {
                const char* utf8 = env->GetStringUTFChars(strObj, nullptr);
                result = utf8;
                env->ReleaseStringUTFChars(strObj, utf8);
                env->DeleteLocalRef(strObj);
            }
            env->DeleteLocalRef(charSeq);
        }
        env->DeleteLocalRef(clipboardManager);
    }

    env->DeleteLocalRef(clipStr);
    if (isAttached) g_jvm->DetachCurrentThread();
    return result;
}

void SetClipboardTextJNI(const std::string& text) {
    if (!g_jvm || !g_activity) return;
    JNIEnv* env = nullptr;
    bool isAttached = false;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        isAttached = true;
    }

    jclass contextClass = env->FindClass("android/content/Context");
    jmethodID getSystemService = env->GetMethodID(contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring clipStr = env->NewStringUTF("clipboard");
    jobject clipboardManager = env->CallObjectMethod(g_activity, getSystemService, clipStr);

    if (clipboardManager) {
        jclass cmClass = env->FindClass("android/content/ClipboardManager");
        jmethodID setText = env->GetMethodID(cmClass, "setText", "(Ljava/lang/CharSequence;)V");
        jstring textToCopy = env->NewStringUTF(text.c_str());
        env->CallVoidMethod(clipboardManager, setText, textToCopy);
        env->DeleteLocalRef(textToCopy);
        env->DeleteLocalRef(clipboardManager);
    }
    env->DeleteLocalRef(clipStr);
    if (isAttached) g_jvm->DetachCurrentThread();
}
