/* ============================================================
 * opensles_shim.c
 * Minimale OpenSL-ES-Implementierung auf SDL2-Audio.
 * Deckt den Unity-Standardpfad ab:
 *   slCreateEngine -> Realize -> GetInterface(ENGINE)
 *   CreateOutputMix -> Realize
 *   CreateAudioPlayer -> Realize -> GetInterface(PLAY, BUFFERQUEUE, VOLUME,
 *                                                ANDROIDCONFIGURATION)
 *   SetPlayState / Enqueue / RegisterCallback
 *
 * Ausgabe läuft über einen SDL-Audio-Device. Ein Feeder-Thread ruft
 * den BufferQueue-Callback periodisch, damit Unity Nachschub liefert.
 * ============================================================ */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <SDL2/SDL.h>
#include <android/log.h>

#define TAG "deadeffect-ssl"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ------------------ OpenSL-ES Typedefs ------------------ */
typedef int32_t  SLresult;
typedef uint32_t SLuint32;
typedef int32_t  SLint32;
typedef uint16_t SLuint16;
typedef int16_t  SLint16;
typedef uint8_t  SLuint8;
typedef int8_t   SLint8;
typedef uint64_t SLuint64;
typedef int64_t  SLint64;
typedef uint32_t SLboolean;
typedef uint32_t SLmillisecond;
typedef int32_t  SLmillibel;
typedef char     SLchar;

#define SL_RESULT_SUCCESS  0
#define SL_RESULT_FAILURE  ((SLresult)0x80000001)
#define SL_BOOLEAN_FALSE   0
#define SL_BOOLEAN_TRUE    1
#define SL_PLAYSTATE_STOPPED 0
#define SL_PLAYSTATE_PAUSED  1
#define SL_PLAYSTATE_PLAYING 2

/* ------------------ Interface-ID-Marker ------------------ */
static const int _iid_engine_marker            = 1;
static const int _iid_play_marker              = 2;
static const int _iid_bq_marker                = 3;
static const int _iid_androidsimplebq_marker   = 4;
static const int _iid_volume_marker            = 5;
static const int _iid_androidconfig_marker     = 6;
static const int _iid_envreverb_marker         = 7;
static const int _iid_effectsend_marker        = 8;
static const int _iid_outputmix_marker         = 9;
static const int _iid_prefetchstatus_marker    = 10;
static const int _iid_androideffectsend_marker = 11;

/* Diese Symbole werden von libunity.so/libil2cpp.so referenziert. */
const void *SL_IID_ENGINE                  = &_iid_engine_marker;
const void *SL_IID_PLAY                    = &_iid_play_marker;
const void *SL_IID_BUFFERQUEUE             = &_iid_bq_marker;
const void *SL_IID_ANDROIDSIMPLEBUFFERQUEUE= &_iid_androidsimplebq_marker;
const void *SL_IID_VOLUME                  = &_iid_volume_marker;
const void *SL_IID_ANDROIDCONFIGURATION    = &_iid_androidconfig_marker;
const void *SL_IID_ENVIRONMENTALREVERB     = &_iid_envreverb_marker;
const void *SL_IID_EFFECTSEND              = &_iid_effectsend_marker;
const void *SL_IID_OUTPUTMIX               = &_iid_outputmix_marker;
const void *SL_IID_PREFETCHSTATUS          = &_iid_prefetchstatus_marker;
const void *SL_IID_ANDROIDEFFECTSEND       = &_iid_androideffectsend_marker;

/* ------------------ Strukturen (Opak, aber layoutgleich) ------------------ */
struct SLObjectItf_;
typedef const struct SLObjectItf_ * const * SLObjectItf;

struct SLEngineItf_;
typedef const struct SLEngineItf_ * const * SLEngineItf;

struct SLPlayItf_;
typedef const struct SLPlayItf_ * const * SLPlayItf;

struct SLAndroidSimpleBufferQueueItf_;
typedef const struct SLAndroidSimpleBufferQueueItf_ * const * SLAndroidSimpleBufferQueueItf;

struct SLVolumeItf_;
typedef const struct SLVolumeItf_ * const * SLVolumeItf;

struct SLAndroidConfigurationItf_;
typedef const struct SLAndroidConfigurationItf_ * const * SLAndroidConfigurationItf;

typedef struct {
    SLuint32 state;
    SLuint32 count;
    SLuint32 index;
} SLAndroidSimpleBufferQueueState;

