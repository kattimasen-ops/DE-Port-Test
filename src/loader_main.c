#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <ucontext.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <android/native_window.h>
#include "so_util.h"
#include "jni_shim.h"

/* Adresse unseres echten Crash-Handlers an den sigaction-Intercept */
extern void (*g_de_crash_handler)(int, siginfo_t *, void *);

#define TAG "deadeffect"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#ifndef DEAD_EFFECT_LIBDIR
#define DEAD_EFFECT_LIBDIR "/roms/ports/DeadEffect/lib"
#endif
#ifndef DEAD_EFFECT_ASSETS
#define DEAD_EFFECT_ASSETS "/roms/ports/DeadEffect/assets"
#endif
#ifndef DEAD_EFFECT_CRASHLOG
#define DEAD_EFFECT_CRASHLOG "/roms/ports/DeadEffect/crash.txt"
#endif

/* ------------------------------------------------------------ */
/* Rohe, signal-sichere Ausgabe                                 */
/* ------------------------------------------------------------ */
__attribute__((noinline))
static void rawlog(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    if (n) { ssize_t r = write(2, s, n); (void)r; }
}

/* Schreibt einen String sowohl auf stderr als auch in crash.txt. */
__attribute__((noinline))
static void crash_write(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    if (!n) return;
    ssize_t r = write(2, s, n); (void)r;
    int fd = open(DEAD_EFFECT_CRASHLOG,
                  O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        ssize_t r2 = write(fd, s, n); (void)r2;
        close(fd);
    }
}

static void crash_write_hex(uint64_t v) {
    char buf[20];
    const char *hx = "0123456789abcdef";
    int n = 0;
    buf[n++] = '0'; buf[n++] = 'x';
    int started = 0;
    for (int i = 15; i >= 0; i--) {
        int nib = (v >> (i * 4)) & 0xF;
        if (nib || started || i == 0) {
            buf[n++] = hx[nib];
            started = 1;
        }
    }
    buf[n] = 0;
    crash_write(buf);
}

static void crash_write_dec(int v) {
    char t[16];
    int n = 0;
    if (v < 0) { crash_write("-"); v = -v; }
    if (v == 0) { crash_write("0"); return; }
    while (v > 0 && n < 15) { t[n++] = '0' + (v % 10); v /= 10; }
    char o[16];
    for (int i = 0; i < n; i++) o[i] = t[n - 1 - i];
    o[n] = 0;
    crash_write(o);
}

