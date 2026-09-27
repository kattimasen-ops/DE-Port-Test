// =============================================================================
// Dead Effect - Native Android-zu-Linux Wrapper
// Unity 2019.4.28f1 | IL2CPP | arm64-v8a
//
// WICHTIG: Android-Libs sind bionic-gelinkt und koennen nicht direkt
// gelinkt werden. Alle Android-Symbole werden hier selbst implementiert
// oder zur Laufzeit per dlopen() geladen.
// =============================================================================

#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <pthread.h>

// --- Android-Log-Level ---
#define ANDROID_LOG_INFO  4
#define ANDROID_LOG_ERROR 6

extern "C" int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tag ? tag : "?");
    int r = vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    return r;
}

extern "C" int __android_log_write(int prio, const char* tag, const char* text) {
    fprintf(stderr, "[%s] %s\n", tag ? tag : "?", text ? text : "");
    return 0;
}

extern "C" int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap) {
    fprintf(stderr, "[%s] ", tag ? tag : "?");
    int r = vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    return r;
}

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "DE-Wrapper", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "DE-Wrapper", __VA_ARGS__)

// --- JavaVM-Stub ---
struct JNIInvokeInterface_stub {
    void* reserved0;
    void* reserved1;
    void* reserved2;
    int (*DestroyJavaVM)(void*);
    int (*AttachCurrentThread)(void*, void**, void*);
    int (*DetachCurrentThread)(void*);
    int (*GetEnv)(void*, void**, int);
    int (*AttachCurrentThreadAsDaemon)(void*, void**, void*);
};

static int stub_DestroyJavaVM(void*) { return 0; }
static int stub_AttachCurrentThread(void*, void** env, void*) { if (env) *env = nullptr; return 0; }
static int stub_DetachCurrentThread(void*) { return 0; }
static int stub_GetEnv(void*, void** env, int) { if (env) *env = nullptr; return -2; }
static int stub_AttachCurrentThreadAsDaemon(void*, void**, void*) { return 0; }

static JNIInvokeInterface_stub g_jniInvoke = {
    nullptr, nullptr, nullptr,
    stub_DestroyJavaVM,
    stub_AttachCurrentThread,
    stub_DetachCurrentThread,
    stub_GetEnv,
    stub_AttachCurrentThreadAsDaemon
};

struct JavaVM_stub {
    JNIInvokeInterface_stub* functions;
};

static JavaVM_stub g_javaVM = { &g_jniInvoke };

typedef int (*JNI_OnLoad_t)(void* vm, void* reserved);
typedef int (*unity_main_t)(int argc, char** argv);

static void* load_library(const char* name) {
    void* handle = dlopen(name, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        LOGE("FEHLER beim Laden von %s: %s", name, dlerror());
    } else {
        LOGI("Geladen: %s", name);
    }
    return handle;
}

int main(int argc, char** argv) {
    LOGI("=== Dead Effect Wrapper gestartet ===");
    LOGI("Unity 2019.4.28f1 | IL2CPP | arm64-v8a");

    // libmain.so laden
    void* libmain = load_library("libmain.so");
    if (!libmain) {
        LOGE("libmain.so konnte nicht geladen werden!");
        return 1;
    }

    // JNI_OnLoad aufrufen
    JNI_OnLoad_t jni_onload = (JNI_OnLoad_t)dlsym(libmain, "JNI_OnLoad");
    if (jni_onload) {
        LOGI("JNI_OnLoad in libmain.so gefunden");
        jni_onload(&g_javaVM, nullptr);
    } else {
        LOGI("JNI_OnLoad nicht in libmain.so, versuche libunity.so...");
        void* libunity = load_library("libunity.so");
        if (libunity) {
            jni_onload = (JNI_OnLoad_t)dlsym(libunity, "JNI_OnLoad");
            if (jni_onload) {
                LOGI("JNI_OnLoad in libunity.so gefunden");
                jni_onload(&g_javaVM, nullptr);
            }
        }
    }

    // Unity main-Funktion finden
    unity_main_t unity_main = (unity_main_t)dlsym(libmain, "main");
    if (!unity_main) {
        LOGI("main nicht in libmain.so, versuche android_main...");
        unity_main = (unity_main_t)dlsym(libmain, "android_main");
    }
    if (!unity_main) {
        void* libunity = dlopen("libunity.so", RTLD_NOW | RTLD_GLOBAL);
        if (libunity) {
            unity_main = (unity_main_t)dlsym(libunity, "main");
            if (!unity_main) {
                unity_main = (unity_main_t)dlsym(libunity, "android_main");
            }
        }
    }

    if (!unity_main) {
        LOGE("Kein Unity-Einstiegspunkt gefunden!");
        return 1;
    }

    LOGI("Unity main gefunden, starte Spiel...");
    int result = unity_main(argc, argv);
    LOGI("Spiel beendet mit Code: %d", result);
    return result;
}