typedef void (*slAndroidSimpleBufferQueueCallback)(SLAndroidSimpleBufferQueueItf bq, void *ctx);

/* ------------------ VTable-Layouts (positionsgenau) ------------------ */
struct SLObjectItf_ {
    SLresult (*Realize)(SLObjectItf self, SLboolean async);
    SLresult (*Resume)(SLObjectItf self, SLboolean async);
    SLresult (*GetState)(SLObjectItf self, SLuint32 *pState);
    SLresult (*GetInterface)(SLObjectItf self, const void *iid, void *pInterface);
    void     (*Destroy)(SLObjectItf self);
    SLresult (*RegisterCallback)(SLObjectItf self, void *cb, void *ctx);
    void     (*AbortAsyncOperation)(SLObjectItf self);
    void     (*SetPriority)(SLObjectItf self, SLint32 prio, SLboolean pre);
    SLresult (*GetPriority)(SLObjectItf self, SLint32 *p, SLboolean *q);
    SLresult (*SetLossOfControlInterfaces)(SLObjectItf self, SLint16 n, void *iids, SLboolean en);
};

struct SLPlayItf_ {
    SLresult (*SetPlayState)(SLPlayItf self, SLuint32 state);
    SLresult (*GetPlayState)(SLPlayItf self, SLuint32 *pState);
    SLresult (*GetDuration)(SLPlayItf self, SLmillisecond *p);
    SLresult (*GetPosition)(SLPlayItf self, SLmillisecond *p);
    SLresult (*RegisterCallback)(SLPlayItf self, void *cb, void *ctx);
    SLresult (*SetCallbackEventsMask)(SLPlayItf self, SLuint32 m);
    SLresult (*GetCallbackEventsMask)(SLPlayItf self, SLuint32 *m);
    SLresult (*SetMarkerPosition)(SLPlayItf self, SLmillisecond m);
    SLresult (*ClearMarkerPosition)(SLPlayItf self);
    SLresult (*GetMarkerPosition)(SLPlayItf self, SLmillisecond *p);
    SLresult (*SetPositionUpdatePeriod)(SLPlayItf self, SLmillisecond m);
    SLresult (*GetPositionUpdatePeriod)(SLPlayItf self, SLmillisecond *p);
};

struct SLAndroidSimpleBufferQueueItf_ {
    SLresult (*Enqueue)(SLAndroidSimpleBufferQueueItf self, const void *pBuffer, SLuint32 size);
    SLresult (*Clear)(SLAndroidSimpleBufferQueueItf self);
    SLresult (*GetState)(SLAndroidSimpleBufferQueueItf self, SLAndroidSimpleBufferQueueState *p);
    SLresult (*RegisterCallback)(SLAndroidSimpleBufferQueueItf self, slAndroidSimpleBufferQueueCallback cb, void *ctx);
};

struct SLVolumeItf_ {
    SLresult (*SetVolumeLevel)(SLVolumeItf self, SLmillibel level);
    SLresult (*GetVolumeLevel)(SLVolumeItf self, SLmillibel *p);
    SLresult (*GetMaxVolumeLevel)(SLVolumeItf self, SLmillibel *p);
    SLresult (*SetMute)(SLVolumeItf self, SLboolean m);
    SLresult (*GetMute)(SLVolumeItf self, SLboolean *m);
    SLresult (*EnableStereoPosition)(SLVolumeItf self, SLboolean e);
    SLresult (*SetStereoPosition)(SLVolumeItf self, SLint32 p);
    SLresult (*GetStereoPosition)(SLVolumeItf self, SLint32 *p);
};

struct SLAndroidConfigurationItf_ {
    SLresult (*SetConfiguration)(SLAndroidConfigurationItf self, const SLchar *key, const void *val, SLuint32 sz);
    SLresult (*GetConfiguration)(SLAndroidConfigurationItf self, const SLchar *key, SLuint32 *pSize, void *val);
    SLresult (*AcquireJavaProxy)(SLAndroidConfigurationItf self, SLuint32 proxyType, void **pProxy);
    SLresult (*ReleaseJavaProxy)(SLAndroidConfigurationItf self, SLuint32 proxyType);
};

