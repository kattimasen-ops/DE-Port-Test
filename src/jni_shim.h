#ifndef JNI_SHIM_H
#define JNI_SHIM_H

#include <SDL2/SDL.h>
#include <EGL/egl.h>

void  jni_shim_init(void);
void  jni_shim_set_egl(EGLDisplay d, EGLSurface s, EGLContext c);
void  jni_shim_handle_sdl_event(SDL_Event *ev);

/* Liefert einen Zeiger auf einen FAKE JNIEnv / JavaVM.
 * Beide haben eine gueltige Function-Table, damit aufrufender Code
 * (JNI_OnLoad etc.) nicht crasht. */
void *jni_get_env(void);
void *jni_get_vm(void);

int   jni_call_native_loader(const char *cls, const char *method, const char *arg);

#endif /* JNI_SHIM_H */
