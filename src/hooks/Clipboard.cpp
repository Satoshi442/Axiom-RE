#include "Clipboard.h"
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

JNIEnv* GetEnv(bool* detach) {
    JavaVM* vm = nullptr;
    jsize n = 0;
    using GetVmsFn = jint (*)(JavaVM**, jsize, jsize*);
    auto getVms = (GetVmsFn)dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
    if (!getVms || getVms(&vm, 1, &n) != JNI_OK || !vm) return nullptr;

    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
        *detach = true;
    }
    return env;
}

jobject GetApplicationContext(JNIEnv* env) {
    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (!atClass || env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    jmethodID curApp = env->GetStaticMethodID(atClass, "currentApplication", "()Landroid/app/Application;");
    if (!curApp || env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    return env->CallStaticObjectMethod(atClass, curApp);
}

}

namespace Clipboard {

std::string GetText() {
    bool detach = false;
    JNIEnv* env = GetEnv(&detach);
    if (!env) return "";

    std::string result = "";
    jobject app = GetApplicationContext(env);
    if (app) {
        jclass contextClass = env->FindClass("android/content/Context");
        jmethodID getSystemService = env->GetMethodID(contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring clipString = env->NewStringUTF("clipboard");
        jobject clipboardManager = env->CallObjectMethod(app, getSystemService, clipString);

        if (clipboardManager) {
            jclass cmClass = env->FindClass("android/content/ClipboardManager");
            jmethodID getPrimaryClip = env->GetMethodID(cmClass, "getPrimaryClip", "()Landroid/content/ClipData;");
            jobject clipData = env->CallObjectMethod(clipboardManager, getPrimaryClip);

            if (clipData) {
                jclass cdClass = env->FindClass("android/content/ClipData");
                jmethodID getItemCount = env->GetMethodID(cdClass, "getItemCount", "()I");
                jint count = env->CallIntMethod(clipData, getItemCount);

                if (count > 0) {
                    jmethodID getItemAt = env->GetMethodID(cdClass, "getItemAt", "(I)Landroid/content/ClipData$Item;");
                    jobject item = env->CallObjectMethod(clipData, getItemAt, 0);

                    jclass itemClass = env->FindClass("android/content/ClipData$Item");
                    jmethodID getText = env->GetMethodID(itemClass, "getText", "()Ljava/lang/CharSequence;");
                    jobject charSeq = env->CallObjectMethod(item, getText);

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
                    env->DeleteLocalRef(item);
                }
                env->DeleteLocalRef(clipData);
            }
            env->DeleteLocalRef(clipboardManager);
        }
        env->DeleteLocalRef(clipString);
        env->DeleteLocalRef(app);
    }

    if (detach) {
        JavaVM* vm = nullptr;
        env->GetJavaVM(&vm);
        if (vm) vm->DetachCurrentThread();
    }
    return result;
}

void SetText(const std::string& text) {
    bool detach = false;
    JNIEnv* env = GetEnv(&detach);
    if (!env) return;

    jobject app = GetApplicationContext(env);
    if (app) {
        jclass contextClass = env->FindClass("android/content/Context");
        jmethodID getSystemService = env->GetMethodID(contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring clipString = env->NewStringUTF("clipboard");
        jobject clipboardManager = env->CallObjectMethod(app, getSystemService, clipString);

        if (clipboardManager) {
            jclass cmClass = env->FindClass("android/content/ClipboardManager");
            jmethodID setPrimaryClip = env->GetMethodID(cmClass, "setPrimaryClip", "(Landroid/content/ClipData;)V");
            
            jclass cdClass = env->FindClass("android/content/ClipData");
            jmethodID newPlainText = env->GetStaticMethodID(cdClass, "newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;");
            jstring label = env->NewStringUTF("Axiom-RE");
            jstring textObj = env->NewStringUTF(text.c_str());
            
            jobject clipData = env->CallStaticObjectMethod(cdClass, newPlainText, label, textObj);
            if (clipData) {
                env->CallVoidMethod(clipboardManager, setPrimaryClip, clipData);
                env->DeleteLocalRef(clipData);
            }
            env->DeleteLocalRef(label);
            env->DeleteLocalRef(textObj);
            env->DeleteLocalRef(clipboardManager);
        }
        env->DeleteLocalRef(clipString);
        env->DeleteLocalRef(app);
    }

    if (detach) {
        JavaVM* vm = nullptr;
        env->GetJavaVM(&vm);
        if (vm) vm->DetachCurrentThread();
    }
}

}