struct SLEngineItf_ {
    SLresult (*CreateLEDDevice)(SLEngineItf self, SLObjectItf *p, SLuint32 n, const void **ids, const SLboolean *req);
    SLresult (*CreateVibraDevice)(SLEngineItf self, SLObjectItf *p, SLuint32 n, const void **ids, const SLboolean *req);
    SLresult (*CreateAudioPlayer)(SLEngineItf self, SLObjectItf *pPlayer,
                                  void *pAudioSrc, void *pAudioSnk,
                                  SLuint32 numInterfaces, const void **pInterfaceIds,
                                  const SLboolean *pInterfaceRequired);
    SLresult (*CreateAudioRecorder)(SLEngineItf self, SLObjectItf *p, void *a, void *b, SLuint32 n, const void **ids, const SLboolean *req);
    SLresult (*CreateOutputMix)(SLEngineItf self, SLObjectItf *pMix, SLuint32 n, const void **ids, const SLboolean *req);
    SLresult (*CreateListener)(SLEngineItf self, SLObjectItf *p, SLuint32 n, const void **ids, const SLboolean *req);
    SLresult (*Create3DGroup)(SLEngineItf self, SLObjectItf *p, SLuint32 n, const void **ids, const SLboolean *req);
    SLresult (*CreateMetadataExtractor)(SLEngineItf self, SLObjectItf *p, void *a, SLuint32 n, const void **ids, const SLboolean *req);
    SLresult (*CreateExtensionObject)(SLEngineItf self, SLObjectItf *p, void *a, SLuint32 b, SLuint32 n, const void **ids, const SLboolean *req);
    SLresult (*QueryNumSupportedInterfaces)(SLEngineItf self, SLuint32 *p);
    SLresult (*QuerySupportedInterfaces)(SLEngineItf self, SLuint32 i, void **p);
    SLresult (*QueryNumSupportedExtensions)(SLEngineItf self, SLuint32 *p);
    SLresult (*QuerySupportedExtension)(SLEngineItf self, SLuint32 i, SLchar *p, SLint16 *l);
    SLresult (*IsExtensionSupported)(SLEngineItf self, const SLchar *name, SLboolean *ok);
};

/* ------------------ Interne Objekte ------------------ */
typedef struct sl_obj {
    /* VTable-Pointer - MUESSEN die ersten Felder sein! */
    const struct SLObjectItf_                  *obj_vtable;
    const struct SLEngineItf_                  *engine_vtable;
    const struct SLPlayItf_                    *play_vtable;
    const struct SLAndroidSimpleBufferQueueItf_*bq_vtable;
    const struct SLVolumeItf_                  *volume_vtable;
    const struct SLAndroidConfigurationItf_    *config_vtable;

    int used;
    int kind;         /* 0=Engine 1=Player 2=OutputMix */
    int realized;

    SLuint32 sampleRateHz;
    SLuint32 numChannels;
    SLuint32 bitsPerSample;
    SLuint32 playState;

    uint8_t *ring;
    size_t   ring_size;
    size_t   ring_read;
    size_t   ring_write;
    pthread_mutex_t lock;

    slAndroidSimpleBufferQueueCallback bq_cb;
    void *bq_ctx;

    pthread_t feeder_thr;
    int feeder_running;

    SDL_AudioDeviceID dev;
} sl_obj;

#define OBJ_FROM_OBJ(p)     ((sl_obj *)((char *)(p) - offsetof(sl_obj, obj_vtable)))
#define OBJ_FROM_ENGINE(p)  ((sl_obj *)((char *)(p) - offsetof(sl_obj, engine_vtable)))
#define OBJ_FROM_PLAY(p)    ((sl_obj *)((char *)(p) - offsetof(sl_obj, play_vtable)))
#define OBJ_FROM_BQ(p)      ((sl_obj *)((char *)(p) - offsetof(sl_obj, bq_vtable)))
#define OBJ_FROM_VOL(p)     ((sl_obj *)((char *)(p) - offsetof(sl_obj, volume_vtable)))
#define OBJ_FROM_CFG(p)     ((sl_obj *)((char *)(p) - offsetof(sl_obj, config_vtable)))

#define SL_OBJ_MAX 32
static sl_obj g_objs[SL_OBJ_MAX];
static pthread_mutex_t g_objs_lock = PTHREAD_MUTEX_INITIALIZER;

