#include "muxers.h"
#include "audio/audio.h"

#ifndef X264_AUDIO_INTERNAL_H
#define X264_AUDIO_INTERNAL_H

void audio_readjust_last( audio_hnd_t *base );
audio_hnd_t *audio_push_filter( audio_hnd_t *base, audio_hnd_t *filter );
audio_hnd_t *audio_pop_filter( audio_hnd_t *base );

#endif
