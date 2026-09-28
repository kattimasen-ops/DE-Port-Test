#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <android/log.h>
#include "jni_shim.h"

#define TAG "deadeffect-jni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ============================================================
 * EGL-Kontext zwischenspeichern
 * ============================================================ */
static EGLDisplay g_dpy  = EGL_NO_DISPLAY;
static EGLSurface g_surf = EGL_NO_SURFACE;
static EGLContext g_ctx  = EGL_NO_CONTEXT;

void jni_shim_init(void) {
    LOGI("JNI-Shim initialisiert");
    /* Wird spaeter durch jni_shim_set_egl ergaenzt */
}

void jni_shim_set_egl(EGLDisplay d, EGLSurface s, EGLContext c) {
    g_dpy = d; g_surf = s; g_ctx = c;
    LOGI("JNI-Shim EGL: dpy=%p surf=%p ctx=%p", d, s, c);
}

void jni_shim_handle_sdl_event(SDL_Event *ev) { (void)ev; }

/* ============================================================
 * Fake JNIEnv + JavaVM
 *
 * Ein JNIEnv ist ein Zeiger auf ein Struct, dessen erstes Feld
 * ein Zeiger auf eine Function-Table ist. Wir bauen beides statisch
 * auf und fuellen alle Slots mit einem Default-Stub, bevor wir
 * die wichtigen Slots mit echten Implementierungen ueberschreiben.
 * ============================================================ */

/* Default-Stub: gibt 0 zurueck. Fuer alle Slots die wir nicht
 * brauchen. Unsauber vom Typprofil her, aber funktioniert auf ARM. */
static uintptr_t jni_default_stub(void) { return 0; }

/* Spezielle Stubs die etwas anderes brauchen */
static void jni_void_stub(void) { }

/* Wir nehmen eine ueberdimensionierte Tabelle, damit auch bei
 * einer abweichenden JNI-Header-Version nichts daneben geht. */
#define JNI_TABLE_SIZE 512
#define JNI_VM_TABLE_SIZE 16

/* JavaVM */
typedef struct {
    void *functions;
} jni_vm_t;

static uintptr_t     g_vm_table[JNI_VM_TABLE_SIZE];
static jni_vm_t      g_vm_singleton;

/* JNIEnv */
typedef struct {
    void *functions;
} jni_env_t;

static uintptr_t     g_env_table[JNI_TABLE_SIZE];
static jni_env_t     g_env_singleton;

/* ============================================================
 * Konkrete JNI-Implementierungen
 * ============================================================ */

/* --- JavaVM-Funktionen --- */

static int jni_vm_GetEnv(void *vm, void **penv, int version) {
    (void)vm; (void)version;
    if (penv) *penv = &g_env_singleton;
    LOGI("JNI VM GetEnv -> %p (version 0x%x)", &g_env_singleton, version);
    return 0;  /* JNI_OK */
}

static int jni_vm_AttachCurrentThread(void *vm, void **penv, void *args) {
    (void)vm; (void)args;
    if (penv) *penv = &g_env_singleton;
    LOGI("JNI VM AttachCurrentThread");
    return 0;
}

static int jni_vm_DetachCurrentThread(void *vm) {
    (void)vm;
    return 0;
}

static int jni_vm_DestroyJavaVM(void *vm) {
    (void)vm;
    return 0;
}

/* --- JNIEnv-Funktionen --- */

static int jni_env_GetVersion(void *env) {
    (void)env;
    return 0x00010006;  /* JNI_VERSION_1_6 */
}

static void *jni_env_FindClass(void *env, const char *name) {
    (void)env;
    LOGI("JNI FindClass(%s)", name ? name : "?");
    /* Fake-Klasse: Zeiger auf statisches Objekt ungleich NULL */
    static int fake_class = 1;
    return &fake_class;
}

static void *jni_env_GetObjectClass(void *env, void *obj) {
    (void)env; (void)obj;
    static int fake_class = 1;
    return &fake_class;
}

static void *jni_env_GetMethodID(void *env, void *cls,
                                 const char *name, const char *sig) {
    (void)env; (void)cls;
    LOGI("JNI GetMethodID(%s, %s)", name ? name : "?", sig ? sig : "?");
    static int fake_mid = 1;
    return &fake_mid;
}

static void *jni_env_GetStaticMethodID(void *env, void *cls,
                                       const char *name, const char *sig) {
    (void)env; (void)cls;
    LOGI("JNI GetStaticMethodID(%s, %s)", name ? name : "?", sig ? sig : "?");
    static int fake_mid = 1;
    return &fake_mid;
}

