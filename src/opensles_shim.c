/* opensles_shim.c — kompakte Version
 * OpenSL-ES auf SDL2-Audio. Deckt den Unity-Standardpfad ab.
 * Enthaelt alle SL_IID-Marker aus den Switch-Ports (phigros_nx etc.).
 */
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

/* Alle SL_IID-Marker, die in Unity-Android-Builds referenziert werden.
 * Quelle: Switch-Port-Lineage (phigros_nx, vln_nx, bully-NX). */
static const int _iid_3DCOMMIT               = 0x01;
static const int _iid_3DDOPPLER              = 0x02;
static const int _iid_3DGROUPING             = 0x03;
static const int _iid_3DLOCATION             = 0x04;
static const int _iid_3DMACROSCOPIC          = 0x05;
static const int _iid_3DSOURCE               = 0x06;
static const int _iid_ANDROIDCONFIGURATION   = 0x07;
static const int _iid_ANDROIDEFFECT          = 0x08;
static const int _iid_ANDROIDEFFECTCAPABILITIES = 0x09;
static const int _iid_ANDROIDEFFECTSEND      = 0x0A;
static const int _iid_ANDROIDSIMPLEBUFFERQUEUE = 0x0B;
static const int _iid_AUDIODECODERCAPABILITIES = 0x0C;
static const int _iid_AUDIOENCODER           = 0x0D;
static const int _iid_AUDIOENCODERCAPABILITIES = 0x0E;
static const int _iid_AUDIOIODEVICECAPABILITIES = 0x0F;
static const int _iid_BASSBOOST              = 0x10;
static const int _iid_BUFFERQUEUE            = 0x11;
static const int _iid_DEVICEVOLUME           = 0x12;
static const int _iid_DYNAMICINTERFACEMANAGEMENT = 0x13;
static const int _iid_DYNAMICSOURCE          = 0x14;
static const int _iid_EFFECTSEND             = 0x15;
static const int _iid_ENGINE                 = 0x16;
static const int _iid_ENGINECAPABILITIES     = 0x17;
static const int _iid_ENVIRONMENTALREVERB    = 0x18;
static const int _iid_EQUALIZER              = 0x19;
static const int _iid_LEDDEVICE              = 0x1A;
static const int _iid_METADATAEXTRACTION     = 0x1B;
static const int _iid_METADATATRAVERSAL      = 0x1C;
static const int _iid_MIDIMESSAGE            = 0x1D;
static const int _iid_MIDIMUTESOLO           = 0x1E;
static const int _iid_MIDITEMPO              = 0x1F;
static const int _iid_MIDITIME               = 0x20;
static const int _iid_MUTESOLO               = 0x21;
static const int _iid_NULL                   = 0x22;
static const int _iid_OBJECT                 = 0x23;
static const int _iid_OUTPUTMIX              = 0x24;
static const int _iid_PITCH                  = 0x25;
static const int _iid_PLAY                   = 0x26;
static const int _iid_PLAYBACKRATE           = 0x27;
static const int _iid_PREFETCHSTATUS         = 0x28;
static const int _iid_PRESETREVERB           = 0x29;
static const int _iid_RATEPITCH              = 0x2A;
static const int _iid_RECORD                 = 0x2B;
static const int _iid_SEEK                   = 0x2C;
static const int _iid_THREADSYNC             = 0x2D;
static const int _iid_VIBRA                  = 0x2E;
static const int _iid_VIRTUALIZER            = 0x2F;
static const int _iid_VOLUME                 = 0x30;
static const int _iid_OUTPUTMIXEXT           = 0x31;

