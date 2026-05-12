#include "input.h"
#include "platform.h"
#include "Limelight.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Map DualSense buttons to Moonlight gamepad mask */
#define MAP(sce_btn, li_btn) \
    if (buttons & (sce_btn)) mask |= (li_btn)

static int s_pad_handle = -1;
static uint32_t s_prev_buttons = 0;

void input_init(int padHandle) {
    s_pad_handle    = padHandle;
    s_prev_buttons  = 0;
}

void input_poll_and_send(void) {
    if (s_pad_handle < 0) return;

    ScePadData pad;
    memset(&pad, 0, sizeof(pad));
    if (scePadReadState(s_pad_handle, &pad) < 0) return;

    uint32_t buttons = pad.buttons;
    int mask = 0;

    MAP(SCE_PAD_BUTTON_CROSS,    A_FLAG);
    MAP(SCE_PAD_BUTTON_CIRCLE,   B_FLAG);
    MAP(SCE_PAD_BUTTON_SQUARE,   X_FLAG);
    MAP(SCE_PAD_BUTTON_TRIANGLE, Y_FLAG);
    MAP(SCE_PAD_BUTTON_L1,       LB_FLAG);
    MAP(SCE_PAD_BUTTON_R1,       RB_FLAG);
    MAP(SCE_PAD_BUTTON_L3,       LS_CLK_FLAG);
    MAP(SCE_PAD_BUTTON_R3,       RS_CLK_FLAG);
    MAP(SCE_PAD_BUTTON_OPTIONS,  PLAY_FLAG);
    MAP(SCE_PAD_BUTTON_CREATE,   BACK_FLAG);
    MAP(SCE_PAD_BUTTON_UP,       UP_FLAG);
    MAP(SCE_PAD_BUTTON_DOWN,     DOWN_FLAG);
    MAP(SCE_PAD_BUTTON_LEFT,     LEFT_FLAG);
    MAP(SCE_PAD_BUTTON_RIGHT,    RIGHT_FLAG);
    MAP(SCE_PAD_BUTTON_TOUCH_PAD, SPECIAL_FLAG);

    /* Analog sticks: PS5 range 0-255, centre ~128; Moonlight wants -32768..32767 */
    short lx = (short)((pad.leftStick.x  - 128) * 257);
    short ly = (short)((128 - pad.leftStick.y)  * 257);
    short rx = (short)((pad.rightStick.x - 128) * 257);
    short ry = (short)((128 - pad.rightStick.y) * 257);

    /* Triggers: 0-255 → 0-255 (Moonlight uses uint8) */
    uint8_t lt = pad.analogButtons.l2;
    uint8_t rt = pad.analogButtons.r2;

    LiSendMultiControllerEvent(0, 1, mask, lt, rt, lx, ly, rx, ry);
}

void input_cleanup(void) {
    s_pad_handle = -1;
}