static void *jni_env_GetFieldID(void *env, void *cls,
                                const char *name, const char *sig) {
    (void)env; (void)cls;
    LOGI("JNI GetFieldID(%s, %s)", name ? name : "?", sig ? sig : "?");
    static int fake_fid = 1;
    return &fake_fid;
}

static void *jni_env_GetStaticFieldID(void *env, void *cls,
                                      const char *name, const char *sig) {
    (void)env; (void)cls;
    LOGI("JNI GetStaticFieldID(%s, %s)", name ? name : "?", sig ? sig : "?");
    static int fake_fid = 1;
    return &fake_fid;
}

static int jni_env_RegisterNatives(void *env, void *cls,
                                   const void *methods, int n) {
    (void)env; (void)cls; (void)methods;
    LOGI("JNI RegisterNatives(count=%d)", n);
    return 0;  /* Erfolg */
}

static int jni_env_UnregisterNatives(void *env, void *cls) {
    (void)env; (void)cls;
    return 0;
}

static void *jni_env_NewGlobalRef(void *env, void *obj) {
    (void)env;
    return obj;  /* Zeiger unveraendert weiterreichen */
}

static void *jni_env_NewLocalRef(void *env, void *obj) {
    (void)env;
    return obj;
}

static void jni_env_DeleteGlobalRef(void *env, void *obj) { (void)env; (void)obj; }
static void jni_env_DeleteLocalRef(void *env, void *obj)  { (void)env; (void)obj; }

static int jni_env_IsSameObject(void *env, void *a, void *b) {
    (void)env;
    return (a == b) ? 1 : 0;  /* JNI_TRUE / JNI_FALSE */
}

static void *jni_env_NewStringUTF(void *env, const char *utf) {
    (void)env;
    LOGI("JNI NewStringUTF(%s)", utf ? utf : "(null)");
    /* Wir verpacken den Zeiger auf den String als "jstring".
     * GetStringUTFChars liefert ihn dann unveraendert zurueck. */
    return (void *)utf;
}

static const char *jni_env_GetStringUTFChars(void *env, void *jstr, unsigned char *isCopy) {
    (void)env;
    if (isCopy) *isCopy = 0;
    return (const char *)jstr;
}

static void jni_env_ReleaseStringUTFChars(void *env, void *jstr, const char *chars) {
    (void)env; (void)jstr; (void)chars;
}

static int jni_env_GetStringUTFLength(void *env, void *jstr) {
    (void)env;
    if (!jstr) return 0;
    return (int)strlen((const char *)jstr);
}

static int jni_env_GetStringLength(void *env, void *jstr) {
    (void)env;
    if (!jstr) return 0;
    return (int)strlen((const char *)jstr);
}

static void *jni_env_ExceptionOccurred(void *env) {
    (void)env;
    return NULL;  /* Keine Exception */
}

static void jni_env_ExceptionDescribe(void *env) { (void)env; }
static void jni_env_ExceptionClear(void *env)    { (void)env; }
static int  jni_env_ExceptionCheck(void *env)    { (void)env; return 0; }

static int jni_env_GetJavaVM(void *env, void **pvm) {
    (void)env;
    if (pvm) *pvm = &g_vm_singleton;
    return 0;
}

static void *jni_env_NewObject(void *env, void *cls, void *mid, ...) {
    (void)env; (void)cls; (void)mid;
    static int fake_obj = 1;
    return &fake_obj;
}

static void *jni_env_AllocObject(void *env, void *cls) {
    (void)env; (void)cls;
    static int fake_obj = 1;
    return &fake_obj;
}

/* CallXxxMethod-Serie: alle geben 0 zurueck.
 * Wir brauchen nur die haeufigsten Signaturen. */
static uintptr_t jni_env_CallObjectMethod(void *env, void *obj, void *mid, ...) {
    (void)env; (void)obj; (void)mid;
    return 0;
}
static uintptr_t jni_env_CallIntMethod(void *env, void *obj, void *mid, ...) {
    (void)env; (void)obj; (void)mid;
    return 0;
}
static uintptr_t jni_env_CallBooleanMethod(void *env, void *obj, void *mid, ...) {
    (void)env; (void)obj; (void)mid;
    return 0;
}
static uintptr_t jni_env_CallVoidMethod(void *env, void *obj, void *mid, ...) {
    (void)env; (void)obj; (void)mid;
    return 0;
}
static uintptr_t jni_env_CallStaticObjectMethod(void *env, void *cls, void *mid, ...) {
    (void)env; (void)cls; (void)mid;
    return 0;
}
static uintptr_t jni_env_CallStaticVoidMethod(void *env, void *cls, void *mid, ...) {
    (void)env; (void)cls; (void)mid;
    return 0;
}
static uintptr_t jni_env_CallStaticIntMethod(void *env, void *cls, void *mid, ...) {
    (void)env; (void)cls; (void)mid;
    return 0;
}

