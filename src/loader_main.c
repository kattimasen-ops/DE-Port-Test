#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <android/native_window.h>
#include "so_util.h"
#include "jni_shim.h"

#define TAG "deadeffect"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#ifndef DEAD_EFFECT_LIBDIR
#define DEAD_EFFECT_LIBDIR "/roms/ports/DeadEffect/lib"
#endif
#ifndef DEAD_EFFECT_ASSETS
#define DEAD_EFFECT_ASSETS "/roms/ports/DeadEffect/assets"
#endif

ANativeWindow *g_android_window = NULL;
void *g_libmain_handle  = NULL;
void *g_libunity_handle = NULL;

/* JNI_OnLoad signature: jint JNI_OnLoad(JavaVM *vm, void *reserved) */
typedef int  (*JNI_OnLoad_t)(void *vm, void *reserved);
typedef void (*UnityPlayer_initJni_t)(void *, void *, void *);
typedef unsigned char (*UnityPlayer_nativeRender_t)(void *, void *, long long, int, int);
typedef void (*UnityPlayer_nativePause_t)(void *, void *);

static JNI_OnLoad_t               unity_onload        = NULL;
static UnityPlayer_initJni_t      unity_init_jni      = NULL;
static UnityPlayer_nativeRender_t unity_native_render = NULL;
static UnityPlayer_nativePause_t  unity_native_pause  = NULL;

static SDL_Window   *sdl_win = NULL;
static SDL_GLContext sdl_ctx = NULL;
static EGLDisplay    egl_dpy = EGL_NO_DISPLAY;
static EGLSurface    egl_surf = EGL_NO_SURFACE;
static EGLContext    egl_ctx = EGL_NO_CONTEXT;

static int video_init(void) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        LOGE("SDL_InitSubSystem: %s", SDL_GetError()); return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    sdl_win = SDL_CreateWindow("Dead Effect",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!sdl_win) { LOGE("SDL_CreateWindow: %s", SDL_GetError()); return -1; }
    sdl_ctx = SDL_GL_CreateContext(sdl_win);
    if (!sdl_ctx) { LOGE("SDL_GL_CreateContext: %s", SDL_GetError()); return -1; }
    SDL_GL_SetSwapInterval(1);
    egl_dpy  = eglGetCurrentDisplay();
    egl_surf = eglGetCurrentSurface(EGL_DRAW);
    egl_ctx  = eglGetCurrentContext();
    LOGI("EGL: dpy=%p surf=%p ctx=%p", egl_dpy, egl_surf, egl_ctx);
    LOGI("GL_VERSION: %s", glGetString(GL_VERSION));
    LOGI("GL_RENDERER: %s", glGetString(GL_RENDERER));
    g_android_window = calloc(1, sizeof(ANativeWindow));
    g_android_window->width  = 640;
    g_android_window->height = 480;
    g_android_window->format = 1;
    return 0;
}

