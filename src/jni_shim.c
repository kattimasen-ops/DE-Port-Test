#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <android/log.h>
#include "jni_shim.h"

#define TAG "deadeffect-jni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef uint16_t jchar;

/* ================================================================
 * Globale EGL-Handles (nur fuer Info/Debug)
 * ================================================================ */
static EGLDisplay g_dpy  = EGL_NO_DISPLAY;
static EGLSurface g_surf = EGL_NO_SURFACE;
static EGLContext g_ctx  = EGL_NO_CONTEXT;

/* ================================================================
 * Fake JNIEnv / JavaVM Tabellen
 * ================================================================ */
#define JNI_TABLE_SIZE      512
#define JNI_VM_TABLE_SIZE    16
#define JNI_MAX_NATIVES    1024
#define JNI_MAX_MIDS        512
#define JNI_MAX_FIDS        512
#define JNI_MAX_CLASSES     256

typedef struct { void *functions; } jni_vm_t;
typedef struct { void *functions; } jni_env_t;

static uintptr_t g_vm_table[JNI_VM_TABLE_SIZE];
static jni_vm_t  g_vm_singleton;

static uintptr_t g_env_table[JNI_TABLE_SIZE];
static jni_env_t g_env_singleton;

/* ---- Registrierte native Methoden ---- */
typedef struct {
    const char *class_name;
    const char *method_name;
    const char *signature;
    void       *fn_ptr;
} jni_native_entry;

static jni_native_entry g_natives[JNI_MAX_NATIVES];
static int              g_native_count = 0;

/* ---- mid / fid Registry ---- */
typedef struct {
    char class_name[64];
    char method_name[64];
    char signature[64];
    int  is_static;
    int  is_field;
} jni_id_entry;

static jni_id_entry g_mids[JNI_MAX_MIDS];
static int          g_mid_count = 0;

static jni_id_entry g_fids[JNI_MAX_FIDS];
static int          g_fid_count = 0;

/* ---- Class Registry ---- */
static char    g_class_names[JNI_MAX_CLASSES][128];
static int     g_class_count = 0;
static uint8_t g_class_storage[JNI_MAX_CLASSES][64];

/* ---- Fake Object Storage (stable pointers) ---- */
static uint8_t g_fake_obj_storage[64]  = {0};
static uint8_t g_fake_str_storage[256] = {0};

/* ================================================================
 * Hilfsfunktionen
 * ================================================================ */
static uintptr_t jni_default_stub(void) { return 0; }

static int find_mid_index(void *mid) {
    if (!mid) return -1;
    uintptr_t p = (uintptr_t)mid;
    uintptr_t base = (uintptr_t)&g_mids[0];
    uintptr_t end  = (uintptr_t)&g_mids[JNI_MAX_MIDS];
    if (p < base || p >= end) return -1;
    return (int)((p - base) / sizeof(jni_id_entry));
}

static int find_fid_index(void *fid) {
    if (!fid) return -1;
    uintptr_t p = (uintptr_t)fid;
    uintptr_t base = (uintptr_t)&g_fids[0];
    uintptr_t end  = (uintptr_t)&g_fids[JNI_MAX_FIDS];
    if (p < base || p >= end) return -1;
    return (int)((p - base) / sizeof(jni_id_entry));
}

static const char *class_name_from_handle(void *cls) {
    if (!cls) return "?";
    uintptr_t p = (uintptr_t)cls;
    for (int i = 0; i < g_class_count; i++) {
        if (p == (uintptr_t)&g_class_storage[i][0])
            return g_class_names[i];
    }
    return "?";
}

/* ================================================================
 * JavaVM Funktionen
 * ================================================================ */
static int jni_vm_GetEnv(void *vm, void **penv, int version) {
    (void)vm;
    if (penv) *penv = &g_env_singleton;
    LOGI("JNI VM GetEnv -> %p (version 0x%x)", &g_env_singleton, version);
    return 0;
}
static int jni_vm_AttachCurrentThread(void *vm, void **penv, void *args) {
    (void)vm; (void)args;
    if (penv) *penv = &g_env_singleton;
    LOGI("JNI VM AttachCurrentThread");
    return 0;
}
static int jni_vm_DetachCurrentThread(void *vm) { (void)vm; return 0; }
static int jni_vm_DestroyJavaVM(void *vm)       { (void)vm; return 0; }

