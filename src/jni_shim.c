#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <android/log.h>
#include "jni_shim.h"

#define TAG "deadeffect-jni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLSurface g_surf = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;

void jni_shim_init(void) { LOGI("JNI-Shim initialisiert"); }

void jni_shim_set_egl(EGLDisplay d, EGLSurface s, EGLContext c) {
    g_dpy = d; g_surf = s; g_ctx = c;
    LOGI("JNI-Shim EGL: dpy=%p surf=%p ctx=%p", d, s, c);
}

/* Simplified fake JNIEnv: we don't need a real one because we call
 * NativeLoader.load directly via so_find_addr. */
void *jni_get_env(void) { static int fake = 0; return &fake; }
void *jni_make_fake_context(void) { static int fake = 0; return &fake; }

int jni_call_native_loader(const char *cls, const char *method, const char *arg) {
    LOGI("jni_call_native_loader(%s.%s, %s)", cls, method, arg);
    /* We import so_find_addr from so_util */
    extern void *so_find_addr(void *handle, const char *name);
    extern void *g_libmain_handle;

    /* Unity's standard NativeLoader.load symbol — the JNI auto-mangled name */
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

void jni_shim_handle_sdl_event(SDL_Event *ev) { (void)ev; }
