#pragma once
#include "Limelight.h"

int  audio_init(int audioConfig, const POPUS_MULTISTREAM_CONFIGURATION opusCfg,
                void *context, int arFlags);
void audio_start(void);
void audio_stop(void);
void audio_cleanup(void);
void audio_decode_and_play(char *data, int length);
int  audio_capabilities(void);
