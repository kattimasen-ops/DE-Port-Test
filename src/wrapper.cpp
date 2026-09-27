// =============================================================================
// Dead Effect - Native Android-zu-Linux Wrapper
// Unity 2019.4.28f1 | IL2CPP | arm64-v8a
//
// Dieser Wrapper laedt libmain.so (den Unity-Einstiegspunkt),
// der wiederum libunity.so und libil2cpp.so laedt.
// =============================================================================

#include <dlfcn.h>
#include <jni.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <android/log.h>

// --- Logging-Makro ---
#define LOG_TAG "DE-Wrapper"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// --- Unity JNI_OnLoad Signatur ---
typedef jint (*JNI_OnLoad_t)(JavaVM* vm, void* reserved);
typedef void (*JNI_OnUnload_t)(JavaVM* vm, void* reserved);

// --- Einstiegspunkt: Die main-Funktion in libmain.so ---
// Unity exportiert diese als "main" oder "android_main"
typedef int (*unity_main_t)(int argc, char** argv);

static JavaVM* g_javaVM = nullptr;

// =============================================================================
// Minimaler JNI-Stub (damit Unity denkt, es spricht mit Java)
// =============================================================================
static jint JNICALL stub_DestroyJavaVM(JavaVM* vm) { return JNI_OK; }
static jint JNICALL stub_AttachCurrentThread(JavaVM* vm, void** env, void* args) { return JNI_OK; }
static jint JNICALL stub_DetachCurrentThread(JavaVM* vm) { return JNI_OK; }
static jint JNICALL stub_GetEnv(JavaVM* vm, void** env, jint version) { 
    *env = nullptr; 
    return JNI_EDETACHED; 
}
static jint JNICALL stub_AttachCurrentThreadAsDaemon(JavaVM* vm, void** env, void* args) { return JNI_OK; }

static JNIInvokeInterface g_jniInvokeInterface = {
    nullptr,  // reserved0
    nullptr,  // reserved1
    nullptr,  // reserved2
    stub_DestroyJavaVM,
    stub_AttachCurrentThread,
    stub_DetachCurrentThread,
    stub_GetEnv,
    stub_AttachCurrentThreadAsDaemon
};

static JavaVM g_javaVMInstance = {
    &g_jniInvokeInterface
};

// =============================================================================
// Hilfsfunktion: Bibliothek laden mit Fehlerbehandlung
// =============================================================================
static void* load_library(const char* name) {
    void* handle = dlopen(name, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        LOGE("FEHLER beim Laden von %s: %s", name, dlerror());
        return nullptr;
    }
    LOGI("Geladen: %s", name);
    return handle;
}

// =============================================================================
// Hauptfunktion
// =============================================================================
int main(int argc, char** argv) {
    LOGI("=== Dead Effect Wrapper gestartet ===");
    LOGI("Unity 2019.4.28f1 | IL2CPP | arm64-v8a");

    // --- 1. libmain.so laden (Unity-Einstiegspunkt) ---
    // libmain.so oeffnet libunity.so und libil2cpp.so
    void* libmain = load_library("libmain.so");
    if (!libmain) {
        LOGE("libmain.so konnte nicht geladen werden!");
        return 1;
    }

    // --- 2. JNI_OnLoad aufrufen (falls exportiert) ---
    JNI_OnLoad_t jni_onload = (JNI_OnLoad_t)dlsym(libmain, "JNI_OnLoad");
    if (jni_onload) {
        LOGI("JNI_OnLoad gefunden, rufe auf...");
        jint result = jni_onload(&g_javaVMInstance, nullptr);
        LOGI("JNI_OnLoad Ergebnis: %d", result);
    } else {
        LOGI("JNI_OnLoad nicht in libmain.so gefunden, versuche libunity.so...");
        
        // Fallback: JNI_OnLoad direkt aus libunity.so
        void* libunity = load_library("libunity.so");
        if (libunity) {
            jni_onload = (JNI_OnLoad_t)dlsym(libunity, "JNI_OnLoad");
            if (jni_onload) {
                LOGI("JNI_OnLoad in libunity.so gefunden, rufe auf...");
                jni_onload(&g_javaVMInstance, nullptr);
            }
        }
    }

    // --- 3. Unity main-Funktion finden und aufrufen ---
    // Unity exportiert den Einstiegspunkt als "main" oder "android_main"
    unity_main_t unity_main = (unity_main_t)dlsym(libmain, "main");
    if (!unity_main) {
        LOGI("main nicht in libmain.so, versuche android_main...");
        unity_main = (unity_main_t)dlsym(libmain, "android_main");
    }
    if (!unity_main) {
        // Letzter Versuch: direkt in libunity.so
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
        // Debug: Alle Symbole auflisten
        void* sym = dlsym(libmain, "JNI_OnLoad");
        if (sym) LOGE("  JNI_OnLoad gefunden");
        return 1;
    }

    LOGI("Unity main gefunden, starte Spiel...");
    int result = unity_main(argc, argv);
    LOGI("Spiel beendet mit Code: %d", result);
    return result;
}