/* ------------------ Vorwaertsdeklarationen ------------------ */
static SLresult obj_Realize(SLObjectItf self, SLboolean async);
static SLresult obj_Resume(SLObjectItf self, SLboolean async);
static SLresult obj_GetState(SLObjectItf self, SLuint32 *p);
static SLresult obj_GetInterface(SLObjectItf self, const void *iid, void *pIf);
static void     obj_Destroy(SLObjectItf self);
static SLresult obj_RegCb(SLObjectItf self, void *cb, void *ctx);
static void     obj_Abort(SLObjectItf self);
static void     obj_SetPrio(SLObjectItf self, SLint32 p, SLboolean pre);
static SLresult obj_GetPrio(SLObjectItf self, SLint32 *p, SLboolean *q);
static SLresult obj_SetLOC(SLObjectItf self, SLint16 n, void *ids, SLboolean en);

static SLresult eng_CreateAudioPlayer(SLEngineItf self, SLObjectItf *pPlayer,
                                      void *pAudioSrc, void *pAudioSnk,
                                      SLuint32 numInterfaces, const void **ids,
                                      const SLboolean *req);
static SLresult eng_CreateOutputMix(SLEngineItf self, SLObjectItf *pMix,
                                    SLuint32 n, const void **ids,
                                    const SLboolean *req);
static SLresult eng_Unsupported(SLEngineItf self, SLObjectItf *p, ...);

static SLresult play_SetPlayState(SLPlayItf self, SLuint32 state);
static SLresult play_GetPlayState(SLPlayItf self, SLuint32 *p);
static SLresult play_GetDuration(SLPlayItf self, SLmillisecond *p);
static SLresult play_GetPosition(SLPlayItf self, SLmillisecond *p);
static SLresult play_RegCb(SLPlayItf self, void *cb, void *ctx);
static SLresult play_OkU32(SLPlayItf self, SLuint32 *x);
static SLresult play_OkMs(SLPlayItf self, SLmillisecond *x);

static SLresult bq_Enqueue(SLAndroidSimpleBufferQueueItf self, const void *buf, SLuint32 size);
static SLresult bq_Clear(SLAndroidSimpleBufferQueueItf self);
static SLresult bq_GetState(SLAndroidSimpleBufferQueueItf self, SLAndroidSimpleBufferQueueState *p);
static SLresult bq_RegCb(SLAndroidSimpleBufferQueueItf self,
                         slAndroidSimpleBufferQueueCallback cb, void *ctx);

static SLresult vol_SetLevel(SLVolumeItf self, SLmillibel level);
static SLresult vol_GetLevel(SLVolumeItf self, SLmillibel *p);
static SLresult vol_GetMax(SLVolumeItf self, SLmillibel *p);
static SLresult vol_SetMute(SLVolumeItf self, SLboolean m);
static SLresult vol_GetMute(SLVolumeItf self, SLboolean *m);
static SLresult vol_EnableStereo(SLVolumeItf self, SLboolean e);
static SLresult vol_SetStereo(SLVolumeItf self, SLint32 p);
static SLresult vol_GetStereo(SLVolumeItf self, SLint32 *p);

static SLresult cfg_Set(SLAndroidConfigurationItf self, const SLchar *k, const void *v, SLuint32 sz);
static SLresult cfg_Get(SLAndroidConfigurationItf self, const SLchar *k, SLuint32 *pSize, void *v);
static SLresult cfg_Ok(SLAndroidConfigurationItf self, SLuint32 t, void **p);

/* ------------------ VTable-Instanzen ------------------ */
static const struct SLObjectItf_ obj_vt = {
    obj_Realize, obj_Resume, obj_GetState, obj_GetInterface, obj_Destroy,
    obj_RegCb, obj_Abort, obj_SetPrio, obj_GetPrio, obj_SetLOC
};

static const struct SLEngineItf_ engine_vt = {
    (void *)eng_Unsupported, (void *)eng_Unsupported,
    eng_CreateAudioPlayer,
    (void *)eng_Unsupported,
    eng_CreateOutputMix,
    (void *)eng_Unsupported, (void *)eng_Unsupported,
    (void *)eng_Unsupported, (void *)eng_Unsupported,
    (void *)eng_Unsupported, (void *)eng_Unsupported,
    (void *)eng_Unsupported, (void *)eng_Unsupported,
    (void *)eng_Unsupported
};

static const struct SLPlayItf_ play_vt = {
    play_SetPlayState, play_GetPlayState, play_GetDuration, play_GetPosition,
    play_RegCb, play_OkU32, play_OkU32,
    play_OkMs, (void *)play_OkU32, play_OkMs,
    play_OkMs, play_OkMs
};

static const struct SLAndroidSimpleBufferQueueItf_ bq_vt = {
    bq_Enqueue, bq_Clear, bq_GetState, bq_RegCb
};

