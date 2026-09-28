#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <ucontext.h>
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

/* ============================================================
 * Robust Crash-Handler
 * Kein malloc/fprintf/backtrace_symbols — die wuerden bei einem
 * korrupten Heap selbst crashen. Nur write() und ein statischer
 * Buffer. PC/SP/Fault-Addr kommen direkt aus dem Signal-Kontext.
 * ============================================================ */

/* Sehr primitives Hex-Format (kein malloc). */
static int fmt_hex(uint64_t v, char *out) {
    const char *hx = "0123456789abcdef";
    int n = 0;
    out[n++] = '0'; out[n++] = 'x';
    int started = 0;
    for (int i = 15; i >= 0; i--) {
        int nibble = (v >> (i*4)) & 0xF;
        if (nibble || started || i == 0) {
            out[n++] = hx[nibble];
            started = 1;
        }
    }
    out[n] = 0;
    return n;
}

static int fmt_dec(int v, char *out) {
    char tmp[16];
    int n = 0;
    if (v < 0) { out[n++] = '-'; v = -v; }
    if (v == 0) { out[n++] = '0'; out[n] = 0; return n; }
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    for (int i = 0; i < n/2; i++) {
        char t = tmp[i]; tmp[i] = tmp[n-1-i]; tmp[n-1-i] = t;
    }
    for (int i = 0; i < n; i++) out[i] = tmp[i];
    out[n] = 0;
    return n;
}

static void safe_write(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    ssize_t r = write(2, s, len);
    (void)r;
}

static void crash_handler(int sig, siginfo_t *info, void *uctx) {
    ucontext_t *uc = (ucontext_t *)uctx;
    char hexbuf[24];
    char decbuf[16];
    char line[256];
    int n = 0;

    /* Fester Vorspann */
    const char *hdr = "\n### SIGNAL ";
    while (*hdr) line[n++] = *hdr++;

    fmt_dec(sig, decbuf);
    for (int i = 0; decbuf[i]; i++) line[n++] = decbuf[i];

    const char *p1 = " PC=";
    while (*p1) line[n++] = *p1++;
    fmt_hex((uint64_t)uc->uc_mcontext.pc, hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];

    const char *p2 = " SP=";
    while (*p2) line[n++] = *p2++;
    fmt_hex((uint64_t)uc->uc_mcontext.sp, hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];

    const char *p3 = " ADDR=";
    while (*p3) line[n++] = *p3++;
    fmt_hex((uint64_t)(info ? info->si_addr : 0), hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];

    const char *tail = " ###\n";
    while (*tail) line[n++] = *tail++;
    line[n] = 0;
    safe_write(line);

    _exit(128 + sig);
}

static void install_crash_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

/* abort() ueberschreiben: kein glibc-Backtrace, nur PC. */
void abort(void) {
    safe_write("\n### ABORT() AUFGERUFEN ###\n");
    _exit(134);
}

#ifndef DEAD_EFFECT_LIBDIR
#define DEAD_EFFECT_LIBDIR "/roms/ports/DeadEffect/lib"
#endif
#ifndef DEAD_EFFECT_ASSETS
#define DEAD_EFFECT_ASSETS "/roms/ports/DeadEffect/assets"
#endif

ANativeWindow *g_android_window = NULL;
void *g_libmain_handle  = NULL;
void *g_libunity_handle = NULL;
void *g_libil2cpp_handle = NULL;

typedef int  (*JNI_OnLoad_t)(void *vm, void *reserved);
typedef void (*initJni_t)(void *, void *, void *);
typedef unsigned char (*nativeRender_t)(void *, void *, long long, int, int);
typedef void (*nativePause_t)(void *, void *);

static initJni_t      unity_init_jni      = NULL;
static nativeRender_t unity_native_render = NULL;
static nativePause_t  unity_native_pause  = NULL;

static SDL_Window   *sdl_win = NULL;
static SDL_GLContext sdl_ctx = NULL;
static EGLDisplay    egl_dpy = EGL_NO_DISPLAY;
static EGLSurface    egl_surf = EGL_NO_SURFACE;
static EGLContext    egl_ctx = EGL_NO_CONTEXT;

static int heap_is_sane(const char *when) {
    void *a = malloc(16);
    if (!a) { LOGE("  Heap-Check(%s): malloc(16) fehlgeschlagen", when); return 0; }
    memset(a, 0xAA, 16);
    free(a);

    void *big = malloc(128 * 1024);
    if (!big) { LOGE("  Heap-Check(%s): malloc(128K) fehlgeschlagen", when); return 0; }
    memset(big, 0xCC, 128 * 1024);
    free(big);

    LOGI("  Heap OK (%s)", when);
    return 1;
}

static int video_init(void) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        LOGE("SDL_InitSubSystem: %s", SDL_GetError());
        return -1;
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