/* Arrays */
static void *jni_env_NewByteArray(void *env, int len) {
    (void)env; (void)len;
    static int fake_arr = 1;
    return &fake_arr;
}
static int jni_env_GetArrayLength(void *env, void *arr) {
    (void)env; (void)arr;
    return 0;
}

/* ============================================================
 * Tabellen initialisieren
 * ============================================================ */

/* JNIEnv-Indices (Standard JNI-Spec, Android NDK jni.h) */
#define ENV_GetVersion              4
#define ENV_DefineClass             5
#define ENV_FindClass               6
#define ENV_GetSuperclass          10
#define ENV_IsAssignableFrom       11
#define ENV_Throw                  13
#define ENV_ThrowNew               14
#define ENV_ExceptionOccurred      15
#define ENV_ExceptionDescribe      16
#define ENV_ExceptionClear         17
#define ENV_PushLocalFrame         19
#define ENV_PopLocalFrame          20
#define ENV_NewGlobalRef           21
#define ENV_DeleteGlobalRef        22
#define ENV_DeleteLocalRef         23
#define ENV_IsSameObject           24
#define ENV_NewLocalRef            25
#define ENV_AllocObject            27
#define ENV_NewObject              28
#define ENV_GetObjectClass         31
#define ENV_IsInstanceOf           32
#define ENV_GetMethodID            33
#define ENV_CallObjectMethod       34
#define ENV_CallBooleanMethod      37
#define ENV_CallIntMethod          49
#define ENV_CallVoidMethod         61
#define ENV_GetFieldID             94
#define ENV_GetStaticMethodID     113
#define ENV_CallStaticObjectMethod 114
#define ENV_CallStaticVoidMethod  121
#define ENV_CallStaticIntMethod   125
#define ENV_GetStaticFieldID      144
#define ENV_NewString             163
#define ENV_GetStringLength       164
#define ENV_GetStringUTFChars     169
#define ENV_ReleaseStringUTFChars 170
#define ENV_NewStringUTF          167
#define ENV_GetStringUTFLength    168
#define ENV_GetArrayLength        171
#define ENV_NewByteArray          176
#define ENV_RegisterNatives       215
#define ENV_UnregisterNatives     216
#define ENV_GetJavaVM             219
#define ENV_ExceptionCheck        228

/* JavaVM-Indices */
#define VM_DestroyJavaVM                  3
#define VM_AttachCurrentThread            4
#define VM_DetachCurrentThread            5
#define VM_GetEnv                         6
#define VM_AttachCurrentThreadAsDaemon    7

static int g_jni_initialized = 0;