/* Symbole, die von libunity/libil2cpp importiert werden. */
const void *SL_IID_ENGINE                    = &_iid_ENGINE;
const void *SL_IID_PLAY                      = &_iid_PLAY;
const void *SL_IID_BUFFERQUEUE               = &_iid_BUFFERQUEUE;
const void *SL_IID_ANDROIDSIMPLEBUFFERQUEUE  = &_iid_ANDROIDSIMPLEBUFFERQUEUE;
const void *SL_IID_VOLUME                    = &_iid_VOLUME;
const void *SL_IID_ANDROIDCONFIGURATION      = &_iid_ANDROIDCONFIGURATION;
const void *SL_IID_ENVIRONMENTALREVERB       = &_iid_ENVIRONMENTALREVERB;
const void *SL_IID_EFFECTSEND                = &_iid_EFFECTSEND;
const void *SL_IID_OUTPUTMIX                 = &_iid_OUTPUTMIX;
const void *SL_IID_PREFETCHSTATUS            = &_iid_PREFETCHSTATUS;
const void *SL_IID_ANDROIDEFFECTSEND         = &_iid_ANDROIDEFFECTSEND;
const void *SL_IID_ANDROIDEFFECT             = &_iid_ANDROIDEFFECT;
const void *SL_IID_ANDROIDEFFECTCAPABILITIES = &_iid_ANDROIDEFFECTCAPABILITIES;
const void *SL_IID_BASSBOOST                 = &_iid_BASSBOOST;
const void *SL_IID_DEVICEVOLUME              = &_iid_DEVICEVOLUME;
const void *SL_IID_DYNAMICINTERFACEMANAGEMENT= &_iid_DYNAMICINTERFACEMANAGEMENT;
const void *SL_IID_DYNAMICSOURCE             = &_iid_DYNAMICSOURCE;
const void *SL_IID_ENGINECAPABILITIES        = &_iid_ENGINECAPABILITIES;
const void *SL_IID_EQUALIZER                 = &_iid_EQUALIZER;
const void *SL_IID_LEDDEVICE                 = &_iid_LEDDEVICE;
const void *SL_IID_METADATAEXTRACTION        = &_iid_METADATAEXTRACTION;
const void *SL_IID_METADATATRAVERSAL         = &_iid_METADATATRAVERSAL;
const void *SL_IID_MIDIMESSAGE               = &_iid_MIDIMESSAGE;
const void *SL_IID_MIDIMUTESOLO              = &_iid_MIDIMUTESOLO;
const void *SL_IID_MIDITEMPO                 = &_iid_MIDITEMPO;
const void *SL_IID_MIDITIME                  = &_iid_MIDITIME;
const void *SL_IID_MUTESOLO                  = &_iid_MUTESOLO;
const void *SL_IID_NULL                      = &_iid_NULL;
const void *SL_IID_OBJECT                    = &_iid_OBJECT;
const void *SL_IID_PITCH                     = &_iid_PITCH;
const void *SL_IID_PLAYBACKRATE              = &_iid_PLAYBACKRATE;
const void *SL_IID_PRESETREVERB              = &_iid_PRESETREVERB;
const void *SL_IID_RATEPITCH                 = &_iid_RATEPITCH;
const void *SL_IID_RECORD                    = &_iid_RECORD;
const void *SL_IID_SEEK                      = &_iid_SEEK;
const void *SL_IID_THREADSYNC                = &_iid_THREADSYNC;
const void *SL_IID_VIBRA                     = &_iid_VIBRA;
const void *SL_IID_VIRTUALIZER               = &_iid_VIRTUALIZER;
const void *SL_IID_AUDIODECODERCAPABILITIES  = &_iid_AUDIODECODERCAPABILITIES;
const void *SL_IID_AUDIOENCODER              = &_iid_AUDIOENCODER;
const void *SL_IID_AUDIOENCODERCAPABILITIES  = &_iid_AUDIOENCODERCAPABILITIES;
const void *SL_IID_AUDIOIODEVICECAPABILITIES = &_iid_AUDIOIODEVICECAPABILITIES;
const void *SL_IID_3DCOMMIT                  = &_iid_3DCOMMIT;
const void *SL_IID_3DDOPPLER                 = &_iid_3DDOPPLER;
const void *SL_IID_3DGROUPING                = &_iid_3DGROUPING;
const void *SL_IID_3DLOCATION                = &_iid_3DLOCATION;
const void *SL_IID_3DMACROSCOPIC             = &_iid_3DMACROSCOPIC;
const void *SL_IID_3DSOURCE                  = &_iid_3DSOURCE;