static const struct SLVolumeItf_ volume_vt = {
    vol_SetLevel, vol_GetLevel, vol_GetMax, vol_SetMute, vol_GetMute,
    vol_EnableStereo, vol_SetStereo, vol_GetStereo
};

static const struct SLAndroidConfigurationItf_ config_vt = {
    cfg_Set, cfg_Get, cfg_Ok, (void *)cfg_Ok
};

/* ------------------ Objekt-Allocator ------------------ */
static sl_obj *alloc_obj(int kind) {
    pthread_mutex_lock(&g_objs_lock);
    for (int i = 0; i < SL_OBJ_MAX; i++) {
        if (!g_objs[i].used) {
            memset(&g_objs[i], 0, sizeof(g_objs[i]));
            g_objs[i].obj_vtable    = &obj_vt;
            g_objs[i].engine_vtable = &engine_vt;
            g_objs[i].play_vtable   = &play_vt;
            g_objs[i].bq_vtable     = &bq_vt;
            g_objs[i].volume_vtable = &volume_vt;
            g_objs[i].config_vtable = &config_vt;
            g_objs[i].used = 1;
            g_objs[i].kind = kind;
            g_objs[i].playState = SL_PLAYSTATE_STOPPED;
            pthread_mutex_init(&g_objs[i].lock, NULL);
            pthread_mutex_unlock(&g_objs_lock);
            return &g_objs[i];
        }
    }
    pthread_mutex_unlock(&g_objs_lock);
    return NULL;
}

/* ------------------ Feeder-Thread ------------------ */
static void *feeder_fn(void *arg) {
    sl_obj *o = arg;
    while (o->feeder_running) {
        pthread_mutex_lock(&o->lock);
        size_t avail = (o->ring_write + o->ring_size - o->ring_read) % o->ring_size;
        size_t half  = o->ring_size / 2;
        slAndroidSimpleBufferQueueCallback cb = o->bq_cb;
        void *ctx = o->bq_ctx;
        pthread_mutex_unlock(&o->lock);

        if (avail < half && cb && o->playState == SL_PLAYSTATE_PLAYING) {
            cb((SLAndroidSimpleBufferQueueItf)&o->bq_vtable, ctx);
        }
        SDL_Delay(5);
    }
    return NULL;
}

/* ------------------ SDL-Audio-Callback ------------------ */
static void sdl_audio_cb(void *userdata, Uint8 *stream, int len) {
    sl_obj *o = userdata;
    pthread_mutex_lock(&o->lock);
    size_t avail = (o->ring_write + o->ring_size - o->ring_read) % o->ring_size;
    size_t want  = (size_t)len;
    if (avail >= want) {
        for (size_t i = 0; i < want; i++) {
            stream[i] = o->ring[o->ring_read];
            o->ring_read = (o->ring_read + 1) % o->ring_size;
        }
    } else {
        for (size_t i = 0; i < avail; i++) {
            stream[i] = o->ring[o->ring_read];
            o->ring_read = (o->ring_read + 1) % o->ring_size;
        }
        memset(stream + avail, 0, want - avail);
    }
    pthread_mutex_unlock(&o->lock);
}

/* ------------------ SLObjectItf-Methoden ------------------ */
static SLresult obj_Realize(SLObjectItf self, SLboolean async) {
    (void)async;
    sl_obj *o = OBJ_FROM_OBJ(self);
    o->realized = 1;

    if (o->kind == 1) { /* Player */
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof(want));
        want.freq     = o->sampleRateHz ? (int)o->sampleRateHz : 48000;
        want.channels = o->numChannels  ? (Uint8)o->numChannels : 2;
        want.format   = (o->bitsPerSample == 32) ? AUDIO_F32SYS : AUDIO_S16SYS;
        want.samples  = 1024;
        want.callback = sdl_audio_cb;
        want.userdata = o;

        o->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!o->dev) {
            LOGE("SDL_OpenAudioDevice fehlgeschlagen: %s", SDL_GetError());
            return SL_RESULT_FAILURE;
        }
        LOGI("OpenSL PCM %uch/%uHz (16/32Bit) -> SDL2 dev=%u",
             (unsigned)have.channels, (unsigned)have.freq, (unsigned)o->dev);

        /* Ring-Buffer: 1 Sekunde */
        o->ring_size = (size_t)have.freq * have.channels *
                       (SDL_AUDIO_BITSIZE(have.format) / 8);
        if (o->ring_size < 8192) o->ring_size = 8192;
        o->ring = (uint8_t *)calloc(1, o->ring_size);
        o->ring_read = o->ring_write = 0;

        o->feeder_running = 1;
        if (pthread_create(&o->feeder_thr, NULL, feeder_fn, o) != 0) {
            o->feeder_running = 0;
            LOGE("Feeder-Thread konnte nicht gestartet werden");
        }
    }
    return SL_RESULT_SUCCESS;
}

