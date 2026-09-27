// =============================================================================
// Dead Effect - Native Android-zu-Linux Wrapper
// Unity 2019.4.28f1 | IL2CPP | arm64-v8a
// Ziel: M9 Pro / R36S (RK3326, ArkOS, GLIBC 2.31)
//
// WICHTIG:
// Android-Bibliotheken sind gegen Bionic (Androids libc) gelinkt.
// Wir koennen sie nicht direkt gegen glibc linken. Stattdessen:
//   1. Alle benoetigten Android-Symbole werden hier implementiert
//      (insbesondere __android_log_*)
//   2. Die Android-.so-Dateien werden zur Laufzeit per dlopen()
//      aus ./lib/ geladen.
//   3. In spaeteren Iterationen wird mcpelauncher-linker das
//      bionic-kompatible dlopen bereitstellen.
// =============================================================================

#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <pthread.h>

// -----------------------------------------------------------------------------
// Android-Log-Level (aus <android/log.h>)
// -----------------------------------------------------------------------------
#ifndef ANDROID_LOG_INFO
#define ANDROID_LOG_INFO  4
#endif
#ifndef ANDROID_LOG_ERROR
#define ANDROID_LOG_ERROR 6
#endif

// -----------------------------------------------------------------------------
// Android-Log-Funktionen selbst implementieren
// (liblog.so ist bionic-gelinkt und kann nicht direkt gelinkt werden)
// -----------------------------------------------------------------------------
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

// Lokale Log-Makros
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "DE-Wrapper", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "DE-Wrapper", __VA_ARGS__)

// -----------------------------------------------------------------------------
// JavaVM-Stub (Unity ruft JNI_OnLoad auf und erwartet ein JavaVM-Objekt)
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Bibliothek laden mit Fehlerbehandlung
// -----------------------------------------------------------------------------
static void* load_library(const char* name) {
    void* handle = dlopen(name, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        LOGE("Fehler beim Laden von %s: %s", name, dlerror());
    } else {
        LOGI("Geladen: %s", name);
    }
    return handle;
}

// -----------------------------------------------------------------------------
// Hauptfunktion
// -----------------------------------------------------------------------------
int main(int argc, char** argv) {
    LOGI("=== Dead Effect Wrapper gestartet ===");
    LOGI("Unity 2019.4.28f1 | IL2CPP | arm64-v8a");
    LOGI("Ziel: M9 Pro / R36S (RK3326)");

    // --- 1. libmain.so laden (Unity-Einstiegspunkt) ---
    // libmain.so oeffnet libunity.so und libil2cpp.so
    void* libmain = load_library("libmain.so");
    if (!libmain) {
        LOGE("libmain.so konnte nicht geladen werden!");
        LOGE("Stelle sicher, dass die .so-Dateien in ./lib/ liegen");
        return 1;
    }

    // --- 2. JNI_OnLoad aufrufen (falls in libmain.so) ---
    typedef int (*JNI_OnLoad_t)(void* vm, void* reserved);
    JNI_OnLoad_t jni_onload = (JNI_OnLoad_t)dlsym(libmain, "JNI_OnLoad");
    if (jni_onload) {
        LOGI("JNI_OnLoad in libmain.so gefunden, rufe auf...");
        jni_onload(&g_javaVM, nullptr);
    } else {
        // Fallback: JNI_OnLoad in libunity.so suchen
        LOGI("JNI_OnLoad nicht in libmain.so, versuche libunity.so...");
        void* libunity = load_library("libunity.so");
        if (libunity) {
            jni_onload = (JNI_OnLoad_t)dlsym(libunity, "JNI_OnLoad");
            if (jni_onload) {
                LOGI("JNI_OnLoad in libunity.so gefunden, rufe auf...");
                jni_onload(&g_javaVM, nullptr);
            }
        }
    }

    // --- 3. Unity main-Funktion finden und aufrufen ---
    typedef int (*unity_main_t)(int argc, char** argv);

    unity_main_t unity_main = (unity_main_t)dlsym(libmain, "main");
    if (!unity_main) {
        LOGI("main nicht in libmain.so, versuche android_main...");
        unity_main = (unity_main_t)dlsym(libmain, "android_main");
    }

    if (!unity_main) {
        // Letzter Versuch: in libunity.so
        LOGI("main/android_main nicht in libmain.so, versuche libunity.so...");
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
        LOGE("Verfuegbare Symbole in libmain.so:");
        // Debug: Liste exportierte Symbole auf
        // (nur zur Diagnose)
        return 1;
    }

    LOGI("Unity main gefunden, starte Spiel...");
    int result = unity_main(argc, argv);
    LOGI("Spiel beendet mit Code: %d", result);
    return result;
}
