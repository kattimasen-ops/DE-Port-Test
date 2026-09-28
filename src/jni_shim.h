#ifndef JNI_SHIM_H
#define JNI_SHIM_H

#include <SDL2/SDL.h>
#include <EGL/egl.h>

void  jni_shim_init(void);
void  jni_shim_set_egl(EGLDisplay d, EGLSurface s, EGLContext c);
void  jni_shim_handle_sdl_event(SDL_Event *ev);

void *jni_get_env(void);
void *jni_make_fake_context(void);
int   jni_call_native_loader(const char *cls, const char *method, const char *arg);

#endif /* JNI_SHIM_H */