struct SLObjectItf_;   typedef const struct SLObjectItf_ * const * SLObjectItf;
struct SLEngineItf_;   typedef const struct SLEngineItf_ * const * SLEngineItf;
struct SLPlayItf_;     typedef const struct SLPlayItf_ * const * SLPlayItf;
struct SLAndroidSimpleBufferQueueItf_;
typedef const struct SLAndroidSimpleBufferQueueItf_ * const * SLAndroidSimpleBufferQueueItf;
struct SLVolumeItf_;   typedef const struct SLVolumeItf_ * const * SLVolumeItf;
struct SLAndroidConfigurationItf_;
typedef const struct SLAndroidConfigurationItf_ * const * SLAndroidConfigurationItf;

typedef struct { SLuint32 state, count, index; } SLAndroidSimpleBufferQueueState;
typedef void (*slAndroidSimpleBufferQueueCallback)(SLAndroidSimpleBufferQueueItf, void*);

struct SLObjectItf_ {
    SLresult (*Realize)(SLObjectItf, SLboolean);
    SLresult (*Resume)(SLObjectItf, SLboolean);
    SLresult (*GetState)(SLObjectItf, SLuint32*);
    SLresult (*GetInterface)(SLObjectItf, const void*, void*);
    void     (*Destroy)(SLObjectItf);
    SLresult (*RegisterCallback)(SLObjectItf, void*, void*);
    void     (*AbortAsyncOperation)(SLObjectItf);
    void     (*SetPriority)(SLObjectItf, SLint32, SLboolean);
    SLresult (*GetPriority)(SLObjectItf, SLint32*, SLboolean*);
    SLresult (*SetLossOfControlInterfaces)(SLObjectItf, SLint16, void*, SLboolean);
};
struct SLPlayItf_ {
    SLresult (*SetPlayState)(SLPlayItf, SLuint32);
    SLresult (*GetPlayState)(SLPlayItf, SLuint32*);
    SLresult (*GetDuration)(SLPlayItf, SLmillisecond*);
    SLresult (*GetPosition)(SLPlayItf, SLmillisecond*);
    SLresult (*RegisterCallback)(SLPlayItf, void*, void*);
    SLresult (*SetCallbackEventsMask)(SLPlayItf, SLuint32);
    SLresult (*GetCallbackEventsMask)(SLPlayItf, SLuint32*);
    SLresult (*SetMarkerPosition)(SLPlayItf, SLmillisecond);
    SLresult (*ClearMarkerPosition)(SLPlayItf);
    SLresult (*GetMarkerPosition)(SLPlayItf, SLmillisecond*);
    SLresult (*SetPositionUpdatePeriod)(SLPlayItf, SLmillisecond);
    SLresult (*GetPositionUpdatePeriod)(SLPlayItf, SLmillisecond*);
};
struct SLAndroidSimpleBufferQueueItf_ {
    SLresult (*Enqueue)(SLAndroidSimpleBufferQueueItf, const void*, SLuint32);
    SLresult (*Clear)(SLAndroidSimpleBufferQueueItf);
    SLresult (*GetState)(SLAndroidSimpleBufferQueueItf, SLAndroidSimpleBufferQueueState*);
    SLresult (*RegisterCallback)(SLAndroidSimpleBufferQueueItf, slAndroidSimpleBufferQueueCallback, void*);
};
struct SLVolumeItf_ {
    SLresult (*SetVolumeLevel)(SLVolumeItf, SLmillibel);
    SLresult (*GetVolumeLevel)(SLVolumeItf, SLmillibel*);
    SLresult (*GetMaxVolumeLevel)(SLVolumeItf, SLmillibel*);
    SLresult (*SetMute)(SLVolumeItf, SLboolean);
    SLresult (*GetMute)(SLVolumeItf, SLboolean*);
    SLresult (*EnableStereoPosition)(SLVolumeItf, SLboolean);
    SLresult (*SetStereoPosition)(SLVolumeItf, SLint32);
    SLresult (*GetStereoPosition)(SLVolumeItf, SLint32*);
};
struct SLAndroidConfigurationItf_ {
    SLresult (*SetConfiguration)(SLAndroidConfigurationItf, const SLchar*, const void*, SLuint32);
    SLresult (*GetConfiguration)(SLAndroidConfigurationItf, const SLchar*, SLuint32*, void*);
    SLresult (*AcquireJavaProxy)(SLAndroidConfigurationItf, SLuint32, void**);
    SLresult (*ReleaseJavaProxy)(SLAndroidConfigurationItf, SLuint32);
};
struct SLEngineItf_ {
    SLresult (*CreateLEDDevice)(SLEngineItf, SLObjectItf*, SLuint32, const void**, const SLboolean*);
    SLresult (*CreateVibraDevice)(SLEngineItf, SLObjectItf*, SLuint32, const void**, const SLboolean*);
    SLresult (*CreateAudioPlayer)(SLEngineItf, SLObjectItf*, void*, void*, SLuint32, const void**, const SLboolean*);
    SLresult (*CreateAudioRecorder)(SLEngineItf, SLObjectItf*, void*, void*, SLuint32, const void**, const SLboolean*);
    SLresult (*CreateOutputMix)(SLEngineItf, SLObjectItf*, SLuint32, const void**, const SLboolean*);
    SLresult (*CreateListener)(SLEngineItf, SLObjectItf*, SLuint32, const void**, const SLboolean*);
    SLresult (*Create3DGroup)(SLEngineItf, SLObjectItf*, SLuint32, const void**, const SLboolean*);
    SLresult (*CreateMetadataExtractor)(SLEngineItf, SLObjectItf*, void*, SLuint32, const void**, const SLboolean*);
    SLresult (*CreateExtensionObject)(SLEngineItf, SLObjectItf*, void*, SLuint32, SLuint32, const void**, const SLboolean*);
    SLresult (*QueryNumSupportedInterfaces)(SLEngineItf, SLuint32*);
    SLresult (*QuerySupportedInterfaces)(SLEngineItf, SLuint32, void**);
    SLresult (*QueryNumSupportedExtensions)(SLEngineItf, SLuint32*);
    SLresult (*QuerySupportedExtension)(SLEngineItf, SLuint32, SLchar*, SLint16*);
    SLresult (*IsExtensionSupported)(SLEngineItf, const SLchar*, SLboolean*);
};