/* ------------------------------------------------------------ */
/* Crash-Handler: minimal, ohne FP-Walk                          */
/* ------------------------------------------------------------ */
__attribute__((noinline))
static void crash_handler(int sig, siginfo_t *info, void *uctx) {
    /* ERSTE Ausgabe, bevor irgendetwas schiefgehen kann */
    crash_write("\n### SIG=");
    crash_write_dec(sig);
    crash_write(" ###\n");

    if (!uctx) {
        crash_write("### ucontext=NULL ###\n");
        _exit(128 + sig);
    }
    ucontext_t *uc = (ucontext_t *)uctx;

    crash_write("### PC=");
    crash_write_hex((uint64_t)uc->uc_mcontext.pc);
    crash_write(" SP=");
    crash_write_hex((uint64_t)uc->uc_mcontext.sp);
    crash_write(" FP=");
    crash_write_hex((uint64_t)uc->uc_mcontext.regs[29]);
    crash_write(" LR=");
    crash_write_hex((uint64_t)uc->uc_mcontext.regs[30]);
    crash_write(" ADDR=");
    crash_write_hex((uint64_t)(info ? info->si_addr : 0));
    crash_write(" ###\n");

    crash_write("### REGS x0=");
    crash_write_hex((uint64_t)uc->uc_mcontext.regs[0]);
    crash_write(" x1=");
    crash_write_hex((uint64_t)uc->uc_mcontext.regs[1]);
    crash_write(" x2=");
    crash_write_hex((uint64_t)uc->uc_mcontext.regs[2]);
    crash_write(" x3=");
    crash_write_hex((uint64_t)uc->uc_mcontext.regs[3]);
    crash_write(" ###\n");

    crash_write("### x19=");
    crash_write_hex((uint64_t)uc->uc_mcontext.regs[19]);
    crash_write(" x20=");
    crash_write_hex((uint64_t)uc->uc_mcontext.regs[20]);
    crash_write(" x21=");
    crash_write_hex((uint64_t)uc->uc_mcontext.regs[21]);
    crash_write(" x29=");
    crash_write_hex((uint64_t)uc->uc_mcontext.regs[29]);
    crash_write(" ###\n");

    /* KEIN FP-Walk - der kann im Handler selbst faulten. */
    crash_write("### END (no FP-walk) ###\n");
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
    sa.sa_flags     = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

/* Verifikation: ist wirklich unser Handler aktiv? */
static void verify_sigsegv_handler(const char *where) {
    struct sigaction cur;
    memset(&cur, 0, sizeof(cur));
    if (sigaction(SIGSEGV, NULL, &cur) != 0) {
        LOGI("[verify/%s] sigaction-Abfrage fehlgeschlagen", where);
        return;
    }
    LOGI("[verify/%s] SIGSEGV handler=%p erwartet=%p flags=0x%x",
         where, (void *)cur.sa_sigaction, (void *)crash_handler,
         (unsigned)cur.sa_flags);
}

static void *crash_handler_watchdog(void *arg) {
    (void)arg;
    for (;;) {
        install_crash_handler();
        usleep(2000); /* 2 ms */
    }
    return NULL;
}

void abort(void) {
    rawlog("\n### ABORT() AUFGERUFEN ###\n");
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

void *g_unity_player_thiz = &g_fake_unity_player_obj;

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
    LOGI("Rufe initJni(env=%p, thiz=%p, context=%p)",
         env, g_unity_player_thiz, g_unity_player_thiz);
    unity_init_jni(env, g_unity_player_thiz, g_unity_player_thiz);
    LOGI("initJni OK");
    return 0;
}

static void arm_crash_handler(const char *what) {
    install_crash_handler();
    LOGI("[arm] crash handler installiert vor %s", what);
    verify_sigsegv_handler(what);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    rawlog("[boot] main() entered\n");

    /* Core-Dumps aktivieren (falls ulimit im Wrapper gesetzt) */
    prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
    struct rlimit rl = { RLIM_INFINITY, RLIM_INFINITY };
    setrlimit(RLIMIT_CORE, &rl);

    /* Unseren Handler an den sigaction-Intercept bekannt machen,
     * BEVOR irgendetwas installiert wird oder libunity lädt. */
    g_de_crash_handler = crash_handler;

    install_crash_handler();

    LOGI("Dead Effect Loader startet");
    LOGI("  libdir = %s", DEAD_EFFECT_LIBDIR);
    LOGI("  assets = %s", DEAD_EFFECT_ASSETS);

    jni_shim_init();
    jni_install_android_contract();
    rawlog("[boot] jni_shim_init + contract done\n");

    g_fake_unity_player_obj.class_ref = &g_fake_unity_player_obj;
    LOGI("Fake-UnityPlayer-Objekt vorbereitet: thiz=%p class_ref=%p",
         g_unity_player_thiz, g_fake_unity_player_obj.class_ref);

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

    if (unity_native_surface_changed) {
        arm_crash_handler("nativeSendSurfaceChangedEvent");
        LOGI("Rufe nativeSendSurfaceChangedEvent(env=%p, thiz=%p, 640, 480)",
             env, g_unity_player_thiz);
        unity_native_surface_changed(env, g_unity_player_thiz, 640, 480);
        LOGI("nativeSendSurfaceChangedEvent OK");
    } else {
        LOGI("[WARN] nativeSendSurfaceChangedEvent nicht gefunden");
    }

    if (unity_native_recreate_gfx_state) {
        arm_crash_handler("nativeRecreateGfxState");
        LOGI("Rufe nativeRecreateGfxState(env=%p, thiz=%p, 0, NULL)",
             env, g_unity_player_thiz);
        unity_native_recreate_gfx_state(env, g_unity_player_thiz, 0, NULL);
        LOGI("nativeRecreateGfxState OK");
    } else {
        LOGI("[WARN] nativeRecreateGfxState nicht gefunden");
    }

    if (unity_native_focus_change) {
        arm_crash_handler("nativeFocusChanged");
        LOGI("Rufe nativeFocusChanged(env=%p, thiz=%p, true)",
             env, g_unity_player_thiz);
        unity_native_focus_change(env, g_unity_player_thiz, 1);
        LOGI("nativeFocusChanged OK");
    } else {
        LOGI("[WARN] nativeFocusChanged nicht gefunden");
    }

    if (unity_native_resume) {
        arm_crash_handler("nativeResume");
        LOGI("Rufe nativeResume(env=%p, thiz=%p)", env, g_unity_player_thiz);
        unity_native_resume(env, g_unity_player_thiz);
        LOGI("nativeResume OK");
    } else {
        LOGI("[WARN] nativeResume nicht gefunden");
    }

    int running = 1;
    int frame   = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            jni_shim_handle_sdl_event(&ev);
        }

        jni_pump_ui_tasks();

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
