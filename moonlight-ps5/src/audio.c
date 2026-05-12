#include "audio.h"
#include "platform.h"
#include <opus_multistream.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLES_PER_FRAME  240   /* 5 ms @ 48 kHz */
#define MAX_CHANNELS       2
#define AUDIO_BUF_FRAMES   4     /* ring buffer depth */

static int                     s_audio_handle  = -1;
static OpusMSDecoder          *s_decoder       = NULL;
static int                     s_channels      = 0;
static int16_t                *s_pcm_buf       = NULL;
static int16_t                *s_ring[AUDIO_BUF_FRAMES];
static int                     s_ring_write    = 0;
static uint32_t                s_frame_samples = SAMPLES_PER_FRAME;

int audio_init(int audioConfig, const POPUS_MULTISTREAM_CONFIGURATION cfg,
               void *context, int arFlags) {
    (void)audioConfig; (void)context; (void)arFlags;

    s_channels     = cfg->channelCount;
    s_frame_samples = SAMPLES_PER_FRAME;

    int err;
    s_decoder = opus_multistream_decoder_create(
        cfg->sampleRate, cfg->channelCount,
        cfg->streams, cfg->coupledStreams,
        cfg->mapping, &err);
    if (err != OPUS_OK || !s_decoder) {
        fprintf(stderr, "audio: opus_multistream_decoder_create: %d\n", err);
        return -1;
    }

    /* One decode buffer */
    s_pcm_buf = (int16_t *)malloc(s_frame_samples * s_channels * sizeof(int16_t));
    if (!s_pcm_buf) return -1;

    /* Ring of output buffers for SceAudioOut */
    for (int i = 0; i < AUDIO_BUF_FRAMES; i++) {
        s_ring[i] = (int16_t *)malloc(s_frame_samples * MAX_CHANNELS * sizeof(int16_t));
        if (!s_ring[i]) return -1;
        memset(s_ring[i], 0, s_frame_samples * MAX_CHANNELS * sizeof(int16_t));
    }

    /* Open audio port: stereo 48 kHz */
    int userId = 0;
    sceUserServiceGetInitialUser(&userId);

    s_audio_handle = sceAudioOutOpen(userId, SCE_AUDIO_OUT_PORT_TYPE_MAIN, 0,
                                     s_frame_samples, cfg->sampleRate,
                                     SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (s_audio_handle < 0) {
        fprintf(stderr, "audio: sceAudioOutOpen failed: 0x%x\n", s_audio_handle);
        return -1;
    }

    int vol[2] = {SCE_AUDIO_OUT_VOLUME_0DB, SCE_AUDIO_OUT_VOLUME_0DB};
    sceAudioOutSetVolume(s_audio_handle, 3, vol);

    printf("audio: init OK ch=%d rate=%d\n", s_channels, cfg->sampleRate);
    return DR_OK;
}

void audio_start(void)   {}
void audio_stop(void)    {}

void audio_cleanup(void) {
    if (s_decoder)      { opus_multistream_decoder_destroy(s_decoder); s_decoder = NULL; }
    if (s_pcm_buf)      { free(s_pcm_buf); s_pcm_buf = NULL; }
    for (int i = 0; i < AUDIO_BUF_FRAMES; i++) {
        free(s_ring[i]);
        s_ring[i] = NULL;
    }
    if (s_audio_handle >= 0) {
        sceAudioOutClose(s_audio_handle);
        s_audio_handle = -1;
    }
}

void audio_decode_and_play(char *data, int length) {
    if (!s_decoder || !s_pcm_buf || s_audio_handle < 0) return;

    int samples = opus_multistream_decode(
        s_decoder, (const unsigned char *)data, length,
        s_pcm_buf, (int)s_frame_samples, 0);
    if (samples <= 0) return;

    /* Upmix/downmix to stereo for SceAudioOut */
    int16_t *out = s_ring[s_ring_write];
    s_ring_write = (s_ring_write + 1) % AUDIO_BUF_FRAMES;

    if (s_channels == 1) {
        for (int i = 0; i < samples; i++) {
            out[i * 2 + 0] = s_pcm_buf[i];
            out[i * 2 + 1] = s_pcm_buf[i];
        }
    } else {
        memcpy(out, s_pcm_buf, (size_t)samples * 2 * sizeof(int16_t));
    }

    sceAudioOutOutput(s_audio_handle, out);
}

int audio_capabilities(void) {
    return CAPABILITY_DIRECT_SUBMIT;
}