static int load_module_chain(void) {
    char path[512];

    /* ------------------------------------------------------------------
     * 1. libunity.so zuerst laden (Strategie aus Referenz-Loader)
     * ------------------------------------------------------------------ */
    snprintf(path, sizeof(path), "%s/libunity.so", DEAD_EFFECT_LIBDIR);
    LOGI("Lade %s", path);
    g_libunity_handle = so_load(path);
    if (!g_libunity_handle) { LOGE("libunity.so laden fehlgeschlagen"); return -1; }
    LOGI("libunity.so geladen: %p", g_libunity_handle);

    /* Symbole dumpen (Diagnose) */
    so_dump_symbols(g_libunity_handle);

    /* ------------------------------------------------------------------
     * 2. JNI_OnLoad von libunity rufen, falls vorhanden
     * ------------------------------------------------------------------ */
    unity_onload = (JNI_OnLoad_t) so_find_addr(g_libunity_handle, "JNI_OnLoad");
    if (unity_onload) {
        LOGI("Rufe JNI_OnLoad(libunity) @ %p", unity_onload);
        int jni_ver = unity_onload(jni_get_env(), NULL);
        LOGI("JNI_OnLoad -> version 0x%x", jni_ver);
    } else {
        LOGI("libunity exportiert kein JNI_OnLoad (ok)");
    }

    /* ------------------------------------------------------------------
     * 3. UnityPlayer-Funktionen in libunity suchen
     * ------------------------------------------------------------------ */
    unity_init_jni      = (UnityPlayer_initJni_t)      so_find_addr(g_libunity_handle, "UnityPlayer_initJni");
    unity_native_render = (UnityPlayer_nativeRender_t) so_find_addr(g_libunity_handle, "UnityPlayer_nativeRender");
    unity_native_pause  = (UnityPlayer_nativePause_t)  so_find_addr(g_libunity_handle, "UnityPlayer_nativePause");

    LOGI("  UnityPlayer_initJni       = %p", unity_init_jni);
    LOGI("  UnityPlayer_nativeRender  = %p", unity_native_render);
    LOGI("  UnityPlayer_nativePause   = %p", unity_native_pause);

    /* ------------------------------------------------------------------
     * 4. libil2cpp.so laden
     * ------------------------------------------------------------------ */
    snprintf(path, sizeof(path), "%s/libil2cpp.so", DEAD_EFFECT_LIBDIR);
    LOGI("Lade %s", path);
    void *libil2cpp_h = so_load(path);
    if (!libil2cpp_h) { LOGE("libil2cpp.so laden fehlgeschlagen"); return -1; }
    LOGI("libil2cpp.so geladen: %p", libil2cpp_h);

    JNI_OnLoad_t il2cpp_onload = (JNI_OnLoad_t) so_find_addr(libil2cpp_h, "JNI_OnLoad");
    if (il2cpp_onload) {
        LOGI("Rufe JNI_OnLoad(libil2cpp) @ %p", il2cpp_onload);
        int ver = il2cpp_onload(jni_get_env(), NULL);
        LOGI("JNI_OnLoad(libil2cpp) -> 0x%x", ver);
    }

    /* ------------------------------------------------------------------
     * 5. libmain.so optional (nur Diagnose)
     * ------------------------------------------------------------------ */
    snprintf(path, sizeof(path), "%s/libmain.so", DEAD_EFFECT_LIBDIR);
    LOGI("Lade %s (optional)", path);
    g_libmain_handle = so_load(path);
    if (g_libmain_handle) {
        LOGI("libmain.so geladen: %p", g_libmain_handle);
        so_dump_symbols(g_libmain_handle);
    } else {
        LOGI("libmain.so konnte nicht geladen werden (nicht kritisch)");
    }

    /* ------------------------------------------------------------------
     * 6. UnityPlayer.initJni aufrufen
     * ------------------------------------------------------------------ */
    if (unity_init_jni) {
        void *env      = jni_get_env();
        void *fake_ctx = jni_make_fake_context();
        LOGI("Rufe UnityPlayer.initJni(env=%p, NULL, ctx=%p)", env, fake_ctx);
        unity_init_jni(env, NULL, fake_ctx);
        LOGI("UnityPlayer.initJni OK");
    } else {
        LOGE("UnityPlayer.initJni nicht gefunden!");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    LOGI("Dead Effect Loader startet");
    LOGI("  libdir = %s", DEAD_EFFECT_LIBDIR);
    LOGI("  assets = %s", DEAD_EFFECT_ASSETS);

    jni_shim_init();
    if (video_init() != 0) return 1;
    if (load_module_chain() != 0) return 1;

    jni_shim_set_egl(egl_dpy, egl_surf, egl_ctx);

    int running = 1;
    void *env = jni_get_env();
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            jni_shim_handle_sdl_event(&ev);
        }
        if (unity_native_render && env) {
            unity_native_render(env, NULL, (long long)SDL_GetTicks(), 640, 480);
        }
        SDL_GL_SwapWindow(sdl_win);
    }
    if (unity_native_pause && env) unity_native_pause(env, NULL);
    LOGI("Loader beendet");
    return 0;
}
