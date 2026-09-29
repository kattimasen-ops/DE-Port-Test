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

/* ============================================================
 * Crash-Handler
 *
 * - Fester, statisch allokierter Stack (sigaltstack), damit wir
 *   nicht vom bereits korrupten Thread-Stack abhaengig sind.
 * - no_stack_protector: verhindert, dass der Canary-Check
 *   im Handler selbst einen zweiten SIGSEGV ausloest.
 * - Nur write() und statische Buffer, kein malloc/fprintf.
 * ============================================================ */

__attribute__((no_stack_protector, noinline))
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

__attribute__((no_stack_protector, noinline))
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

__attribute__((no_stack_protector, noinline))
static void safe_write(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    ssize_t r = write(2, s, len);
    (void)r;
}

__attribute__((no_stack_protector, noinline))
static void crash_handler(int sig, siginfo_t *info, void *uctx) {
    ucontext_t *uc = (ucontext_t *)uctx;
    char hexbuf[24];
    char decbuf[16];
    char line[256];
    int n = 0;

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

static char g_alt_stack[SIGSTKSZ * 4] __attribute__((aligned(16)));

static void install_crash_handler(void) {
    stack_t ss;
    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = g_alt_stack;
    ss.ss_size = sizeof(g_alt_stack);
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

void abort(void) {
    safe_write("\n### ABORT() AUFGERUFEN ###\n");
    _exit(134);
}

/* ============================================================
 * Globaler State
 * ============================================================ */
ANativeWindow *g_android_window = NULL;
void *g_libmain_handle  = NULL;
void *g_libunity_handle = NULL;
void *g_libil2cpp_handle = NULL;

typedef int  (*JNI_OnLoad_t)(void *vm, void *reserved);
typedef void (*UnityMain_t)(void *env, void *thiz);
typedef void (*nativePause_t)(void *, void *);

static UnityMain_t   unity_main_fn     = NULL;
static nativePause_t unity_native_pause = NULL;

static SDL_Window   *sdl_win = NULL;
static SDL_GLContext sdl_ctx = NULL;
static EGLDisplay    egl_dpy = EGL_NO_DISPLAY;
static EGLSurface    egl_surf = EGL_NO_SURFACE;
static EGLContext    egl_ctx = EGL_NO_CONTEXT;

/* ============================================================
 * Heap-Sanity
 * ============================================================ */
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

/* ============================================================
 * SDL/EGL-Setup
 * ============================================================ */
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

/* ============================================================
 * Modul-Laden
 * ============================================================ */
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

    LOGI("[INFO] externe libc++_shared.so nicht geladen");
}

/* ============================================================
 * Init-Reihenfolge (an DT angelehnt):
 *
 *  1. libmain.so laden
 *  2. JNI_OnLoad(libmain)
 *  3. NativeLoader.load(libdir) -> lädt libunity, libil2cpp, initJni
 *  4. UnityMain-Native suchen
 * ============================================================ */
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

    /* NativeLoader.load triggert in libmain.so das Laden von
     * libunity.so, libil2cpp.so (per dlopen-Hook) und ruft
     * intern UnityPlayer.initJni auf. */
    LOGI("Rufe NativeLoader.load(%s)", DEAD_EFFECT_LIBDIR);
    int rc = jni_call_native_loader(
        "com/unity3d/player/NativeLoader", "load", DEAD_EFFECT_LIBDIR);
    if (rc != 0) {
        LOGE("NativeLoader.load fehlgeschlagen (rc=%d)", rc);
        return -1;
    }
    LOGI("NativeLoader.load OK");

    /* Handle auf libunity besorgen: NativeLoader hat es bereits
     * geladen, aber wir kennen den Zeiger nicht. Sicherheitshalber
     * prüfen, ob es im dlopen-Hook gelandet ist. */
    const char *UP = "com/unity3d/player/UnityPlayer";
    unity_main_fn      = (UnityMain_t)   jni_find_native(UP, "UnityMain");
    unity_native_pause = (nativePause_t) jni_find_native(UP, "nativePause");

    LOGI("  UnityMain    = %p", unity_main_fn);
    LOGI("  nativePause  = %p", unity_native_pause);

    if (!unity_main_fn) {
        LOGE("UnityMain nicht in JNI-Registry gefunden!");
        jni_dump_natives();
        return -1;
    }

    return 0;
}

/* ============================================================
 * UnityMain-Thread: blockiert bis Spiel beendet.
 * ============================================================ */
static volatile int g_unity_running = 0;

static void *unity_main_thread(void *arg) {
    (void)arg;
    void *env = jni_get_env();
    LOGI("UnityMain-Thread startet (env=%p, thiz=%p)", env, g_android_window);
    g_unity_running = 1;
    unity_main_fn(env, g_android_window);
    g_unity_running = 0;
    LOGI("UnityMain-Thread beendet");
    return NULL;
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

    /* UnityMain im eigenen Thread starten.
     * Der Main-Thread kümmert sich um SDL-Input und Quit. */
    pthread_t t;
    if (pthread_create(&t, NULL, unity_main_thread, NULL) != 0) {
        LOGE("pthread_create für UnityMain fehlgeschlagen");
        return 1;
    }

    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
            jni_shim_handle_sdl_event(&ev);
        }
        SDL_Delay(16);
        if (!g_unity_running) running = 0;
    }

    void *env = jni_get_env();
    if (unity_native_pause && env) {
        LOGI("nativePause aufrufen");
        unity_native_pause(env, NULL);
    }

    LOGI("Loader beendet");
    return 0;
}
