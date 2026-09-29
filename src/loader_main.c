#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <ucontext.h>
#include <pthread.h>
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

__attribute__((noinline))
static void rawlog(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    if (n) { ssize_t r = write(2, s, n); (void)r; }
}

static int fmt_hex(uint64_t v, char *out) {
    const char *hx = "0123456789abcdef";
    int n = 0; out[n++] = '0'; out[n++] = 'x'; int started = 0;
    for (int i = 15; i >= 0; i--) {
        int nib = (v >> (i * 4)) & 0xF;
        if (nib || started || i == 0) { out[n++] = hx[nib]; started = 1; }
    }
    out[n] = 0; return n;
}

static int fmt_dec(int v, char *out) {
    char tmp[16]; int n = 0;
    if (v < 0) { out[n++] = '-'; v = -v; }
    if (v == 0) { out[n++] = '0'; out[n] = 0; return n; }
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    for (int i = 0; i < n / 2; i++) {
        char t = tmp[i]; tmp[i] = tmp[n - 1 - i]; tmp[n - 1 - i] = t;
    }
    for (int i = 0; i < n; i++) out[i] = tmp[i];
    out[n] = 0; return n;
}

static void safe_write(const char *s) {
    size_t len = 0; while (s[len]) len++;
    ssize_t r = write(2, s, len); (void)r;
}

static void dump_fp_chain(uint64_t fp, uint64_t pc, uint64_t lr) {
    char buf[128]; int n;
    safe_write("\n### BACKTRACE (FP chain) ###\n");
    for (int i = 0; i < 24; i++) {
        n = 0; buf[n++] = ' '; buf[n++] = '#';
        if (i >= 10) buf[n++] = '0' + (i / 10);
        buf[n++] = '0' + (i % 10); buf[n++] = ' ';
        if (i == 0) {
            const char *p = "PC="; while (*p) buf[n++] = *p++;
            fmt_hex(pc, buf + n); while (buf[n]) n++;
            p = " LR="; while (*p) buf[n++] = *p++;
            fmt_hex(lr, buf + n); while (buf[n]) n++;
            p = " FP="; while (*p) buf[n++] = *p++;
            fmt_hex(fp, buf + n); while (buf[n]) n++;
        } else if (fp && fp >= 0x1000 && (fp & 7) == 0) {
            volatile uint64_t *q = (volatile uint64_t *)fp;
            uint64_t next_fp = q[0]; uint64_t ret_lr = q[1];
            const char *p = "LR="; while (*p) buf[n++] = *p++;
            fmt_hex(ret_lr, buf + n); while (buf[n]) n++;
            p = " FP="; while (*p) buf[n++] = *p++;
            fmt_hex(next_fp, buf + n); while (buf[n]) n++;
            if (next_fp <= fp || (next_fp & 7) != 0) fp = 0; else fp = next_fp;
        } else break;
        buf[n++] = '\n'; buf[n] = 0;
        safe_write(buf);
    }
    safe_write("### END BACKTRACE ###\n");
}