/* ================================================================
 * JNIEnv: Grundfunktionen
 * ================================================================ */
static int jni_env_GetVersion(void *env) { (void)env; return 0x00010006; }

static void *jni_env_FindClass(void *env, const char *name) {
    (void)env;
    LOGI("JNI FindClass(%s)", name ? name : "?");
    if (!name) return NULL;
    for (int i = 0; i < g_class_count; i++) {
        if (strcmp(g_class_names[i], name) == 0)
            return &g_class_storage[i][0];
    }
    if (g_class_count >= JNI_MAX_CLASSES) return NULL;
    int i = g_class_count++;
    strncpy(g_class_names[i], name, 127);
    g_class_names[i][127] = 0;
    LOGI("  -> neue Klasse #%d registriert: %s", i, name);
    return &g_class_storage[i][0];
}

static void *jni_env_GetObjectClass(void *env, void *obj) {
    (void)env; (void)obj;
    if (g_class_count == 0) {
        strcpy(g_class_names[0], "java/lang/Object");
        g_class_count = 1;
    }
    return &g_class_storage[0][0];
}

static int jni_env_IsInstanceOf(void *env, void *obj, void *cls) {
    (void)env;
    const char *cn = class_name_from_handle(cls);
    LOGI("JNI IsInstanceOf(obj=%p, class=%s) -> true (fake)", obj, cn);
    return obj != NULL ? 1 : 0;
}

static int jni_env_IsSameObject(void *env, void *a, void *b) {
    (void)env;
    return (a == b) ? 1 : 0;
}

/* ================================================================
 * JNIEnv: Method / Field IDs
 * ================================================================ */
static void *jni_env_GetMethodID(void *env, void *cls,
                                 const char *name, const char *sig) {
    (void)env;
    const char *cn = class_name_from_handle(cls);
    LOGI("JNI GetMethodID(%s.%s%s)", cn, name ? name : "?", sig ? sig : "");
    if (!name || g_mid_count >= JNI_MAX_MIDS) return NULL;
    jni_id_entry *e = &g_mids[g_mid_count++];
    strncpy(e->class_name,  cn, 63);
    strncpy(e->method_name, name, 63);
    strncpy(e->signature,   sig ? sig : "", 63);
    e->is_static = 0;
    e->is_field  = 0;
    return e;
}

static void *jni_env_GetStaticMethodID(void *env, void *cls,
                                       const char *name, const char *sig) {
    (void)env;
    const char *cn = class_name_from_handle(cls);
    LOGI("JNI GetStaticMethodID(%s.%s%s)", cn, name ? name : "?", sig ? sig : "");
    if (!name || g_mid_count >= JNI_MAX_MIDS) return NULL;
    jni_id_entry *e = &g_mids[g_mid_count++];
    strncpy(e->class_name,  cn, 63);
    strncpy(e->method_name, name, 63);
    strncpy(e->signature,   sig ? sig : "", 63);
    e->is_static = 1;
    e->is_field  = 0;
    return e;
}

static void *jni_env_GetFieldID(void *env, void *cls,
                                const char *name, const char *sig) {
    (void)env;
    const char *cn = class_name_from_handle(cls);
    LOGI("JNI GetFieldID(%s.%s%s)", cn, name ? name : "?", sig ? sig : "");
    if (!name || g_fid_count >= JNI_MAX_FIDS) return NULL;
    jni_id_entry *e = &g_fids[g_fid_count++];
    strncpy(e->class_name,  cn, 63);
    strncpy(e->method_name, name, 63);
    strncpy(e->signature,   sig ? sig : "", 63);
    e->is_static = 0;
    e->is_field  = 1;
    return e;
}