static void try_call_onload(const char *libname, void *handle) {
    JNI_OnLoad_t fn = (JNI_OnLoad_t) so_find_addr(handle, "JNI_OnLoad");
    if (!fn) {
        LOGI("[%s] kein JNI_OnLoad", libname);
        return;
    }
    LOGI("[%s] Rufe JNI_OnLoad(%p) mit vm=%p", libname, fn, jni_get_vm());
    int ver = fn(jni_get_vm(), NULL);
    LOGI("[%s] JNI_OnLoad -> 0x%x", libname, ver);
}

static void preload_libcxx(void) {
    char p[512];
    void *h = NULL;

    snprintf(p, sizeof(p), "%s/libc++_shared.so", DEAD_EFFECT_LIBDIR);
    h = dlopen(p, RTLD_NOW | RTLD_GLOBAL);
    if (h) { LOGI("libc++_shared.so vorgeladen: %p", h); return; }

    h = dlopen("libc++_shared.so", RTLD_NOW | RTLD_GLOBAL);
    if (h) { LOGI("libc++_shared.so vorgeladen (via Name): %p", h); return; }

    LOGI("[INFO] externe libc++_shared.so nicht geladen — "
         "Unity-Libs sind statisch gelinkt, nicht benoetigt");
}

static int load_module_chain(void) {
    char path[512];

    if (!heap_is_sane("vor libmain")) return -1;

    snprintf(path, sizeof(path), "%s/libmain.so", DEAD_EFFECT_LIBDIR);
    LOGI("Lade %s", path);
    g_libmain_handle = so_load(path);
    if (!g_libmain_handle) { LOGE("libmain.so laden fehlgeschlagen"); return -1; }
    LOGI("libmain.so geladen: %p", g_libmain_handle);
    if (!heap_is_sane("nach libmain")) return -1;
    try_call_onload("libmain", g_libmain_handle);

    snprintf(path, sizeof(path), "%s/libunity.so", DEAD_EFFECT_LIBDIR);
    LOGI("Lade %s", path);
    g_libunity_handle = so_load(path);
    if (!g_libunity_handle) { LOGE("libunity.so laden fehlgeschlagen"); return -1; }
    LOGI("libunity.so geladen: %p", g_libunity_handle);
    if (!heap_is_sane("nach libunity")) return -1;
    so_dump_symbols(g_libunity_handle);
    try_call_onload("libunity", g_libunity_handle);

    snprintf(path, sizeof(path), "%s/libil2cpp.so", DEAD_EFFECT_LIBDIR);
    LOGI("Lade %s", path);
    g_libil2cpp_handle = so_load(path);
    if (!g_libil2cpp_handle) { LOGE("libil2cpp.so laden fehlgeschlagen"); return -1; }
    LOGI("libil2cpp.so geladen: %p", g_libil2cpp_handle);
    if (!heap_is_sane("nach libil2cpp")) return -1;
    try_call_onload("libil2cpp", g_libil2cpp_handle);

    jni_dump_natives();

    const char *UP = "com/unity3d/player/UnityPlayer";
    unity_init_jni      = (initJni_t)      jni_find_native(UP, "initJni");
    unity_native_render = (nativeRender_t) jni_find_native(UP, "nativeRender");
    unity_native_pause  = (nativePause_t)  jni_find_native(UP, "nativePause");

    LOGI("  initJni      = %p", unity_init_jni);
    LOGI("  nativeRender = %p", unity_native_render);
    LOGI("  nativePause  = %p", unity_native_pause);

    if (!unity_init_jni) {
        LOGE("initJni nicht in JNI-Registry gefunden!");
        return -1;
    }
    void *env = jni_get_env();
    LOGI("Rufe initJni(env=%p, NULL, NULL)", env);
    unity_init_jni(env, NULL, NULL);
    LOGI("initJni OK");
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    install_crash_handler();

    LOGI("Dead Effect Loader startet");
    LOGI("  libdir = %s", DEAD_EFFECT_LIBDIR);
    LOGI("  assets = %s", DEAD_EFFECT_ASSETS);

    jni_shim_init();
    preload_libcxx();

    if (video_init() != 0) return 1;
    if (load_module_chain() != 0) return 1;

    jni_shim_set_egl(egl_dpy, egl_surf, egl_ctx);

    int running = 1;
    void *env = jni_get_env();
    int frame = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            jni_shim_handle_sdl_event(&ev);
        }
        if (unity_native_render && env) {
            LOGI("render frame %d — rufe nativeRender ...", frame);
            unity_native_render(env, NULL, (long long)SDL_GetTicks(), 640, 480);
            LOGI("render frame %d — nativeRender OK", frame);
        } else {
            LOGI("render frame %d — kein nativeRender/ env", frame);
        }
        SDL_GL_SwapWindow(sdl_win);
        frame++;
        if (frame > 5) {
            /* Nach 5 Frames absichtlich beenden, damit Test kurz bleibt */
            LOGI("5 Frames erreicht — beende Test");
            running = 0;
        }
    }
    if (unity_native_pause && env) unity_native_pause(env, NULL);
    LOGI("Loader beendet");
    return 0;
}