typedef struct sl_obj {
    const struct SLObjectItf_ *obj_vt;
    const struct SLEngineItf_ *engine_vt;
    const struct SLPlayItf_ *play_vt;
    const struct SLAndroidSimpleBufferQueueItf_ *bq_vt;
    const struct SLVolumeItf_ *vol_vt;
    const struct SLAndroidConfigurationItf_ *cfg_vt;
    int used, kind, realized;
    SLuint32 rate, ch, bits, state;
    uint8_t *ring; size_t rsz, rr, rw;
    pthread_mutex_t lock;
    slAndroidSimpleBufferQueueCallback cb; void *cbctx;
    pthread_t thr; int thr_run;
    SDL_AudioDeviceID dev;
} sl_obj;

#define OBJ_SELF(p)  ((sl_obj*)((char*)(p) - offsetof(sl_obj, obj_vt)))
#define ENG_SELF(p)  ((sl_obj*)((char*)(p) - offsetof(sl_obj, engine_vt)))
#define PLAY_SELF(p) ((sl_obj*)((char*)(p) - offsetof(sl_obj, play_vt)))
#define BQ_SELF(p)   ((sl_obj*)((char*)(p) - offsetof(sl_obj, bq_vt)))

#define MAXOBJ 16
static sl_obj g_pool[MAXOBJ];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static SLresult obj_Realize(SLObjectItf, SLboolean);
static SLresult obj_Resume(SLObjectItf, SLboolean);
static SLresult obj_GetState(SLObjectItf, SLuint32*);
static SLresult obj_GetInterface(SLObjectItf, const void*, void*);
static void     obj_Destroy(SLObjectItf);
static SLresult obj_RegCb(SLObjectItf, void*, void*);
static void     obj_Abort(SLObjectItf);
static void     obj_SetPrio(SLObjectItf, SLint32, SLboolean);
static SLresult obj_GetPrio(SLObjectItf, SLint32*, SLboolean*);
static SLresult obj_SetLOC(SLObjectItf, SLint16, void*, SLboolean);
static SLresult eng_CreateAudioPlayer(SLEngineItf, SLObjectItf*, void*, void*, SLuint32, const void**, const SLboolean*);
static SLresult eng_CreateOutputMix(SLEngineItf, SLObjectItf*, SLuint32, const void**, const SLboolean*);
static SLresult eng_Unsup(SLEngineItf, SLObjectItf*, ...);
static SLresult play_SetState(SLPlayItf, SLuint32);
static SLresult play_GetState(SLPlayItf, SLuint32*);
static SLresult play_Ms0(SLPlayItf, SLmillisecond*);
static SLresult play_U320(SLPlayItf, SLuint32*);
static SLresult play_RegCb(SLPlayItf, void*, void*);
static SLresult bq_Enq(SLAndroidSimpleBufferQueueItf, const void*, SLuint32);
static SLresult bq_Clr(SLAndroidSimpleBufferQueueItf);
static SLresult bq_GetState(SLAndroidSimpleBufferQueueItf, SLAndroidSimpleBufferQueueState*);
static SLresult bq_RegCb(SLAndroidSimpleBufferQueueItf, slAndroidSimpleBufferQueueCallback, void*);
static SLresult vol_Lvl(SLVolumeItf, SLmillibel);
static SLresult vol_Get(SLVolumeItf, SLmillibel*);
static SLresult vol_Mute(SLVolumeItf, SLboolean);
static SLresult vol_GetMute(SLVolumeItf, SLboolean*);
static SLresult vol_EnSt(SLVolumeItf, SLboolean);
static SLresult vol_SetSt(SLVolumeItf, SLint32);
static SLresult vol_GetSt(SLVolumeItf, SLint32*);
static SLresult cfg_Set(SLAndroidConfigurationItf, const SLchar*, const void*, SLuint32);
static SLresult cfg_Get(SLAndroidConfigurationItf, const SLchar*, SLuint32*, void*);
static SLresult cfg_Null(SLAndroidConfigurationItf, SLuint32, void**);

