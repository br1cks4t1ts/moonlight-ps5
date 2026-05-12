#include "video.h"
#include "platform.h"
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <ps5/kernel.h>

#define FRAME_WIDTH  1920
#define FRAME_HEIGHT 1080
#define FB_COUNT     3
#define FB_ALIGN     0x100000  /* 1 MiB – required by SceVideoOut */

static int             s_video_handle = -1;
static int             s_width, s_height;

/* FFmpeg */
static AVCodecContext *s_codec_ctx = NULL;
static AVPacket       *s_pkt       = NULL;
static AVFrame        *s_frame     = NULL;

/* Framebuffers – aligned to FB_ALIGN */
static void  *s_fb[FB_COUNT];
static int    s_fb_index = 0;
static size_t s_fb_size;

static pthread_mutex_t s_fb_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── YUV420 → BGRA blit ──────────────────────────────────────────────────── */
static void yuv420_to_bgra(const AVFrame *f, uint8_t *dst, int dw, int dh) {
    const uint8_t *Y  = f->data[0];
    const uint8_t *Cb = f->data[1];
    const uint8_t *Cr = f->data[2];
    int ystride  = f->linesize[0];
    int cbstride = f->linesize[1];
    int crstride = f->linesize[2];

    for (int y = 0; y < dh; y++) {
        uint8_t *row = dst + y * dw * 4;
        for (int x = 0; x < dw; x++) {
            int yv = Y[y * ystride + x];
            int cb = Cb[(y / 2) * cbstride + (x / 2)] - 128;
            int cr = Cr[(y / 2) * crstride + (x / 2)] - 128;

            int r = yv + (int)(1.402f  * cr);
            int g = yv - (int)(0.344f  * cb) - (int)(0.714f * cr);
            int b = yv + (int)(1.772f  * cb);

            row[x * 4 + 0] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
            row[x * 4 + 1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
            row[x * 4 + 2] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
            row[x * 4 + 3] = 0xFF;
        }
    }
}

/* ── Moonlight callbacks ─────────────────────────────────────────────────── */
int video_init(int videoFormat, int width, int height, int redrawRate,
               void *context, int drFlags) {
    (void)videoFormat; (void)context; (void)drFlags;

    s_width  = width  > 0 ? width  : FRAME_WIDTH;
    s_height = height > 0 ? height : FRAME_HEIGHT;

    /* Elevate our ucred so SceVideoOut service accepts us as a game process */
    pid_t mypid = getpid();
    uint64_t orig_authid = kernel_get_ucred_authid(mypid);
    uint8_t privcaps[16] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                            0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
    printf("video: orig authid=0x%lx pid=%d\n", orig_authid, mypid);
    kernel_set_ucred_authid(mypid, 0x3800000000000001UL);
    kernel_set_ucred_caps(mypid, privcaps);

    int userId = 0;
    sceUserServiceGetInitialUser(&userId);
    printf("video: sceUserServiceGetInitialUser = %d\n", userId);

    /* Try busType 0 and 1, userId 0/1/0xFE */
    int user_ids[]  = { userId, 0, 1, 0xFE, 0xFF000001 };
    int bus_types[] = { 0, 1 };
    s_video_handle = -1;
    for (int b = 0; b < (int)(sizeof(bus_types)/sizeof(bus_types[0])) && s_video_handle < 0; b++) {
        for (int u = 0; u < (int)(sizeof(user_ids)/sizeof(user_ids[0])); u++) {
            int h = sceVideoOutOpen(user_ids[u], bus_types[b], 0, NULL);
            printf("video: sceVideoOutOpen(uid=%d bus=%d) = 0x%x\n",
                   user_ids[u], bus_types[b], (unsigned)h);
            if (h >= 0) { s_video_handle = h; break; }
        }
    }
    kernel_set_ucred_authid(mypid, orig_authid);

    /* Last resort: privileged system API — doesn't require a user session */
    if (s_video_handle < 0) {
        for (int b = 0; b < 2; b++) {
            int h = sceVideoOutSysOpenInternal(b, 0, NULL);
            printf("video: sceVideoOutSysOpenInternal(bus=%d) = 0x%x\n", b, (unsigned)h);
            if (h >= 0) { s_video_handle = h; break; }
        }
    }

    if (s_video_handle < 0) {
        fprintf(stderr, "video: sceVideoOutOpen failed on all combos\n");
        return -1;
    }
    sceVideoOutSetFlipRate(s_video_handle, 0);

    /* Allocate aligned framebuffers */
    s_fb_size = (size_t)s_width * s_height * 4;
    s_fb_size = (s_fb_size + FB_ALIGN - 1) & ~(size_t)(FB_ALIGN - 1);

    void *fb_addrs[FB_COUNT];
    for (int i = 0; i < FB_COUNT; i++) {
        if (posix_memalign(&s_fb[i], FB_ALIGN, s_fb_size) != 0) {
            fprintf(stderr, "video: framebuffer alloc failed\n");
            return -1;
        }
        memset(s_fb[i], 0, s_fb_size);
        fb_addrs[i] = s_fb[i];
    }

    SceVideoOutBufferAttribute attr;
    sceVideoOutSetBufferAttribute(&attr,
        SCE_VIDEO_OUT_PIXEL_FORMAT_B8_G8_R8_A8,
        SCE_VIDEO_OUT_TILING_MODE_LINEAR,
        SCE_VIDEO_OUT_ASPECT_RATIO_16_9,
        (uint32_t)s_width, (uint32_t)s_height, (uint32_t)s_width);

    int rc = sceVideoOutRegisterBuffers(s_video_handle, 0, fb_addrs,
                                        FB_COUNT, &attr);
    if (rc < 0) {
        fprintf(stderr, "video: sceVideoOutRegisterBuffers failed: 0x%x\n", rc);
        return -1;
    }

    /* Init FFmpeg decoder */
    enum AVCodecID codec_id = (videoFormat & VIDEO_FORMAT_MASK_H265)
                              ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
    const AVCodec *codec = avcodec_find_decoder(codec_id);
    if (!codec) {
        fprintf(stderr, "video: codec not found\n");
        return -1;
    }

    s_codec_ctx = avcodec_alloc_context3(codec);
    if (!s_codec_ctx) return -1;

    s_codec_ctx->width       = s_width;
    s_codec_ctx->height      = s_height;
    s_codec_ctx->thread_count = 2;
    s_codec_ctx->flags2      |= AV_CODEC_FLAG2_FAST;

    if (avcodec_open2(s_codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "video: avcodec_open2 failed\n");
        return -1;
    }

    s_pkt   = av_packet_alloc();
    s_frame = av_frame_alloc();
    if (!s_pkt || !s_frame) return -1;

    printf("video: init OK %dx%d\n", s_width, s_height);
    return DR_OK;
}

void video_start(void)   { /* nothing to do – FFmpeg is stateless per-frame */ }
void video_stop(void)    {}

void video_cleanup(void) {
    if (s_frame)     { av_frame_free(&s_frame);          s_frame = NULL; }
    if (s_pkt)       { av_packet_free(&s_pkt);           s_pkt   = NULL; }
    if (s_codec_ctx) { avcodec_free_context(&s_codec_ctx); }

    if (s_video_handle >= 0) {
        sceVideoOutClose(s_video_handle);
        s_video_handle = -1;
    }
    for (int i = 0; i < FB_COUNT; i++) {
        free(s_fb[i]);
        s_fb[i] = NULL;
    }
}

int video_submit_decode_unit(PDECODE_UNIT decodeUnit) {
    if (!s_codec_ctx) return DR_OK;

    /* Assemble NAL data into packet */
    int total = 0;
    for (PLENTRY e = decodeUnit->bufferList; e; e = e->next)
        total += e->length;

    if (av_new_packet(s_pkt, total) < 0)
        return DR_NEED_IDR;

    uint8_t *p = s_pkt->data;
    for (PLENTRY e = decodeUnit->bufferList; e; e = e->next) {
        memcpy(p, e->data, e->length);
        p += e->length;
    }
    s_pkt->pts = (int64_t)(decodeUnit->presentationTimeUs / 1000);

    int ret = avcodec_send_packet(s_codec_ctx, s_pkt);
    av_packet_unref(s_pkt);
    if (ret < 0) return DR_NEED_IDR;

    while (avcodec_receive_frame(s_codec_ctx, s_frame) == 0) {
        pthread_mutex_lock(&s_fb_lock);

        int next = (s_fb_index + 1) % FB_COUNT;
        yuv420_to_bgra(s_frame, (uint8_t *)s_fb[next], s_width, s_height);

        sceVideoOutSubmitFlip(s_video_handle, next,
                              SCE_VIDEO_OUT_FLIP_MODE_VSYNC, 0);
        s_fb_index = next;

        pthread_mutex_unlock(&s_fb_lock);
    }

    return DR_OK;
}

int video_capabilities(void) {
    return CAPABILITY_DIRECT_SUBMIT;
}
