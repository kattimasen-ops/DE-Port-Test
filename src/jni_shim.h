#ifndef JNI_SHIM_H
#define JNI_SHIM_H

#include <stdint.h>
#include <stddef.h>
#include <SDL2/SDL.h>
#include <EGL/egl.h>

/* ---- Lifecycle ---- */
void  jni_shim_init(void);
void  jni_shim_set_egl(EGLDisplay d, EGLSurface s, EGLContext c);
void  jni_shim_handle_sdl_event(SDL_Event *ev);

/* ---- VM / Env Access ---- */
void *jni_get_env(void);
void *jni_get_vm(void);
void *jni_get_activity(void);

/* ---- Native Method Registry ---- */
void  jni_dump_natives(void);
void *jni_find_native(const char *class_name, const char *method_name);
void *jni_find_native_signature(const char *signature);

/* ---- UI Task Pump (required by Unity HandlerThread) ---- */
void  jni_pump_ui_tasks(void);

/* ---- Native Loader Bridge ---- */
int   jni_call_native_loader(const char *cls, const char *method, const char *arg);

/* ---- Android Contract Install (UnityPlayerActivity compatibility) ---- */
void  jni_install_android_contract(void);

#endif