static void *jni_env_GetStaticFieldID(void *env, void *cls,
                                      const char *name, const char *sig) {
    (void)env;
    const char *cn = class_name_from_handle(cls);
    LOGI("JNI GetStaticFieldID(%s.%s%s)", cn, name ? name : "?", sig ? sig : "");
    if (!name || g_fid_count >= JNI_MAX_FIDS) return NULL;
    jni_id_entry *e = &g_fids[g_fid_count++];
    strncpy(e->class_name,  cn, 63);
    strncpy(e->method_name, name, 63);
    strncpy(e->signature,   sig ? sig : "", 63);
    e->is_static = 1;
    e->is_field  = 1;
    return e;
}

/* ================================================================
 * JNIEnv: Call*Method Dispatcher
 * ================================================================ */
static void *make_fake_string(const char *s) {
    return (void *)s;
}

static uintptr_t jni_dispatch_object_method(void *mid, int is_static) {
    int idx = find_mid_index(mid);
    if (idx < 0) {
        LOGI("  [dispatch] unbekannte mid %p -> NULL", mid);
        return 0;
    }
    jni_id_entry *e = &g_mids[idx];
    LOGI("  [dispatch] %s%s.%s%s",
         is_static ? "static " : "",
         e->class_name, e->method_name, e->signature);

    if (strstr(e->class_name, "AudioManager") ||
        strstr(e->method_name, "getProperty")) {
        if (strstr(e->method_name, "getProperty")) {
            LOGI("  [dispatch] -> fake AudioManager property \"48000\"");
            return (uintptr_t)make_fake_string("48000");
        }
    }

    if (strstr(e->method_name, "parseInt") ||
        strstr(e->method_name, "valueOf")) {
        LOGI("  [dispatch] -> fake int 0");
        return 0;
    }

    if (strstr(e->method_name, "getWidth")  ||
        strstr(e->method_name, "getHeight") ||
        strstr(e->method_name, "getDisplayMetrics")) {
        LOGI("  [dispatch] -> fake display metric 640/480");
        return 0;
    }

    LOGI("  [dispatch] -> NULL (unbekannt)");
    return 0;
}

static uintptr_t jni_dispatch_void_method(void *mid, int is_static) {
    int idx = find_mid_index(mid);
    if (idx < 0) return 0;
    jni_id_entry *e = &g_mids[idx];
    LOGI("  [dispatch void] %s%s.%s%s",
         is_static ? "static " : "",
         e->class_name, e->method_name, e->signature);
    return 0;
}

static uintptr_t jni_dispatch_int_method(void *mid, int is_static) {
    int idx = find_mid_index(mid);
    if (idx < 0) return 0;
    jni_id_entry *e = &g_mids[idx];
    LOGI("  [dispatch int] %s%s.%s%s",
         is_static ? "static " : "",
         e->class_name, e->method_name, e->signature);

    if (strstr(e->method_name, "getWidth"))   return 640;
    if (strstr(e->method_name, "getHeight"))  return 480;
    if (strstr(e->method_name, "getVersion")) return 0x00010006;
    return 0;
}

