#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── SceVideoOut ─────────────────────────────────────────────────────────── */
#define SCE_VIDEO_OUT_BUS_TYPE_MAIN              0
#define SCE_VIDEO_OUT_PIXEL_FORMAT_B8_G8_R8_A8  0x80000000u
#define SCE_VIDEO_OUT_TILING_MODE_LINEAR         0
#define SCE_VIDEO_OUT_ASPECT_RATIO_16_9          1
#define SCE_VIDEO_OUT_FLIP_MODE_VSYNC            1

typedef struct {
    uint32_t pixelFormat;
    uint32_t tilingMode;
    uint32_t aspectRatio;
    uint32_t width;
    uint32_t height;
    uint32_t pitchInPixel;
    uint32_t reserved0;
    uint64_t option;
} SceVideoOutBufferAttribute;

typedef struct {
    uint64_t count;
    int      processId;
    uint64_t reserved[2];
} SceVideoOutFlipStatus;

int  sceVideoOutOpen(int userId, int busType, int index, const void *param);
int  sceVideoOutSysOpenInternal(int busType, int index, const void *param);
int  sceVideoOutClose(int handle);
int  sceVideoOutSetFlipRate(int handle, int rate);
void sceVideoOutSetBufferAttribute(SceVideoOutBufferAttribute *attr,
                                   uint32_t pixelFormat, uint32_t tilingMode,
                                   uint32_t aspectRatio, uint32_t width,
                                   uint32_t height, uint32_t pitchInPixel);
int  sceVideoOutRegisterBuffers(int handle, int startIndex, void **addresses,
                                int bufferNum,
                                const SceVideoOutBufferAttribute *attr);
int  sceVideoOutSubmitFlip(int handle, int bufferIndex, int flipMode,
                           int64_t flipArg);
int  sceVideoOutGetFlipStatus(int handle, SceVideoOutFlipStatus *status);
int  sceVideoOutWaitVblankStart(int handle);

/* ── SceAudioOut ─────────────────────────────────────────────────────────── */
#define SCE_AUDIO_OUT_PORT_TYPE_MAIN       0
#define SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO 0x00000100u
#define SCE_AUDIO_OUT_VOLUME_0DB           32768

int sceAudioOutOpen(int userId, int portType, int index,
                    uint32_t len, uint32_t freq, uint32_t param);
int sceAudioOutClose(int handle);
int sceAudioOutOutput(int handle, const void *ptr);
int sceAudioOutSetVolume(int handle, int flag, int *vol);

/* ── ScePad ──────────────────────────────────────────────────────────────── */
#define SCE_PAD_PORT_TYPE_STANDARD 0

/* Button bitmask – same as PS4 */
#define SCE_PAD_BUTTON_L3         0x00000002u
#define SCE_PAD_BUTTON_R3         0x00000004u
#define SCE_PAD_BUTTON_OPTIONS    0x00000008u
#define SCE_PAD_BUTTON_UP         0x00000010u
#define SCE_PAD_BUTTON_RIGHT      0x00000020u
#define SCE_PAD_BUTTON_DOWN       0x00000040u
#define SCE_PAD_BUTTON_LEFT       0x00000080u
#define SCE_PAD_BUTTON_L2         0x00000100u
#define SCE_PAD_BUTTON_R2         0x00000200u
#define SCE_PAD_BUTTON_L1         0x00000400u
#define SCE_PAD_BUTTON_R1         0x00000800u
#define SCE_PAD_BUTTON_TRIANGLE   0x00001000u
#define SCE_PAD_BUTTON_CIRCLE     0x00002000u
#define SCE_PAD_BUTTON_CROSS      0x00004000u
#define SCE_PAD_BUTTON_SQUARE     0x00008000u
#define SCE_PAD_BUTTON_TOUCH_PAD  0x00100000u
#define SCE_PAD_BUTTON_CREATE     0x00000001u

typedef struct { uint8_t x, y; } ScePadAnalogStick;
typedef struct { uint8_t l2, r2; uint8_t _pad[2]; } ScePadAnalogButtons;

typedef struct {
    uint32_t          buttons;
    ScePadAnalogStick leftStick;
    ScePadAnalogStick rightStick;
    ScePadAnalogButtons analogButtons;
    uint8_t           _reserved[128];
} ScePadData;

int scePadInit(void);
int scePadOpen(int userId, int type, int index, const void *param);
int scePadClose(int handle);
int scePadReadState(int handle, ScePadData *data);

/* ── SceUserService ──────────────────────────────────────────────────────── */
#define SCE_USER_SERVICE_USER_ID_SYSTEM 0xFEu

int sceUserServiceInitialize(const void *param);
int sceUserServiceTerminate(void);
int sceUserServiceGetInitialUser(int *userId);

/* ── SceSysmodule ────────────────────────────────────────────────────────── */
#define SCE_SYSMODULE_PAD       0x0011
#define SCE_SYSMODULE_AUDIO_OUT 0x0005
#define SCE_SYSMODULE_VIDEO_OUT 0x0056

int sceSysmoduleLoadModule(uint16_t moduleId);
int sceSysmoduleUnloadModule(uint16_t moduleId);