static const struct SLObjectItf_ OBJ_VT = {
    obj_Realize, obj_Resume, obj_GetState, obj_GetInterface, obj_Destroy,
    obj_RegCb, obj_Abort, obj_SetPrio, obj_GetPrio, obj_SetLOC
};
static const struct SLEngineItf_ ENG_VT = {
    (void*)eng_Unsup, (void*)eng_Unsup, eng_CreateAudioPlayer,
    (void*)eng_Unsup, eng_CreateOutputMix,
    (void*)eng_Unsup, (void*)eng_Unsup, (void*)eng_Unsup, (void*)eng_Unsup,
    (void*)eng_Unsup, (void*)eng_Unsup, (void*)eng_Unsup, (void*)eng_Unsup,
    (void*)eng_Unsup
};
static const struct SLPlayItf_ PLAY_VT = {
    play_SetState, play_GetState, play_Ms0, play_Ms0,
    play_RegCb, play_U320, play_U320,
    play_Ms0, (void*)play_U320, play_Ms0,
    play_Ms0, play_Ms0
};
static const struct SLAndroidSimpleBufferQueueItf_ BQ_VT = {
    bq_Enq, bq_Clr, bq_GetState, bq_RegCb
};
static const struct SLVolumeItf_ VOL_VT = {
    vol_Lvl, vol_Get, vol_Get, vol_Mute, vol_GetMute,
    vol_EnSt, vol_SetSt, vol_GetSt
};
static const struct SLAndroidConfigurationItf_ CFG_VT = {
    cfg_Set, cfg_Get, cfg_Null, (void*)cfg_Null
};