/* ---- Konkrete Call*Method-Implementierungen ---- */
static uintptr_t jni_env_CallObjectMethod(void *env, void *obj, void *mid, ...) {
    (void)env; (void)obj;
    return jni_dispatch_object_method(mid, 0);
}
static uintptr_t jni_env_CallStaticObjectMethod(void *env, void *cls, void *mid, ...) {
    (void)env; (void)cls;
    return jni_dispatch_object_method(mid, 1);
}
static uintptr_t jni_env_CallVoidMethod(void *env, void *obj, void *mid, ...) {
    (void)env; (void)obj;
    return jni_dispatch_void_method(mid, 0);
}
static uintptr_t jni_env_CallStaticVoidMethod(void *env, void *cls, void *mid, ...) {
    (void)env; (void)cls;
    return jni_dispatch_void_method(mid, 1);
}
static uintptr_t jni_env_CallIntMethod(void *env, void *obj, void *mid, ...) {
    (void)env; (void)obj;
    return jni_dispatch_int_method(mid, 0);
}
static uintptr_t jni_env_CallStaticIntMethod(void *env, void *cls, void *mid, ...) {
    (void)env; (void)cls;
    return jni_dispatch_int_method(mid, 1);
}
static uintptr_t jni_env_CallBooleanMethod(void *env, void *obj, void *mid, ...) {
    (void)env; (void)obj; (void)mid;
    return 0;
}
static uintptr_t jni_env_CallObjectMethodV(void *env, void *obj, void *mid, va_list args) {
    (void)env; (void)obj; (void)args;
    return jni_dispatch_object_method(mid, 0);
}
static uintptr_t jni_env_CallStaticObjectMethodV(void *env, void *cls, void *mid, va_list args) {
    (void)env; (void)cls; (void)args;
    return jni_dispatch_object_method(mid, 1);
}
static uintptr_t jni_env_CallVoidMethodV(void *env, void *obj, void *mid, va_list args) {
    (void)env; (void)obj; (void)args;
    return jni_dispatch_void_method(mid, 0);
}
static uintptr_t jni_env_CallStaticVoidMethodV(void *env, void *cls, void *mid, va_list args) {
    (void)env; (void)cls; (void)args;
    return jni_dispatch_void_method(mid, 1);
}
static uintptr_t jni_env_CallVoidMethodA(void *env, void *obj, void *mid, void *args) {
    (void)env; (void)obj; (void)args;
    return jni_dispatch_void_method(mid, 0);
}
static uintptr_t jni_env_CallStaticVoidMethodA(void *env, void *cls, void *mid, void *args) {
    (void)env; (void)cls; (void)args;
    return jni_dispatch_void_method(mid, 1);
}
static uintptr_t jni_env_CallObjectMethodA(void *env, void *obj, void *mid, void *args) {
    (void)env; (void)obj; (void)args;
    return jni_dispatch_object_method(mid, 0);
}
static uintptr_t jni_env_CallStaticObjectMethodA(void *env, void *cls, void *mid, void *args) {
    (void)env; (void)cls; (void)args;
    return jni_dispatch_object_method(mid, 1);
}

/* ================================================================
 * JNIEnv: Strings
 * ================================================================ */
static void *jni_env_NewStringUTF(void *env, const char *utf) {
    (void)env;
    LOGI("JNI NewStringUTF(%s)", utf ? utf : "(null)");
    return (void *)(utf ? utf : "");
}