__attribute__((noinline))
static void crash_handler(int sig, siginfo_t *info, void *uctx) {
    if (!uctx) {
        safe_write("\n### SIGNAL but uctx=NULL ###\n");
        _exit(128 + sig);
    }
    ucontext_t *uc = (ucontext_t *)uctx;
    char hexbuf[24];
    char decbuf[16];
    char line[512];
    int n = 0;
    const char *hdr = "\n### SIGNAL "; while (*hdr) line[n++] = *hdr++;
    fmt_dec(sig, decbuf); for (int i = 0; decbuf[i]; i++) line[n++] = decbuf[i];
    uint64_t pc   = (uint64_t)uc->uc_mcontext.pc;
    uint64_t sp   = (uint64_t)uc->uc_mcontext.sp;
    uint64_t fp   = (uint64_t)uc->uc_mcontext.regs[29];
    uint64_t lr   = (uint64_t)uc->uc_mcontext.regs[30];
    uint64_t addr = (uint64_t)(info ? info->si_addr : 0);
    const char *p;
    p = " PC=";   while (*p) line[n++] = *p++; fmt_hex(pc, hexbuf);   for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    p = " SP=";   while (*p) line[n++] = *p++; fmt_hex(sp, hexbuf);   for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    p = " FP=";   while (*p) line[n++] = *p++; fmt_hex(fp, hexbuf);   for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    p = " LR=";   while (*p) line[n++] = *p++; fmt_hex(lr, hexbuf);   for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    p = " ADDR="; while (*p) line[n++] = *p++; fmt_hex(addr, hexbuf); for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    p = " ###\n"; while (*p) line[n++] = *p++;
    line[n] = 0;
    safe_write(line);
    n = 0;
    const char *pr = "### REGS x0=";
    while (*pr) line[n++] = *pr++;
    fmt_hex((uint64_t)uc->uc_mcontext.regs[0], hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    pr = " x1="; while (*pr) line[n++] = *pr++;
    fmt_hex((uint64_t)uc->uc_mcontext.regs[1], hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    pr = " x2="; while (*pr) line[n++] = *pr++;
    fmt_hex((uint64_t)uc->uc_mcontext.regs[2], hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    pr = " x3="; while (*pr) line[n++] = *pr++;
    fmt_hex((uint64_t)uc->uc_mcontext.regs[3], hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    pr = " ###\n"; while (*pr) line[n++] = *pr++;
    line[n] = 0;
    safe_write(line);
    dump_fp_chain(fp, pc, lr);
    _exit(128 + sig);
}

static char g_alt_stack[SIGSTKSZ * 4] __attribute__((aligned(16)));

static void install_crash_handler(void) {
    stack_t ss;
    memset(&ss, 0, sizeof(ss));
    ss.ss_sp    = g_alt_stack;
    ss.ss_size  = sizeof(g_alt_stack);
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags     = SA_SIGINFO | SA_ONSTACK;  /* KEIN SA_RESETHAND mehr,
                                                 * damit der Handler auch
                                                 * mehrfach feuert */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

/* Watchdog: installiert den Handler sehr aggressiv (alle 10ms) neu,
 * weil libunity ihn offenbar waehrend nativeSendSurfaceChangedEvent
 * ueberschreibt. */
static void *crash_handler_watchdog(void *arg) {
    (void)arg;
    for (;;) {
        install_crash_handler();
        usleep(10000);  /* 10 ms */
    }
    return NULL;
}

void abort(void) {
    safe_write("\n### ABORT() AUFGERUFEN ###\n");
    _exit(134);
}

ANativeWindow *g_android_window = NULL;
void *g_libmain_handle   = NULL;
void *g_libunity_handle  = NULL;
void *g_libil2cpp_handle = NULL;

static struct {
    void *class_ref;
    uint8_t payload[512];
} g_fake_unity_player_obj = {0};

static void *g_unity_player_thiz = &g_fake_unity_player_obj;

typedef int  (*JNI_OnLoad_t)(void *vm, void *reserved);
typedef void (*initJni_t)(void *, void *, void *);
typedef unsigned char (*nativeRender_t)(void *, void *);
typedef void (*nativePause_t)(void *, void *);
typedef void (*nativeResume_t)(void *, void *);
typedef void (*nativeFocusChanged_t)(void *, void *, unsigned char);
typedef void (*nativeSendSurfaceChangedEvent_t)(void *, void *, int, int);
typedef void (*nativeRecreateGfxState_t)(void *, void *, int, void *);

static initJni_t             unity_init_jni                    = NULL;
static nativeRender_t        unity_native_render               = NULL;
static nativePause_t         unity_native_pause                = NULL;
static nativeResume_t        unity_native_resume               = NULL;
static nativeFocusChanged_t  unity_native_focus_change         = NULL;
static nativeSendSurfaceChangedEvent_t unity_native_surface_changed = NULL;
static nativeRecreateGfxState_t unity_native_recreate_gfx_state = NULL;

static SDL_Window    *sdl_win  = NULL;
static SDL_GLContext  sdl_ctx  = NULL;
static EGLDisplay     egl_dpy  = EGL_NO_DISPLAY;
static EGLSurface     egl_surf = EGL_NO_SURFACE;
static EGLContext     egl_ctx  = EGL_NO_CONTEXT;

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
    if (!fn) { LOGI("[%s] kein JNI_OnLoad", libname); return; }
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
    unity_init_jni               = (initJni_t)             jni_find_native(UP, "initJni");
    unity_native_render          = (nativeRender_t)        jni_find_native(UP, "nativeRender");
    unity_native_pause           = (nativePause_t)         jni_find_native(UP, "nativePause");
    unity_native_resume          = (nativeResume_t)        jni_find_native(UP, "nativeResume");
    unity_native_focus_change    = (nativeFocusChanged_t)  jni_find_native(UP, "nativeFocusChanged");
    unity_native_surface_changed = (nativeSendSurfaceChangedEvent_t) jni_find_native(UP, "nativeSendSurfaceChangedEvent");
    unity_native_recreate_gfx_state = (nativeRecreateGfxState_t)     jni_find_native(UP, "nativeRecreateGfxState");

    LOGI("  initJni                       = %p", unity_init_jni);
    LOGI("  nativeRender                  = %p", unity_native_render);
    LOGI("  nativePause                   = %p", unity_native_pause);
    LOGI("  nativeResume                  = %p", unity_native_resume);
    LOGI("  nativeFocusChanged            = %p", unity_native_focus_change);
    LOGI("  nativeSendSurfaceChangedEvent = %p", unity_native_surface_changed);
    LOGI("  nativeRecreateGfxState        = %p", unity_native_recreate_gfx_state);

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

/* Hilfsfunktion: installiert den Crash-Handler direkt vor einem
 * kritischen nativen Aufruf, um sicherzustellen, dass libunity ihn
 * nicht kurz vorher ueberschrieben hat. */
static void arm_crash_handler(const char *what) {
    install_crash_handler();
    LOGI("[arm] crash handler installiert vor %s", what);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    rawlog("[boot] main() entered\n");
    install_crash_handler();

    LOGI("Dead Effect Loader startet");
    LOGI("  libdir = %s", DEAD_EFFECT_LIBDIR);
    LOGI("  assets = %s", DEAD_EFFECT_ASSETS);

    jni_shim_init();
    rawlog("[boot] jni_shim_init done\n");
    preload_libcxx();
    rawlog("[boot] preload_libcxx done\n");

    rawlog("[boot] video_init...\n");
    if (video_init() != 0) { rawlog("[boot] video_init failed\n"); return 1; }
    rawlog("[boot] video_init done\n");

    rawlog("[boot] load_module_chain...\n");
    if (load_module_chain() != 0) { rawlog("[boot] load_module_chain failed\n"); return 1; }
    rawlog("[boot] load_module_chain done\n");

    rawlog("[boot] starting crash handler watchdog...\n");
    pthread_t watchdog;
    if (pthread_create(&watchdog, NULL, crash_handler_watchdog, NULL) != 0) {
        rawlog("[boot] WARN: watchdog thread creation failed\n");
    } else {
        rawlog("[boot] watchdog thread started\n");
    }

    jni_shim_set_egl(egl_dpy, egl_surf, egl_ctx);
    void *env = jni_get_env();

    LOGI("g_unity_player_thiz = %p (statisches Objekt, 512+ Bytes)", g_unity_player_thiz);

    /* === DIAGNOSE-LAUF: nativeSendSurfaceChangedEvent ueberspringen ===
     * Die Funktion crasht, bevor sie irgendeine JNI-Methode aufruft.
     * Sie erwartet vermutlich ein echtes Java-Objekt, kein Fake.
     * Wir versuchen, ohne sie weiterzukommen.
     */
    LOGI("[SKIP] nativeSendSurfaceChangedEvent wird uebersprungen");

    /* --- nativeRecreateGfxState versuchen --- */
    if (unity_native_recreate_gfx_state) {
        arm_crash_handler("nativeRecreateGfxState");
        LOGI("Rufe nativeRecreateGfxState(env=%p, thiz=%p, 0, NULL)",
             env, g_unity_player_thiz);
        unity_native_recreate_gfx_state(env, g_unity_player_thiz, 0, NULL);
        LOGI("nativeRecreateGfxState OK");
    } else {
        LOGI("[WARN] nativeRecreateGfxState nicht gefunden");
    }

    /* --- Focus Gain --- */
    if (unity_native_focus_change) {
        arm_crash_handler("nativeFocusChanged");
        LOGI("Rufe nativeFocusChanged(env=%p, thiz=%p, true)",
             env, g_unity_player_thiz);
        unity_native_focus_change(env, g_unity_player_thiz, 1);
        LOGI("nativeFocusChanged OK");
    } else {
        LOGI("[WARN] nativeFocusChanged nicht gefunden");
    }

    /* --- Resume --- */
    if (unity_native_resume) {
        arm_crash_handler("nativeResume");
        LOGI("Rufe nativeResume(env=%p, thiz=%p)", env, g_unity_player_thiz);
        unity_native_resume(env, g_unity_player_thiz);
        LOGI("nativeResume OK");
    } else {
        LOGI("[WARN] nativeResume nicht gefunden");
    }

    /* --- Render-Loop --- */
    int running = 1;
    int frame   = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            jni_shim_handle_sdl_event(&ev);
        }
        if (unity_native_render && env) {
            arm_crash_handler("nativeRender");
            LOGI("render frame %d — rufe nativeRender(env=%p, thiz=%p) ...",
                 frame, env, g_unity_player_thiz);
            unsigned char ok = unity_native_render(env, g_unity_player_thiz);
            LOGI("render frame %d — nativeRender OK (rc=%u)", frame, ok);
        } else {
            LOGI("render frame %d — kein nativeRender/env", frame);
        }
        SDL_GL_SwapWindow(sdl_win);
        frame++;
        if (frame > 5) {
            LOGI("5 Frames erreicht — beende Test");
            running = 0;
        }
    }

    /* --- Shutdown --- */
    if (unity_native_pause && env) {
        arm_crash_handler("nativePause");
        LOGI("Rufe nativePause(env=%p, thiz=%p)", env, g_unity_player_thiz);
        unity_native_pause(env, g_unity_player_thiz);
    }
    if (unity_native_focus_change) {
        LOGI("Rufe nativeFocusChanged(env=%p, thiz=%p, false)",
             env, g_unity_player_thiz);
        unity_native_focus_change(env, g_unity_player_thiz, 0);
    }
    LOGI("Loader beendet");
    return 0;
}
