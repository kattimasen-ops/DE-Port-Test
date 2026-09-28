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
    SLresult (*Create