static sl_obj *obj_new(int kind) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAXOBJ; i++) {
        if (!g_pool[i].used) {
            memset(&g_pool[i], 0, sizeof(g_pool[i]));
            g_pool[i].obj_vt    = &OBJ_VT;
            g_pool[i].engine_vt = &ENG_VT;
            g_pool[i].play_vt   = &PLAY_VT;
            g_pool[i].bq_vt     = &BQ_VT;
            g_pool[i].vol_vt    = &VOL_VT;
            g_pool[i].cfg_vt    = &CFG_VT;
            g_pool[i].used = 1;
            g_pool[i].kind = kind;
            pthread_mutex_init(&g_pool[i].lock, NULL);
            pthread_mutex_unlock(&g_lock);
            return &g_pool[i];
        }
    }
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

static void *feeder(void *arg) {
    sl_obj *o = arg;
    while (o->thr_run) {
        pthread_mutex_lock(&o->lock);
        size_t avail = (o->rw + o->rsz - o->rr) % o->rsz;
        slAndroidSimpleBufferQueueCallback cb = o->cb; void *ctx = o->cbctx;
        pthread_mutex_unlock(&o->lock);
        if (avail < o->rsz/2 && cb && o->state == SL_PLAYSTATE_PLAYING)
            cb((SLAndroidSimpleBufferQueueItf)&o->bq_vt, ctx);
        SDL_Delay(5);
    }
    return NULL;
}
static void sdl_cb(void *ud, Uint8 *stream, int len) {
    sl_obj *o = ud;
    pthread_mutex_lock(&o->lock);
    size_t avail = (o->rw + o->rsz - o->rr) % o->rsz;
    size_t want = (size_t)len;
    if (avail >= want) {
        for (size_t i = 0; i < want; i++) { stream[i] = o->ring[o->rr]; o->rr = (o->rr+1)%o->rsz; }
    } else {
        for (size_t i = 0; i < avail; i++) { stream[i] = o->ring[o->rr]; o->rr = (o->rr+1)%o->rsz; }
        memset(stream+avail, 0, want-avail);
    }
    pthread_mutex_unlock(&o->lock);
}

