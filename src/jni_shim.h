#ifndef DE_JNI_SHIM_H
#define DE_JNI_SHIM_H
#include <EGL/egl.h>

void  jni_shim_init(void);
void *jni_get_env(void);
void *jni_make_fake_context(void);
int   jni_call_native_loader(const char *cls, const char *method, const char *arg);
void  jni_shim_set_egl(EGLDisplay, EGLSurface, EGLContext);
void  jni_shim_handle_sdl_event(void *ev);

#endif