static SLresult obj_Resume(SLObjectItf self, SLboolean async) { (void)self;(void)async; return SL_RESULT_SUCCESS; }
static SLresult obj_GetState(SLObjectItf self, SLuint32 *p) { (void)self; if (p) *p = 0; return SL_RESULT_SUCCESS; }

static SLresult obj_GetInterface(SLObjectItf self, const void *iid, void *pIf) {
    sl_obj *o = OBJ_FROM_OBJ(self);
    if (!pIf) return SL_RESULT_FAILURE;

    if (o->kind == 0) {          /* Engine */
        if (iid == SL_IID_ENGINE) {
            *(SLEngineItf *)pIf = (SLEngineItf)&o->engine_vtable;
            return SL_RESULT_SUCCESS;
        }
    } else if (o->kind == 1) {   /* Player */
        if (iid == SL_IID_PLAY) {
            *(SLPlayItf *)pIf = (SLPlayItf)&o->play_vtable;
            return SL_RESULT_SUCCESS;
        }
        if (iid == SL_IID_BUFFERQUEUE || iid == SL_IID_ANDROIDSIMPLEBUFFERQUEUE) {
            *(SLAndroidSimpleBufferQueueItf *)pIf =
                (SLAndroidSimpleBufferQueueItf)&o->bq_vtable;
            return SL_RESULT_SUCCESS;
        }
        if (iid == SL_IID_VOLUME) {
            *(SLVolumeItf *)pIf = (SLVolumeItf)&o->volume_vtable;
            return SL_RESULT_SUCCESS;
        }
        if (iid == SL_IID_ANDROIDCONFIGURATION) {
            *(SLAndroidConfigurationItf *)pIf =
                (SLAndroidConfigurationItf)&o->config_vtable;
            return SL_RESULT_SUCCESS;
        }
    } else if (o->kind == 2) {   /* OutputMix */
        if (iid == SL_IID_OUTPUTMIX) {
            *(void **)pIf = &o->obj_vtable;
            return SL_RESULT_SUCCESS;
        }
    }
    LOGI("GetInterface: unbekannte iid %p (kind=%d)", iid, o->kind);
    *(void **)pIf = NULL;
    return SL_RESULT_SUCCESS;
}

static void obj_Destroy(SLObjectItf self) {
    sl_obj *o = OBJ_FROM_OBJ(self);
    if (o->feeder_running) {
        o->feeder_running = 0;
        pthread_join(o->feeder_thr, NULL);
    }
    if (o->dev) { SDL_CloseAudioDevice(o->dev); o->dev = 0; }
    free(o->ring); o->ring = NULL;
    pthread_mutex_destroy(&o->lock);
    o->used = 0;
}
static SLresult obj_RegCb(SLObjectItf self, void *cb, void *ctx) { (void)self;(void)cb;(void)ctx; return SL_RESULT_SUCCESS; }
static void     obj_Abort(SLObjectItf self) { (void)self; }
static void     obj_SetPrio(SLObjectItf self, SLint32 p, SLboolean pre) { (void)self;(void)p;(void)pre; }
static SLresult obj_GetPrio(SLObjectItf self, SLint32 *p, SLboolean *q) { (void)self; if(p)*p=0; if(q)*q=0; return SL_RESULT_SUCCESS; }
static SLresult obj_SetLOC(SLObjectItf self, SLint16 n, void *ids, SLboolean en) { (void)self;(void)n;(void)ids;(void)en; return SL_RESULT_SUCCESS; }