static void *jni_env_NewString(void *env, const jchar *unicode, int len) {
    (void)env;
    if (!unicode || len <= 0) return (void *)"";
    char *out = malloc((size_t)len + 1);
    if (!out) return NULL;
    for (int i = 0; i < len; i++) out[i] = (char)(unicode[i] & 0xFF);
    out[len] = 0;
    LOGI("JNI NewString(%d) -> %s", len, out);
    return out;
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

/* ================================================================
 * JNIEnv: Fields (Object / Int)
 * ================================================================ */
static void *jni_env_GetStaticObjectField(void *env, void *cls, void *fid) {
    (void)env; (void)cls;
    int idx = find_fid_index(fid);
    if (idx >= 0) {
        LOGI("JNI GetStaticObjectField(%s.%s) -> fake obj",
             g_fids[idx].class_name, g_fids[idx].method_name);
    }
    return g_fake_obj_storage;
}

static void *jni_env_GetObjectField(void *env, void *obj, void *fid) {
    (void)env; (void)obj;
    int idx = find_fid_index(fid);
    if (idx >= 0) {
        LOGI("JNI GetObjectField(%s.%s) -> fake obj",
             g_fids[idx].class_name, g_fids[idx].method_name);
    }
    return g_fake_obj_storage;
}

static void jni_env_SetObjectField(void *env, void *obj, void *fid, void *val) {
    (void)env; (void)obj; (void)fid; (void)val;
}

static int jni_env_GetStaticIntField(void *env, void *cls, void *fid) {
    (void)env; (void)cls; (void)fid;
    return 0;
}
static int jni_env_GetIntField(void *env, void *obj, void *fid) {
    (void)env; (void)obj; (void)fid;
    return 0;
}
static void jni_env_SetIntField(void *env, void *obj, void *fid, int val) {
    (void)env; (void)obj; (void)fid; (void)val;
}

/* ================================================================
 * JNIEnv: Arrays
 * ================================================================ */
static void *jni_env_GetObjectArrayElement(void *env, void *arr, int idx) {
    (void)env; (void)arr; (void)idx;
    return g_fake_obj_storage;
}
static void *jni_env_NewObjectArray(void *env, int len, void *cls, void *init) {
    (void)env; (void)len; (void)cls; (void)init;
    return g_fake_obj_storage;
}
static void jni_env_SetObjectArrayElement(void *env, void *arr, int idx, void *val) {
    (void)env; (void)arr; (void)idx; (void)val;
}
static void *jni_env_NewByteArray(void *env, int len) {
    (void)env; (void)len;
    return g_fake_obj_storage;
}
static int jni_env_GetArrayLength(void *env, void *arr) {
    (void)env; (void)arr;
    return 0;
}

/* ================================================================
 * JNIEnv: Object Allocation
 * ================================================================ */
static void *jni_env_NewObject(void *env, void *cls, void *mid, ...) {
    (void)env; (void)cls; (void)mid;
    return g_fake_obj_storage;
}
static void *jni_env_AllocObject(void *env, void *cls) {
    (void)env; (void)cls;
    return g_fake_obj_storage;
}

/* ================================================================
 * JNIEnv: References
 * ================================================================ */
static void *jni_env_NewGlobalRef(void *env, void *obj) { (void)env; return obj; }
static void *jni_env_NewLocalRef(void *env, void *obj)  { (void)env; return obj; }
static void  jni_env_DeleteGlobalRef(void *env, void *obj) { (void)env; (void)obj; }
static void  jni_env_DeleteLocalRef(void *env, void *obj)  { (void)env; (void)obj; }

/* ================================================================
 * JNIEnv: Exceptions
 * ================================================================ */
static void *jni_env_ExceptionOccurred(void *env) { (void)env; return NULL; }
static void  jni_env_ExceptionDescribe(void *env) { (void)env; }
static void  jni_env_ExceptionClear(void *env)    { (void)env; }
static int   jni_env_ExceptionCheck(void *env)    { (void)env; return 0; }

/* ================================================================
 * JNIEnv: VM Access
 * ================================================================ */
static int jni_env_GetJavaVM(void *env, void **pvm) {
    (void)env;
    if (pvm) *pvm = &g_vm_singleton;
    return 0;
}

/* ================================================================
 * JNIEnv: RegisterNatives
 * ================================================================ */
static int jni_env_RegisterNatives(void *env, void *cls,
                                   const void *methods, int n) {
    (void)env; (void)cls;
    const struct {
        const char *name;
        const char *signature;
        void       *fnPtr;
    } *m = (const void *)methods;

    if (!m || n <= 0) {
        LOGI("JNI RegisterNatives(count=%d) — leer", n);
        return 0;
    }
    for (int i = 0; i < n && g_native_count < JNI_MAX_NATIVES; i++) {
        g_natives[g_native_count].class_name  = NULL;
        g_natives[g_native_count].method_name = m[i].name;
        g_natives[g_native_count].signature   = m[i].signature;
        g_natives[g_native_count].fn_ptr      = m[i].fnPtr;
        g_native_count++;
    }
    LOGI("JNI RegisterNatives(count=%d), total=%d", n, g_native_count);
    return 0;
}
static int jni_env_UnregisterNatives(void *env, void *cls) { (void)env; (void)cls; return 0; }

/* ================================================================
 * Slot-Nummern der JNIEnv-Tabelle (JDK 8 / jni.h)
 * ================================================================ */
#define ENV_GetVersion                4
#define ENV_FindClass                 6
#define ENV_ExceptionOccurred        15
#define ENV_ExceptionDescribe        16
#define ENV_ExceptionClear           17
#define ENV_NewGlobalRef             21
#define ENV_DeleteGlobalRef          22
#define ENV_DeleteLocalRef           23
#define ENV_IsSameObject             24
#define ENV_NewLocalRef              25
#define ENV_AllocObject              27
#define ENV_NewObject                28
#define ENV_GetObjectClass           31
#define ENV_IsInstanceOf             32
#define ENV_GetMethodID              33
#define ENV_CallObjectMethod         34
#define ENV_CallObjectMethodV        35
#define ENV_CallObjectMethodA        36
#define ENV_CallBooleanMethod        37
#define ENV_CallIntMethod            49
#define ENV_CallVoidMethod           61
#define ENV_CallVoidMethodV          62
#define ENV_CallVoidMethodA          63
#define ENV_GetFieldID               94
#define ENV_GetObjectField           95
#define ENV_GetIntField             100
#define ENV_SetObjectField          104
#define ENV_SetIntField             109
#define ENV_GetStaticMethodID       113
#define ENV_CallStaticObjectMethod  114
#define ENV_CallStaticObjectMethodV 115
#define ENV_CallStaticObjectMethodA 116
#define ENV_CallStaticVoidMethod    121
#define ENV_CallStaticVoidMethodV   122
#define ENV_CallStaticVoidMethodA   123
#define ENV_CallStaticIntMethod     125
#define ENV_GetStaticFieldID        144
#define ENV_GetStaticObjectField    145
#define ENV_GetStaticIntField       150
#define ENV_NewString               163
#define ENV_GetStringLength         164
#define ENV_NewStringUTF            167
#define ENV_GetStringUTFLength      168
#define ENV_GetStringUTFChars       169
#define ENV_ReleaseStringUTFChars   170
#define ENV_GetArrayLength          171
#define ENV_NewObjectArray          172
#define ENV_GetObjectArrayElement   173
#define ENV_SetObjectArrayElement   174
#define ENV_NewByteArray            176
#define ENV_RegisterNatives         215
#define ENV_UnregisterNatives       216
#define ENV_GetJavaVM               219
#define ENV_ExceptionCheck          228

#define VM_DestroyJavaVM                  3
#define VM_AttachCurrentThread            4
#define VM_DetachCurrentThread            5
#define VM_GetEnv                         6

/* ================================================================
 * Tabellen-Aufbau
 * ================================================================ */
static int g_jni_initialized = 0;

static void jni_init_tables(void) {
    if (g_jni_initialized) return;
    g_jni_initialized = 1;

    for (int i = 0; i < JNI_TABLE_SIZE; i++)
        g_env_table[i] = (uintptr_t)jni_default_stub;
    for (int i = 0; i < JNI_VM_TABLE_SIZE; i++)
        g_vm_table[i] = (uintptr_t)jni_default_stub;

    g_env_table[ENV_GetVersion]             = (uintptr_t)jni_env_GetVersion;
    g_env_table[ENV_FindClass]              = (uintptr_t)jni_env_FindClass;
    g_env_table[ENV_ExceptionOccurred]      = (uintptr_t)jni_env_ExceptionOccurred;
    g_env_table[ENV_ExceptionDescribe]      = (uintptr_t)jni_env_ExceptionDescribe;
    g_env_table[ENV_ExceptionClear]         = (uintptr_t)jni_env_ExceptionClear;
    g_env_table[ENV_NewGlobalRef]           = (uintptr_t)jni_env_NewGlobalRef;
    g_env_table[ENV_DeleteGlobalRef]        = (uintptr_t)jni_env_DeleteGlobalRef;
    g_env_table[ENV_DeleteLocalRef]         = (uintptr_t)jni_env_DeleteLocalRef;
    g_env_table[ENV_IsSameObject]           = (uintptr_t)jni_env_IsSameObject;
    g_env_table[ENV_NewLocalRef]            = (uintptr_t)jni_env_NewLocalRef;
    g_env_table[ENV_AllocObject]            = (uintptr_t)jni_env_AllocObject;
    g_env_table[ENV_NewObject]              = (uintptr_t)jni_env_NewObject;
    g_env_table[ENV_GetObjectClass]         = (uintptr_t)jni_env_GetObjectClass;
    g_env_table[ENV_IsInstanceOf]           = (uintptr_t)jni_env_IsInstanceOf;
    g_env_table[ENV_GetMethodID]            = (uintptr_t)jni_env_GetMethodID;
    g_env_table[ENV_CallObjectMethod]       = (uintptr_t)jni_env_CallObjectMethod;
    g_env_table[ENV_CallObjectMethodV]      = (uintptr_t)jni_env_CallObjectMethodV;
    g_env_table[ENV_CallObjectMethodA]      = (uintptr_t)jni_env_CallObjectMethodA;
    g_env_table[ENV_CallBooleanMethod]      = (uintptr_t)jni_env_CallBooleanMethod;
    g_env_table[ENV_CallIntMethod]          = (uintptr_t)jni_env_CallIntMethod;
    g_env_table[ENV_CallVoidMethod]         = (uintptr_t)jni_env_CallVoidMethod;
    g_env_table[ENV_CallVoidMethodV]        = (uintptr_t)jni_env_CallVoidMethodV;
    g_env_table[ENV_CallVoidMethodA]        = (uintptr_t)jni_env_CallVoidMethodA;
    g_env_table[ENV_GetFieldID]             = (uintptr_t)jni_env_GetFieldID;
    g_env_table[ENV_GetObjectField]         = (uintptr_t)jni_env_GetObjectField;
    g_env_table[ENV_GetIntField]            = (uintptr_t)jni_env_GetIntField;
    g_env_table[ENV_SetObjectField]         = (uintptr_t)jni_env_SetObjectField;
    g_env_table[ENV_SetIntField]            = (uintptr_t)jni_env_SetIntField;
    g_env_table[ENV_GetStaticMethodID]      = (uintptr_t)jni_env_GetStaticMethodID;
    g_env_table[ENV_CallStaticObjectMethod] = (uintptr_t)jni_env_CallStaticObjectMethod;
    g_env_table[ENV_CallStaticObjectMethodV]= (uintptr_t)jni_env_CallStaticObjectMethodV;
    g_env_table[ENV_CallStaticObjectMethodA]= (uintptr_t)jni_env_CallStaticObjectMethodA;
    g_env_table[ENV_CallStaticVoidMethod]   = (uintptr_t)jni_env_CallStaticVoidMethod;
    g_env_table[ENV_CallStaticVoidMethodV]  = (uintptr_t)jni_env_CallStaticVoidMethodV;
    g_env_table[ENV_CallStaticVoidMethodA]  = (uintptr_t)jni_env_CallStaticVoidMethodA;
    g_env_table[ENV_CallStaticIntMethod]    = (uintptr_t)jni_env_CallStaticIntMethod;
    g_env_table[ENV_GetStaticFieldID]       = (uintptr_t)jni_env_GetStaticFieldID;
    g_env_table[ENV_GetStaticObjectField]   = (uintptr_t)jni_env_GetStaticObjectField;
    g_env_table[ENV_GetStaticIntField]      = (uintptr_t)jni_env_GetStaticIntField;
    g_env_table[ENV_NewString]              = (uintptr_t)jni_env_NewString;
    g_env_table[ENV_GetStringLength]        = (uintptr_t)jni_env_GetStringLength;
    g_env_table[ENV_NewStringUTF]           = (uintptr_t)jni_env_NewStringUTF;
    g_env_table[ENV_GetStringUTFLength]     = (uintptr_t)jni_env_GetStringUTFLength;
    g_env_table[ENV_GetStringUTFChars]      = (uintptr_t)jni_env_GetStringUTFChars;
    g_env_table[ENV_ReleaseStringUTFChars]  = (uintptr_t)jni_env_ReleaseStringUTFChars;
    g_env_table[ENV_GetArrayLength]         = (uintptr_t)jni_env_GetArrayLength;
    g_env_table[ENV_NewObjectArray]         = (uintptr_t)jni_env_NewObjectArray;
    g_env_table[ENV_GetObjectArrayElement]  = (uintptr_t)jni_env_GetObjectArrayElement;
    g_env_table[ENV_SetObjectArrayElement]  = (uintptr_t)jni_env_SetObjectArrayElement;
    g_env_table[ENV_NewByteArray]           = (uintptr_t)jni_env_NewByteArray;
    g_env_table[ENV_RegisterNatives]        = (uintptr_t)jni_env_RegisterNatives;
    g_env_table[ENV_UnregisterNatives]      = (uintptr_t)jni_env_UnregisterNatives;
    g_env_table[ENV_GetJavaVM]              = (uintptr_t)jni_env_GetJavaVM;
    g_env_table[ENV_ExceptionCheck]         = (uintptr_t)jni_env_ExceptionCheck;

    g_vm_table[VM_DestroyJavaVM]       = (uintptr_t)jni_vm_DestroyJavaVM;
    g_vm_table[VM_AttachCurrentThread] = (uintptr_t)jni_vm_AttachCurrentThread;
    g_vm_table[VM_DetachCurrentThread] = (uintptr_t)jni_vm_DetachCurrentThread;
    g_vm_table[VM_GetEnv]              = (uintptr_t)jni_vm_GetEnv;

    g_env_singleton.functions = (void *)g_env_table;
    g_vm_singleton.functions  = (void *)g_vm_table;

    LOGI("JNI-Tabellen initialisiert: env=%p vm=%p",
         &g_env_singleton, &g_vm_singleton);
}

/* ================================================================
 * Oeffentliche API (jni_shim.h)
 * ================================================================ */
void *jni_get_env(void) { jni_init_tables(); return &g_env_singleton; }
void *jni_get_vm(void)  { jni_init_tables(); return &g_vm_singleton; }

void jni_shim_init(void) {
    jni_init_tables();
    LOGI("JNI-Shim initialisiert");
}

void jni_shim_set_egl(EGLDisplay d, EGLSurface s, EGLContext c) {
    g_dpy  = d;
    g_surf = s;
    g_ctx  = c;
    LOGI("JNI-Shim EGL: dpy=%p surf=%p ctx=%p", d, s, c);
}

void jni_shim_handle_sdl_event(SDL_Event *ev) {
    /* Aktuell kein Input-Handling im Shim. */
    (void)ev;
}

void *jni_get_activity(void) {
    /* Stabiler Fake-Activity-Zeiger (nicht NULL, damit libunity
     * nicht versucht, ihn zu dereferenzieren). */
    static uint8_t fake_activity[64] = {0};
    return fake_activity;
}

void jni_pump_ui_tasks(void) {
    /* Kein UI-Thread im DE-Loader. No-op. */
}

void jni_dump_natives(void) {
    jni_init_tables();
    LOGI("=== JNI Registry: %d Methoden ===", g_native_count);
    for (int i = 0; i < g_native_count; i++) {
        LOGI("  [%3d] %s -> %p",
             i,
             g_natives[i].method_name ? g_natives[i].method_name : "?",
             g_natives[i].fn_ptr);
    }
}

void *jni_find_native(const char *class_name, const char *method_name) {
    (void)class_name;
    jni_init_tables();
    if (!method_name) return NULL;
    for (int i = 0; i < g_native_count; i++) {
        if (g_natives[i].method_name &&
            strcmp(g_natives[i].method_name, method_name) == 0) {
            return g_natives[i].fn_ptr;
        }
    }
    return NULL;
}

void *jni_find_native_signature(const char *signature) {
    jni_init_tables();
    if (!signature) return NULL;
    for (int i = 0; i < g_native_count; i++) {
        if (g_natives[i].signature &&
            strcmp(g_natives[i].signature, signature) == 0) {
            return g_natives[i].fn_ptr;
        }
    }
    return NULL;
}

int jni_call_native_loader(const char *cls, const char *method, const char *arg) {
    LOGI("jni_call_native_loader(%s.%s, %s)",
         cls ? cls : "?", method ? method : "?", arg ? arg : "?");
    /* Fuer DE nicht benoetigt — libunity/libil2cpp werden direkt
     * ueber ihren JNI_OnLoad initialisiert. */
    return 0;
}

void jni_install_android_contract(void) {
    /* Pre-Registrierung einiger Klassen, die libunity ueber
     * FindClass erwartet, damit sie nicht NULL zurueckbekommt. */
    jni_init_tables();
    (void)jni_env_FindClass(NULL, "com/unity3d/player/UnityPlayer");
    (void)jni_env_FindClass(NULL, "com/unity3d/player/NativeLoader");
    (void)jni_env_FindClass(NULL, "java/lang/Object");
    LOGI("JNI Android-Contract installiert");
}