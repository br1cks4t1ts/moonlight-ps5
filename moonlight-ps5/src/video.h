#pragma once
#include "Limelight.h"

int  video_init(int videoFormat, int width, int height, int redrawRate,
                void *context, int drFlags);
void video_start(void);
void video_stop(void);
void video_cleanup(void);
int  video_submit_decode_unit(PDECODE_UNIT decodeUnit);
int  video_capabilities(void);
