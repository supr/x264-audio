#include "muxers.h"
#include "audio/audio.h"

#ifndef X264_AUDIO_INTERNAL_H
#define X264_AUDIO_INTERNAL_H

// TODO: make this libav*-independent

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"

#define AUDIO_BUFSIZE (AVCODEC_MAX_AUDIO_FRAME_SIZE*3/2)
#define AUDIO_MAX_QUEUE_LENGTH 10

enum FilterType
{
    FILTER_TYPE_NONE,
    FILTER_TYPE_DECODER,
    FILTER_TYPE_ENCODER,
    FILTER_TYPE_TRANSFORM
};

/* properties of the source given by the demuxer */
typedef struct audio_info_t
{
    enum CodecID codec_id;
    const char *codec_name;
    int samplerate;
    enum SampleFormat samplefmt;
    int samplesize;
    int channels;
    uint8_t *extradata;
    int extradata_size;
} audio_info_t;

typedef struct audio_hnd_t
{
    enum FilterType type;
    struct cli_audio_t *self;
    audio_info_t *info;
    AVPacketList *first_pkt, *last_pkt;
    int pktcount;
    int pktsize;
    AVPacket pkt, pkt_temp;
    rational_t *time_base;
    int external;
    int copy;
    unsigned trackid;
    int framesize;
    int framelen;
    int64_t seek_dts;
    int64_t first_dts;
    struct audio_hnd_t *next;
    struct audio_hnd_t *last;
    struct audio_hnd_t *enc;
    struct audio_hnd_t *muxer;
    AVFormatContext *lavf;
    hnd_t opaque;
} audio_hnd_t;

void audio_readjust_last( hnd_t base );
hnd_t audio_push_filter( hnd_t base, hnd_t filter );
hnd_t audio_pop_filter( hnd_t base );

#endif