static void jni_init_tables(void) {
    if (g_jni_initialized) return;
    g_jni_initialized = 1;

    /* Alle Slots mit Default-Stub fuellen */
    for (int i = 0; i < JNI_TABLE_SIZE; i++) {
        g_env_table[i] = (uintptr_t)jni_default_stub;
    }
    for (int i = 0; i < JNI_VM_TABLE_SIZE; i++) {
        g_vm_table[i] = (uintptr_t)jni_default_stub;
    }

    /* JNIEnv-Slots fuellen */
    g_env_table[ENV_GetVersion]            = (uintptr_t)jni_env_GetVersion;
    g_env_table[ENV_FindClass]             = (uintptr_t)jni_env_FindClass;
    g_env_table[ENV_ExceptionOccurred]     = (uintptr_t)jni_env_ExceptionOccurred;
    g_env_table[ENV_ExceptionDescribe]     = (uintptr_t)jni_env_ExceptionDescribe;
    g_env_table[ENV_ExceptionClear]        = (uintptr_t)jni_env_ExceptionClear;
    g_env_table[ENV_NewGlobalRef]          = (uintptr_t)jni_env_NewGlobalRef;
    g_env_table[ENV_DeleteGlobalRef]       = (uintptr_t)jni_env_DeleteGlobalRef;
    g_env_table[ENV_DeleteLocalRef]        = (uintptr_t)jni_env_DeleteLocalRef;
    g_env_table[ENV_IsSameObject]          = (uintptr_t)jni_env_IsSameObject;
    g_env_table[ENV_NewLocalRef]           = (uintptr_t)jni_env_NewLocalRef;
    g_env_table[ENV_AllocObject]           = (uintptr_t)jni_env_AllocObject;
    g_env_table[ENV_NewObject]             = (uintptr_t)jni_env_NewObject;
    g_env_table[ENV_GetObjectClass]        = (uintptr_t)jni_env_GetObjectClass;
    g_env_table[ENV_GetMethodID]           = (uintptr_t)jni_env_GetMethodID;
    g_env_table[ENV_CallObjectMethod]      = (uintptr_t)jni_env_CallObjectMethod;
    g_env_table[ENV_CallBooleanMethod]     = (uintptr_t)jni_env_CallBooleanMethod;
    g_env_table[ENV_CallIntMethod]         = (uintptr_t)jni_env_CallIntMethod;
    g_env_table[ENV_CallVoidMethod]        = (uintptr_t)jni_env_CallVoidMethod;
    g_env_table[ENV_GetFieldID]            = (uintptr_t)jni_env_GetFieldID;
    g_env_table[ENV_GetStaticMethodID]     = (uintptr_t)jni_env_GetStaticMethodID;
    g_env_table[ENV_CallStaticObjectMethod]= (uintptr_t)jni_env_CallStaticObjectMethod;
    g_env_table[ENV_CallStaticVoidMethod]  = (uintptr_t)jni_env_CallStaticVoidMethod;
    g_env_table[ENV_CallStaticIntMethod]   = (uintptr_t)jni_env_CallStaticIntMethod;
    g_env_table[ENV_GetStaticFieldID]      = (uintptr_t)jni_env_GetStaticFieldID;
    g_env_table[ENV_NewString]             = (uintptr_t)jni_env_NewStringUTF;
    g_env_table[ENV_NewStringUTF]          = (uintptr_t)jni_env_NewStringUTF;
    g_env_table[ENV_GetStringLength]       = (uintptr_t)jni_env_GetStringLength;
    g_env_table[ENV_GetStringUTFLength]    = (uintptr_t)jni_env_GetStringUTFLength;
    g_env_table[ENV_GetStringUTFChars]     = (uintptr_t)jni_env_GetStringUTFChars;
    g_env_table[ENV_ReleaseStringUTFChars] = (uintptr_t)jni_env_ReleaseStringUTFChars;
    g_env_table[ENV_GetArrayLength]        = (uintptr_t)jni_env_GetArrayLength;
    g_env_table[ENV_NewByteArray]          = (uintptr_t)jni_env_NewByteArray;
    g_env_table[ENV_RegisterNatives]       = (uintptr_t)jni_env_RegisterNatives;
    g_env_table[ENV_UnregisterNatives]     = (uintptr_t)jni_env_UnregisterNatives;
    g_env_table[ENV_GetJavaVM]             = (uintptr_t)jni_env_GetJavaVM;
    g_env_table[ENV_ExceptionCheck]        = (uintptr_t)jni_env_ExceptionCheck;

    /* JavaVM-Slots fuellen */
    g_vm_table[VM_DestroyJavaVM]      = (uintptr_t)jni_vm_DestroyJavaVM;
    g_vm_table[VM_AttachCurrentThread]= (uintptr_t)jni_vm_AttachCurrentThread;
    g_vm_table[VM_DetachCurrentThread]= (uintptr_t)jni_vm_DetachCurrentThread;
    g_vm_table[VM_GetEnv]             = (uintptr_t)jni_vm_GetEnv;

    /* Singletons verdrahten */
    g_env_singleton.functions = (void *)g_env_table;
    g_vm_singleton.functions  = (void *)g_vm_table;

    LOGI("JNI-Tabellen initialisiert: env=%p vm=%p",
         &g_env_singleton, &g_vm_singleton);
}

void *jni_get_env(void) {
    jni_init_tables();
    return &g_env_singleton;
}

void *jni_get_vm(void) {
    jni_init_tables();
    return &g_vm_singleton;
}

/* ============================================================
 * NativeLoader-Aufruf (Legacy, wird im neuen Flow nicht mehr genutzt)
 * ============================================================ */
int jni_call_native_loader(const char *cls, const char *method, const char *arg) {
    LOGI("jni_call_native_loader(%s.%s, %s)", cls, method, arg);
    extern void *so_find_addr(void *handle, const char *name);
    extern void *g_libmain_handle;

    static const char *names[] = {
        "Java_com_unity3d_player_NativeLoader_load",
        "NativeLoader_load",
        NULL
    };
    void *fn = NULL;
    for (int i = 0; names[i]; i++) {
        fn = so_find_addr(g_libmain_handle, names[i]);
        if (fn) { LOGI("Found %s at %p", names[i], fn); break; }
    }
    if (!fn) { LOGE("NativeLoader.load symbol not found"); return -1; }

    void *env = jni_get_env();
    typedef void (*fn_t)(void *, void *, const char *);
    ((fn_t)fn)(env, NULL, arg);
    return 0;
}
