#include "audio/audio.h"
#include "audio/audio_internal.h"

#ifndef LAVC_H_
#define LAVC_H_

typedef struct encoder_t
{
    AVCodecContext *ctx;
} encoder_t;

typedef struct opaque_t
{
    AVFormatContext *lavf;
} opaque_t;

static inline void from_avrational( rational_t *r, AVRational *av ) {
    r->num = (int64_t) av->num;
    r->den = (int64_t) av->den;
}

static inline void to_avrational( AVRational *av, rational_t *r ) {
    av->num = (int32_t) r->num;
    av->den = (int32_t) r->den;
}

#endif /* LAVC_H_ */