/* ------------------ SLEngineItf ------------------ */
static SLresult eng_CreateAudioPlayer(SLEngineItf self, SLObjectItf *pPlayer,
                                      void *pAudioSrc, void *pAudioSnk,
                                      SLuint32 numInterfaces, const void **ids,
                                      const SLboolean *req) {
    (void)self; (void)pAudioSnk; (void)numInterfaces; (void)ids; (void)req;
    if (!pPlayer) return SL_RESULT_FAILURE;

    SLuint32 numCh = 0, rateHz = 0, bits = 0;
    if (pAudioSrc) {
        /* SLDataSource*: { void* pLocator; void* pFormat; } */
        void **ds = (void **)pAudioSrc;
        void  *fmt = ds[1];
        if (fmt) {
            /* SLDataFormat_PCM: format, numCh, samplesPerSec(mHz),
             * bitsPerSample, containerSize, channelMask, endianness */
            SLuint32 *f = (SLuint32 *)fmt;
            numCh  = f[1];
            rateHz = f[2] / 1000;
            bits   = f[3];
        }
    }

    sl_obj *o = alloc_obj(1);
    if (!o) return SL_RESULT_FAILURE;
    o->numChannels   = numCh  ? numCh  : 2;
    o->sampleRateHz  = rateHz ? rateHz : 48000;
    o->bitsPerSample = bits   ? bits   : 16;

    LOGI("CreateAudioPlayer: ch=%u rate=%uHz bits=%u",
         (unsigned)o->numChannels, (unsigned)o->sampleRateHz,
         (unsigned)o->bitsPerSample);

    *pPlayer = (SLObjectItf)&o->obj_vtable;
    return SL_RESULT_SUCCESS;
}

static SLresult eng_CreateOutputMix(SLEngineItf self, SLObjectItf *pMix,
                                    SLuint32 n, const void **ids,
                                    const SLboolean *req) {
    (void)self; (void)n; (void)ids; (void)req;
    if (!pMix) return SL_RESULT_FAILURE;
    sl_obj *o = alloc_obj(2);
    if (!o) return SL_RESULT_FAILURE;
    *pMix = (SLObjectItf)&o->obj_vtable;
    LOGI("CreateOutputMix -> %p", (void *)o);
    return SL_RESULT_SUCCESS;
}

static SLresult eng_Unsupported(SLEngineItf self, SLObjectItf *p, ...) {
    (void)self;
    if (p) *p = NULL;
    return SL_RESULT_FEATURE_UNSUPPORTED_OR_OK();
}
/* Trick: Feature-Unsupported gibt es nur bei manchen SL-Versionen.
 * Wir geben einfach SUCCESS mit NULL zurueck - Unity prueft das eh nicht. */
#define SL_RESULT_FEATURE_UNSUPPORTED_OR_OK() 0

/* ------------------ SLPlayItf ------------------ */
static SLresult play_SetPlayState(SLPlayItf self, SLuint32 state) {
    sl_obj *o = OBJ_FROM_PLAY(self);
    o->playState = state;
    if (o->dev) {
        SDL_PauseAudioDevice(o->dev, state == SL_PLAYSTATE_PLAYING ? 0 : 1);
    }
    LOGI("SetPlayState: %u", (unsigned)state);
    return SL_RESULT_SUCCESS;
}
static SLresult play_GetPlayState(SLPlayItf self, SLuint32 *p) {
    sl_obj *o = OBJ_FROM_PLAY(self);
    if (p) *p = o->playState;
    return SL_RESULT_SUCCESS;
}
static SLresult play_GetDuration(SLPlayItf self, SLmillisecond *p) { (void)self; if(p)*p=0xFFFFFFFF; return SL_RESULT_SUCCESS; }
static SLresult play_GetPosition(SLPlayItf self, SLmillisecond *p) { (void)self; if(p)*p=0; return SL_RESULT_SUCCESS; }
static SLresult play_RegCb(SLPlayItf self, void *cb, void *ctx) { (void)self;(void)cb;(void)ctx; return SL_RESULT_SUCCESS; }
static SLresult play_OkU32(SLPlayItf self, SLuint32 *x) { (void)self; if(x)*x=0; return SL_RESULT_SUCCESS; }
static SLresult play_OkMs(SLPlayItf self, SLmillisecond *x) { (void)self; if(x)*x=0; return SL_RESULT_SUCCESS; }