static SLresult obj_Realize(SLObjectItf self, SLboolean async) {
    (void)async;
    sl_obj *o = OBJ_SELF(self); o->realized = 1;
    if (o->kind == 1) {
        SDL_AudioSpec want = {0}, have;
        want.freq = o->rate ? (int)o->rate : 48000;
        want.channels = o->ch ? (Uint8)o->ch : 2;
        want.format = (o->bits == 32) ? AUDIO_F32SYS : AUDIO_S16SYS;
        want.samples = 1024; want.callback = sdl_cb; want.userdata = o;
        o->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!o->dev) { LOGE("SDL_OpenAudioDevice: %s", SDL_GetError()); return SL_RESULT_FAILURE; }
        o->rsz = (size_t)have.freq * have.channels * (SDL_AUDIO_BITSIZE(have.format)/8);
        if (o->rsz < 8192) o->rsz = 8192;
        o->ring = calloc(1, o->rsz);
        o->thr_run = 1;
        if (pthread_create(&o->thr, NULL, feeder, o) != 0) o->thr_run = 0;
        LOGI("OpenSL PCM %uch/%uHz -> SDL dev=%u",
             (unsigned)have.channels, (unsigned)have.freq, (unsigned)o->dev);
    }
    return SL_RESULT_SUCCESS;
}
static SLresult obj_Resume(SLObjectItf s, SLboolean a) { (void)s;(void)a; return 0; }
static SLresult obj_GetState(SLObjectItf s, SLuint32 *p) { (void)s; if(p)*p=0; return 0; }
static SLresult obj_GetInterface(SLObjectItf self, const void *iid, void *out) {
    sl_obj *o = OBJ_SELF(self);
    if (!out) return SL_RESULT_FAILURE;
    if (o->kind == 0) {
        if (iid == SL_IID_ENGINE) { *(SLEngineItf*)out = &o->engine_vt; return 0; }
    } else if (o->kind == 1) {
        if (iid == SL_IID_PLAY) { *(SLPlayItf*)out = &o->play_vt; return 0; }
        if (iid == SL_IID_BUFFERQUEUE || iid == SL_IID_ANDROIDSIMPLEBUFFERQUEUE) {
            *(SLAndroidSimpleBufferQueueItf*)out = &o->bq_vt; return 0;
        }
        if (iid == SL_IID_VOLUME) { *(SLVolumeItf*)out = &o->vol_vt; return 0; }
        if (iid == SL_IID_ANDROIDCONFIGURATION) { *(SLAndroidConfigurationItf*)out = &o->cfg_vt; return 0; }
    } else if (o->kind == 2) {
        if (iid == SL_IID_OUTPUTMIX) { *(void**)out = &o->obj_vt; return 0; }
    }
    *(void**)out = NULL; return 0;
}
static void obj_Destroy(SLObjectItf self) {
    sl_obj *o = OBJ_SELF(self);
    if (o->thr_run) { o->thr_run = 0; pthread_join(o->thr, NULL); }
    if (o->dev) { SDL_CloseAudioDevice(o->dev); o->dev = 0; }
    free(o->ring); o->ring = NULL;
    pthread_mutex_destroy(&o->lock);
    o->used = 0;
}
static SLresult obj_RegCb(SLObjectItf s, void *c, void *x) { (void)s;(void)c;(void)x; return 0; }
static void obj_Abort(SLObjectItf s) { (void)s; }
static void obj_SetPrio(SLObjectItf s, SLint32 p, SLboolean e) { (void)s;(void)p;(void)e; }
static SLresult obj_GetPrio(SLObjectItf s, SLint32 *p, SLboolean *q) { (void)s; if(p)*p=0; if(q)*q=0; return 0; }
static SLresult obj_SetLOC(SLObjectItf s, SLint16 n, void *i, SLboolean e) { (void)s;(void)n;(void)i;(void)e; return 0; }

static SLresult eng_CreateAudioPlayer(SLEngineItf self, SLObjectItf *pp, void *src, void *snk,
                                       SLuint32 n, const void **ids, const SLboolean *req) {
    (void)self;(void)snk;(void)n;(void)ids;(void)req;
    if (!pp) return SL_RESULT_FAILURE;
    SLuint32 ch=0, rate=0, bits=0;
    if (src) {
        void **ds = (void**)src; void *fmt = ds[1];
        if (fmt) { SLuint32 *f = fmt; ch=f[1]; rate=f[2]/1000; bits=f[3]; }
    }
    sl_obj *o = obj_new(1); if (!o) return SL_RESULT_FAILURE;
    o->ch = ch?ch:2; o->rate = rate?rate:48000; o->bits = bits?bits:16;
    *pp = (SLObjectItf)&o->obj_vt;
    LOGI("CreateAudioPlayer ch=%u rate=%u bits=%u",
         (unsigned)o->ch, (unsigned)o->rate, (unsigned)o->bits);
    return 0;
}
static SLresult eng_CreateOutputMix(SLEngineItf s, SLObjectItf *p, SLuint32 n,
                                     const void **i, const SLboolean *r) {
    (void)s;(void)n;(void)i;(void)r;
    if (!p) return SL_RESULT_FAILURE;
    sl_obj *o = obj_new(2); if (!o) return SL_RESULT_FAILURE;
    *p = (SLObjectItf)&o->obj_vt;
    return 0;
}
static SLresult eng_Unsup(SLEngineItf s, SLObjectItf *p, ...) { (void)s; if (p) *p = NULL; return 0; }

