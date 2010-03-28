#include "muxers.h"

#ifndef X264_AUDIO_H
#define X264_AUDIO_H

// TODO: make this libav*-independent

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"

#define AUDIO_BUFSIZE (AVCODEC_MAX_AUDIO_FRAME_SIZE*3/2)

/* properties of the source given by the demuxer */
typedef struct
{
    int codec_id;
    const char *codec_name;
    int samplerate;
    int samplefmt;
    int samplesize;
    int channels;
    uint8_t *extradata;
    int extradata_size;
} audio_info_t;

typedef struct audio_hnd_t
{
    audio_info_t *info;
    int copy;
    int raw;
    int track;
    AVPacketList *first_pkt, *last_pkt;
    int pktcount;
    int pktsize;
    AVPacket pkt, pkt_temp;
    AVRational *time_base;
    int framesize;
    int framelen;
    struct audio_hnd_t *enc_hnd;
    hnd_t *encoder;
    hnd_t *opaque;
} audio_hnd_t;

typedef struct
{
    char *encoder_name;
    int bitrate;
    int quality;
    int quality_mode;
} audio_opt_t;

typedef struct
{
    int (*open_track_lavf)( audio_hnd_t *handle, AVFormatContext *ctx, int track, int copy );
    int (*open_encoder)( audio_hnd_t *handle, audio_opt_t *opt );
    int (*decode_audio)( audio_hnd_t *handle, uint8_t *buffer, int buffer_length );
    int (*encode_audio)( audio_hnd_t *handle, uint8_t *outbuf, int outbuf_length, uint8_t *inbuf, int inbuf_length );
    int (*close_encoder)( audio_hnd_t *handle );
    int (*close_track)( audio_hnd_t *handle );
} cli_audio_t;

enum
{
    TRACK_ANY = -1,
    TRACK_NONE = -2,
};

enum
{
    AUDIO_ERROR = -3,
    AUDIO_QUEUE_EMPTY = -2,
    AUDIO_AGAIN = -1
};

extern const cli_audio_t lavc_audio;

/**
 * Puts an AVPacket in the decoding queue.
 * @returns 0 on success, -1 on error
 */
int audio_queue_avpacket( audio_hnd_t *handle, AVPacket *pkt );
/**
 * Calls audio_queue_avpacket with an AVPacket with .data = buffer, .size = buffer_length, .dts = dts.
 * The .data is memcpyed from the buffer.
 * @returns the result of audio_queue_avpacket
 */
int audio_queue_rawdata( audio_hnd_t *handle, uint8_t *buffer, int buffer_length, int64_t dts );
/**
 * Gets the next AVPacket in the decoding queue and puts in pkt.
 * @returns 1 on success, 0 if the queue is empty
 */
int audio_dequeue_avpacket( audio_hnd_t *handle, AVPacket *pkt );

static inline int64_t from_time_base( int64_t ts, AVRational *time_base )
{
    return ts * time_base->num / time_base->den;
}

static inline int64_t to_time_base( int64_t ts, AVRational *time_base )
{
    return ts * time_base->den / time_base->num;
}

#endif