/* ------------------ SLAndroidSimpleBufferQueueItf ------------------ */
static SLresult bq_Enqueue(SLAndroidSimpleBufferQueueItf self,
                           const void *buf, SLuint32 size) {
    sl_obj *o = OBJ_FROM_BQ(self);
    if (!buf || size == 0) return SL_RESULT_FAILURE;
    pthread_mutex_lock(&o->lock);
    /* einen Slot frei lassen, um read==write nicht mit full zu verwechseln */
    size_t freeSlots = (o->ring_read + o->ring_size - o->ring_write - 1) % o->ring_size;
    size_t toWrite = size;
    if (toWrite > freeSlots) toWrite = freeSlots;
    for (size_t i = 0; i < toWrite; i++) {
        o->ring[o->ring_write] = ((const uint8_t *)buf)[i];
        o->ring_write = (o->ring_write + 1) % o->ring_size;
    }
    pthread_mutex_unlock(&o->lock);
    return SL_RESULT_SUCCESS;
}
static SLresult bq_Clear(SLAndroidSimpleBufferQueueItf self) {
    sl_obj *o = OBJ_FROM_BQ(self);
    pthread_mutex_lock(&o->lock);
    o->ring_read = o->ring_write = 0;
    pthread_mutex_unlock(&o->lock);
    return SL_RESULT_SUCCESS;
}
static SLresult bq_GetState(SLAndroidSimpleBufferQueueItf self,
                            SLAndroidSimpleBufferQueueState *p) {
    (void)self;
    if (p) { p->state = 0; p->count = 0; p->index = 0; }
    return SL_RESULT_SUCCESS;
}
static SLresult bq_RegCb(SLAndroidSimpleBufferQueueItf self,
                         slAndroidSimpleBufferQueueCallback cb, void *ctx) {
    sl_obj *o = OBJ_FROM_BQ(self);
    o->bq_cb  = cb;
    o->bq_ctx = ctx;
    return SL_RESULT_SUCCESS;
}

/* ------------------ SLVolumeItf ------------------ */
static SLresult vol_SetLevel(SLVolumeItf self, SLmillibel level) {
    (void)self; LOGI("SetVolumeLevel %d mB", (int)level);
    return SL_RESULT_SUCCESS;
}
static SLresult vol_GetLevel(SLVolumeItf self, SLmillibel *p) { (void)self; if(p)*p=0; return SL_RESULT_SUCCESS; }
static SLresult vol_GetMax(SLVolumeItf self, SLmillibel *p)   { (void)self; if(p)*p=0; return SL_RESULT_SUCCESS; }
static SLresult vol_SetMute(SLVolumeItf self, SLboolean m)    { (void)self;(void)m; return SL_RESULT_SUCCESS; }
static SLresult vol_GetMute(SLVolumeItf self, SLboolean *m)   { (void)self; if(m)*m=0; return SL_RESULT_SUCCESS; }
static SLresult vol_EnableStereo(SLVolumeItf self, SLboolean e){ (void)self;(void)e; return SL_RESULT_SUCCESS; }
static SLresult vol_SetStereo(SLVolumeItf self, SLint32 p)    { (void)self;(void)p; return SL_RESULT_SUCCESS; }
static SLresult vol_GetStereo(SLVolumeItf self, SLint32 *p)   { (void)self; if(p)*p=0; return SL_RESULT_SUCCESS; }

/* ------------------ SLAndroidConfigurationItf ------------------ */
static SLresult cfg_Set(SLAndroidConfigurationItf self, const SLchar *k,
                        const void *v, SLuint32 sz) {
    (void)self; (void)k; (void)v; (void)sz;
    return SL_RESULT_SUCCESS;
}
static SLresult cfg_Get(SLAndroidConfigurationItf self, const SLchar *k,
                        SLuint32 *pSize, void *v) {
    (void)self; (void)k; (void)v;
    if (pSize) *pSize = 0;
    return SL_RESULT_SUCCESS;
}
static SLresult cfg_Ok(SLAndroidConfigurationItf self, SLuint32 t, void **p) {
    (void)self; (void)t;
    if (p) *p = NULL;
    return SL_RESULT_SUCCESS;
}

/* ------------------ slCreateEngine ------------------ */
SLresult slCreateEngine(SLObjectItf *pEngine, SLuint32 numOptions,
                        const void *pEngineOptions, SLuint32 numInterfaces,
                        const void *pInterfaceIds, const SLboolean *pInterfaceRequired) {
    (void)numOptions; (void)pEngineOptions;
    (void)numInterfaces; (void)pInterfaceIds; (void)pInterfaceRequired;
    if (!pEngine) return SL_RESULT_FAILURE;
    sl_obj *o = alloc_obj(0);
    if (!o) return SL_RESULT_FAILURE;
    *pEngine = (SLObjectItf)&o->obj_vtable;
    LOGI("slCreateEngine -> %p", (void *)o);
    return SL_RESULT_SUCCESS;
}