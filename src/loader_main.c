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
 * RAW-Logging (unbuffered, direkt via write(2))
 * ============================================================ */
__attribute__((noinline))
static void rawlog(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    if (n) { ssize_t r = write(2, s, n); (void)r; }
}

__attribute__((noinline))
static void rawlog_hex(uint64_t v) {
    char buf[24];
    const char *hx = "0123456789abcdef";
    int n = 0;
    buf[n++] = '0'; buf[n++] = 'x';
    int started = 0;
    for (int i = 15; i >= 0; i--) {
        int nib = (v >> (i*4)) & 0xF;
        if (nib || started || i == 0) { buf[n++] = hx[nib]; started = 1; }
    }
    buf[n] = 0;
    write(2, buf, n);
}

/* ============================================================
 * Crash-Handler (aus dem vorigen Fix beibehalten)
 * ============================================================ */
__attribute__((noinline))
static int fmt_hex(uint64_t v, char *out) {
    const char *hx = "0123456789abcdef";
    int n = 0; out[n++] = '0'; out[n++] = 'x'; int started = 0;
    for (int i = 15; i >= 0; i--) {
        int nib = (v >> (i*4)) & 0xF;
        if (nib || started || i == 0) { out[n++] = hx[nib]; started = 1; }
    }
    out[n] = 0; return n;
}
__attribute__((noinline))
static int fmt_dec(int v, char *out) {
    char tmp[16]; int n = 0;
    if (v < 0) { out[n++] = '-'; v = -v; }
    if (v == 0) { out[n++] = '0'; out[n] = 0; return n; }
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    for (int i = 0; i < n/2; i++) { char t = tmp[i]; tmp[i] = tmp[n-1-i]; tmp[n-1-i] = t; }
    for (int i = 0; i < n; i++) out[i] = tmp[i];
    out[n] = 0; return n;
}
__attribute__((noinline))
static void safe_write(const char *s) {
    size_t len = 0; while (s[len]) len++;
    ssize_t r = write(2, s, len); (void)r;
}
__attribute__((noinline))
static void dump_fp_chain(uint64_t fp, uint64_t pc, uint64_t lr) {
    char buf[128]; int n;
    safe_write("\n### BACKTRACE (FP chain) ###\n");
    for (int i = 0; i < 20; i++) {
        n = 0; buf[n++] = ' '; buf[n++] = '#';
        if (i >= 10) buf[n++] = '0' + (i/10);
        buf[n++] = '0' + (i%10); buf[n++] = ' ';
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
    ucontext_t *uc = (ucontext_t *)uctx;
    char hexbuf[24]; char decbuf[16]; char line[512]; int n = 0;
    const char *hdr = "\n### SIGNAL "; while (*hdr) line[n++] = *hdr++;
    fmt_dec(sig, decbuf); for (int i = 0; decbuf[i]; i++) line[n++] = decbuf[i];
    uint64_t pc = (uint64_t)uc->uc_mcontext.pc;
    uint64_t sp = (uint64_t)uc->uc_mcontext.sp;
    uint64_t fp = (uint64_t)uc->uc_mcontext.regs[29];
    uint64_t lr = (uint64_t)uc->uc_mcontext.regs[30];
    uint64_t addr = (uint64_t)(info ? info->si_addr : 0);
    const char *p;
    p = " PC="; while (*p) line[n++] = *p++; fmt_hex(pc, hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    p = " SP="; while (*p) line[n++] = *p++; fmt_hex(sp, hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    p = " FP="; while (*p) line[n++] = *p++; fmt_hex(fp, hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    p = " LR="; while (*p) line[n++] = *p++; fmt_hex(lr, hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    p = " ADDR="; while (*p) line[n++] = *p++; fmt_hex(addr, hexbuf);
    for (int i = 0; hexbuf[i]; i++) line[n++] = hexbuf[i];
    p = " ###\n"; while (*p) line[n++] = *p++;
    line[n] = 0; safe_write(line);
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
    line[n] = 0; safe_write(line);
    dump_fp_chain(fp, pc, lr);
    _exit(128 + sig);
}
static char g_alt_stack[SIGSTKSZ * 4] __attribute__((aligned(16)));
static void install_crash_handler(void) {
    stack_t ss; memset(&ss, 0, sizeof(ss));
    ss.ss_sp = g_alt_stack; ss.ss_size = sizeof(g_alt_stack); ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);  sigaction(SIGFPE, &sa, NULL);
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
void *g_libmain_handle   = NULL;
void *g_libunity_handle  = NULL;
void *g_libil2cpp_handle = NULL;

typedef int  (*JNI_OnLoad_t)(void *vm, void *reserved);
typedef void (*UnityMain_t)(void *env, void *thiz);
typedef void (*nativePause_t)(void *, void *);
typedef void (*initJni_t)(void *, void *, void *);

static UnityMain_t   unity_main_fn      = NULL;
static nativePause_t unity_native_pause = NULL;
static initJni_t     unity_init_jni     = NULL;

static SDL_Window    *sdl_win  = NULL;
static SDL_GLContext  sdl_ctx  = NULL;
static EGLDisplay     egl_dpy  = EGL_NO_DISPLAY;
static EGLSurface     egl_surf = EGL_NO_SURFACE;
static EGLContext     egl_ctx  = EGL_NO_CONTEXT;

/* ============================================================
 * Capability Preflight (aus nxport.json abgeleitet)
 *
 * DT prüft 10 Capabilities in Phasen. Wir prüfen sie gesammelt
 * und protokollieren, was fehlt, BEVOR Unity geladen wird.
 * Das verhindert stumme Crashes durch fehlende SDL/EGL/ALSA.
 * ============================================================ */

typedef struct {
    const char *name;
    int (*check)(void);
    int satisfied;
} capability_t;

static int cap_portmaster(void) {
    return access("/opt/system/Tools/PortMaster", F_OK) == 0;
}
static int cap_aarch64_libs(void) {
    void *h = dlopen("libc.so.6", RTLD_NOW);
    if (h) { dlclose(h); return 1; }
    return 0;
}
static int cap_graphics_window(void) {
    return (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0;
}
static int cap_graphics_gles2(void) {
    const char *v = (const char *)glGetString(GL_VERSION);
    return v && (strstr(v, "OpenGL ES 2") || strstr(v, "OpenGL ES 3"));
}
static int cap_graphics_egl(void) {
    return egl_dpy != EGL_NO_DISPLAY;
}
static int cap_graphics_egl_config(void) {
    EGLConfig cfg; EGLint n = 0;
    if (egl_dpy == EGL_NO_DISPLAY) return 0;
    eglGetConfigs(egl_dpy, &cfg, 1, &n);
    return n > 0;
}
static int cap_graphics_drawable(void) {
    return egl_surf != EGL_NO_SURFACE;
}
static int cap_audio_output(void) {
    SDL_AudioSpec want = {0}, have;
    want.freq = 48000; want.channels = 2; want.format = AUDIO_S16SYS;
    want.samples = 1024; want.callback = NULL;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev) { SDL_CloseAudioDevice(dev); return 1; }
    return 0;
}
static int cap_input_mapping(void) {
    const char *f = getenv("SDL_GAMECONTROLLERCONFIG_FILE");
    if (f && access(f, R_OK) == 0) return 1;
    return SDL_GameControllerEventState(SDL_QUERY) != 0;
}
static int cap_input_controller_api(void) {
    return (SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) != 0;
}

static capability_t g_caps[] = {
    { "host.portmaster",            cap_portmaster,              0 },
    { "host.aarch64-libs",          cap_aarch64_libs,            0 },
    { "graphics.window",            cap_graphics_window,         0 },
    { "graphics.gles2",             cap_graphics_gles2,          0 },
    { "graphics.egl",               cap_graphics_egl,            0 },
    { "graphics.egl-config",        cap_graphics_egl_config,     0 },
    { "graphics.drawable",          cap_graphics_drawable,       0 },
    { "audio.output-open",          cap_audio_output,            0 },
    { "input.controller-mapping",   cap_input_mapping,           0 },
    { "input.controller-api",       cap_input_controller_api,    0 },
};

static int capability_check(void) {
    int satisfied = 0;
    int total = (int)(sizeof(g_caps)/sizeof(g_caps[0]));
    for (int i = 0; i < total; i++) {
        g_caps[i].satisfied = g_caps[i].check ? g_caps[i].check() : 1;
        if (g_caps[i].satisfied) satisfied++;
        LOGI("[cap] %-30s %s", g_caps[i].name,
             g_caps[i].satisfied ? "OK" : "MISSING");
    }
    LOGI("[cap] %d/%d satisfied", satisfied, total);
    rawlog("[boot] capabilities checked\n");
    return satisfied;
}

/* ============================================================
 * Heap-Sanity
 * ============================================================ */
static int heap_is_sane(const char *when) {
    void *a = malloc(16);
    if (!a) { LOGE("  Heap-Check(%s): malloc(16) fehlgeschlagen", when); return 0; }
    memset(a, 0xAA, 16); free(a);
    void *big = malloc(128 * 1024);
    if (!big) { LOGE("  Heap-Check(%s): malloc(128K) fehlgeschlagen", when); return 0; }
    memset(big, 0xCC, 128 * 1024); free(big);
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
    LOGI("[INFO] externe libc++_shared.so nicht geladen");
}

/* Findet ein Modul anhand seines Pfads. Ersetzt so_is_our_handle-basierte
 * Heuristiken durch einen präzisen Lookup. */
extern int so_module_count(void);
extern const char *so_module_path(int index);

static int module_loaded(const char *needle) {
    int n = so_module_count();
    for (int i = 0; i < n; i++) {
        const char *p = so_module_path(i);
        if (p && strstr(p, needle)) return 1;
    }
    return 0;
}

/* ============================================================
 * Init-Reihenfolge (nach DT adapter-contract-v1.json):
 *
 *  load-libmain
 *  init-libmain (JNI_OnLoad)
 *  native-loader (NativeLoader.load → lädt libunity + libil2cpp)
 *  init-jni (UnityPlayer.initJni – EXPLIZIT)
 *  → erst danach ist UnityMain in der Registry
 * ============================================================ */
static int load_module_chain(void) {
    char path[512];

    rawlog("[boot] load_module_chain: vor heap_is_sane\n");
    if (!heap_is_sane("vor libmain")) return -1;

    /* --- libmain --- */
    snprintf(path, sizeof(path), "%s/libmain.so", DEAD_EFFECT_LIBDIR);
    rawlog("[boot] lade libmain.so\n");
    LOGI("Lade %s", path);
    g_libmain_handle = so_load(path);
    if (!g_libmain_handle) { LOGE("libmain.so laden fehlgeschlagen"); return -1; }
    rawlog("[boot] libmain.so geladen\n");
    LOGI("libmain.so geladen: %p", g_libmain_handle);
    if (!heap_is_sane("nach libmain")) return -1;
    try_call_onload("libmain", g_libmain_handle);

    /* --- NativeLoader.load: lädt libunity + libil2cpp via dlopen-Hook --- */
    rawlog("[boot] rufe NativeLoader.load\n");
    LOGI("Rufe NativeLoader.load(%s)", DEAD_EFFECT_LIBDIR);
    int rc = jni_call_native_loader(
        "com/unity3d/player/NativeLoader", "load", DEAD_EFFECT_LIBDIR);
    if (rc != 0) {
        LOGE("NativeLoader.load fehlgeschlagen (rc=%d)", rc);
        return -1;
    }
    LOGI("NativeLoader.load OK");
    rawlog("[boot] NativeLoader.load OK\n");

    /* --- Sicherheitsnetz: libil2cpp explizit laden, falls der dlopen-Hook
     *     nicht gegriffen hat. DT-Contract listet load-libil2cpp separat. --- */
    if (!module_loaded("libil2cpp.so")) {
        rawlog("[boot] libil2cpp nicht geladen — lade explizit\n");
        snprintf(path, sizeof(path), "%s/libil2cpp.so", DEAD_EFFECT_LIBDIR);
        g_libil2cpp_handle = so_load(path);
        if (g_libil2cpp_handle) {
            LOGI("libil2cpp.so explizit geladen: %p", g_libil2cpp_handle);
            try_call_onload("libil2cpp", g_libil2cpp_handle);
        } else {
            LOGE("libil2cpp.so konnte nicht geladen werden");
        }
    } else {
        rawlog("[boot] libil2cpp bereits via dlopen geladen\n");
    }
    if (module_loaded("libunity.so")) {
        LOGI("libunity.so in Modulregistry gefunden");
    } else {
        LOGI("WARNUNG: libunity.so nicht in Modulregistry");
    }

    /* --- UnityPlayer.initJni EXPLIZIT aufrufen ---
     *     NativeLoader.load lädt die Bibliotheken, aber initJni wird vom
     *     ursprünglichen Android-Activity separat aufgerufen. Ohne diesen
     *     Aufruf wird UnityMain nie in der JNI-Registry registriert.  */
    const char *UP = "com/unity3d/player/UnityPlayer";
    unity_init_jni = (initJni_t) jni_find_native(UP, "initJni");
    if (unity_init_jni) {
        rawlog("[boot] rufe UnityPlayer.initJni\n");
        LOGI("Rufe UnityPlayer.initJni(env=%p, context=%p)",
             jni_get_env(), g_android_window);
        unity_init_jni(jni_get_env(), g_android_window, NULL);
        LOGI("initJni OK");
        rawlog("[boot] initJni OK\n");
    } else {
        LOGE("initJni nicht in JNI-Registry gefunden!");
    }

    /* --- Jetzt erst UnityMain suchen --- */
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
 * UnityMain-Thread
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

    rawlog("[boot] main() entered\n");
    install_crash_handler();
    rawlog("[boot] crash handler installed\n");
    rawlog("[boot] libdir=");
    rawlog(DEAD_EFFECT_LIBDIR);
    rawlog(" assets=");
    rawlog(DEAD_EFFECT_ASSETS);
    rawlog("\n");

    LOGI("Dead Effect Loader startet");
    LOGI("  libdir = %s", DEAD_EFFECT_LIBDIR);
    LOGI("  assets = %s", DEAD_EFFECT_ASSETS);

    /* JNI-Shim zuerst, damit Capability-Checks ihn nutzen können */
    jni_shim_init();
    rawlog("[boot] jni_shim_init done\n");

    preload_libcxx();
    rawlog("[boot] preload_libcxx done\n");

    /* Video/EGL zuerst, damit graphics.* Capabilities prüfbar sind */
    rawlog("[boot] video_init...\n");
    if (video_init() != 0) { rawlog("[boot] video_init failed\n"); return 1; }
    rawlog("[boot] video_init done\n");

    /* Capability Preflight (aus DT nxport.json) */
    capability_check();

    rawlog("[boot] load_module_chain...\n");
    if (load_module_chain() != 0) { rawlog("[boot] load_module_chain failed\n"); return 1; }
    rawlog("[boot] load_module_chain done\n");

    jni_shim_set_egl(egl_dpy, egl_surf, egl_ctx);

    pthread_t t;
    if (pthread_create(&t, NULL, unity_main_thread, NULL) != 0) {
        LOGE("pthread_create für UnityMain fehlgeschlagen");
     