static SLresult play_SetState(SLPlayItf self, SLuint32 st) {
    sl_obj *o = PLAY_SELF(self); o->state = st;
    if (o->dev) SDL_PauseAudioDevice(o->dev, st == SL_PLAYSTATE_PLAYING ? 0 : 1);
    return 0;
}
static SLresult play_GetState(SLPlayItf self, SLuint32 *p) {
    sl_obj *o = PLAY_SELF(self); if(p)*p=o->state; return 0;
}
static SLresult play_Ms0(SLPlayItf s, SLmillisecond *p) { (void)s; if(p)*p=0; return 0; }
static SLresult play_U320(SLPlayItf s, SLuint32 *p) { (void)s; if(p)*p=0; return 0; }
static SLresult play_RegCb(SLPlayItf s, void *c, void *x) { (void)s;(void)c;(void)x; return 0; }

static SLresult bq_Enq(SLAndroidSimpleBufferQueueItf self, const void *buf, SLuint32 sz) {
    sl_obj *o = BQ_SELF(self);
    if (!buf || !sz) return SL_RESULT_FAILURE;
    pthread_mutex_lock(&o->lock);
    size_t free_slots = (o->rr + o->rsz - o->rw - 1) % o->rsz;
    size_t w = sz > free_slots ? free_slots : sz;
    for (size_t i = 0; i < w; i++) {
        o->ring[o->rw] = ((const uint8_t*)buf)[i]; o->rw = (o->rw+1)%o->rsz;
    }
    pthread_mutex_unlock(&o->lock);
    return 0;
}
static SLresult bq_Clr(SLAndroidSimpleBufferQueueItf self) {
    sl_obj *o = BQ_SELF(self);
    pthread_mutex_lock(&o->lock); o->rr = o->rw = 0; pthread_mutex_unlock(&o->lock);
    return 0;
}
static SLresult bq_GetState(SLAndroidSimpleBufferQueueItf s, SLAndroidSimpleBufferQueueState *p) {
    (void)s; if(p){p->state=0;p->count=0;p->index=0;} return 0;
}
static SLresult bq_RegCb(SLAndroidSimpleBufferQueueItf self,
                          slAndroidSimpleBufferQueueCallback cb, void *ctx) {
    sl_obj *o = BQ_SELF(self); o->cb = cb; o->cbctx = ctx; return 0;
}

static SLresult vol_Lvl(SLVolumeItf s, SLmillibel l) { (void)s;(void)l; return 0; }
static SLresult vol_Get(SLVolumeItf s, SLmillibel *p) { (void)s; if(p)*p=0; return 0; }
static SLresult vol_Mute(SLVolumeItf s, SLboolean m) { (void)s;(void)m; return 0; }
static SLresult vol_GetMute(SLVolumeItf s, SLboolean *m) { (void)s; if(m)*m=0; return 0; }
static SLresult vol_EnSt(SLVolumeItf s, SLboolean e) { (void)s;(void)e; return 0; }
static SLresult vol_SetSt(SLVolumeItf s, SLint32 p) { (void)s;(void)p; return 0; }
static SLresult vol_GetSt(SLVolumeItf s, SLint32 *p) { (void)s; if(p)*p=0; return 0; }

static SLresult cfg_Set(SLAndroidConfigurationItf s, const SLchar *k,
                         const void *v, SLuint32 z) { (void)s;(void)k;(void)v;(void)z; return 0; }
static SLresult cfg_Get(SLAndroidConfigurationItf s, const SLchar *k,
                         SLuint32 *ps, void *v) { (void)s;(void)k;(void)v; if(ps)*ps=0; return 0; }
static SLresult cfg_Null(SLAndroidConfigurationItf s, SLuint32 t, void **p) {
    (void)s;(void)t; if(p)*p=NULL; return 0;
}

SLresult slCreateEngine(SLObjectItf *pe, SLuint32 no, const void *eo,
                        SLuint32 ni, const void *ii, const SLboolean *ir) {
    (void)no;(void)eo;(void)ni;(void)ii;(void)ir;
    if (!pe) return SL_RESULT_FAILURE;
    sl_obj *o = obj_new(0); if (!o) return SL_RESULT_FAILURE;
    *pe = (SLObjectItf)&o->obj_vt;
    LOGI("slCreateEngine -> %p", (void*)o);
    return 0;
}