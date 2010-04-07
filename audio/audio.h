#include "muxers.h"

#ifndef X264_AUDIO_H
#define X264_AUDIO_H

// TODO: make this libav*-independent

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"

#define AUDIO_BUFSIZE (AVCODEC_MAX_AUDIO_FRAME_SIZE*3/2)

/* properties of the source given by the demuxer */
typedef struct audio_info_t
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
    struct cli_audio_t *self;
    audio_info_t *info;
    int external;
    int copy;
    int track;
    AVPacketList *first_pkt, *last_pkt;
    int pktcount;
    int pktsize;
    AVPacket pkt, pkt_temp;
    rational_t *time_base;
    int framesize;
    int framelen;
    int64_t seek_dts;
    int64_t first_dts;
    struct audio_hnd_t *next;
    struct audio_hnd_t *last;
    struct audio_hnd_t *enc;
    hnd_t *opaque;
} audio_hnd_t;

typedef struct audio_opt_t
{
    char *encoder_name;
    int bitrate;
    int quality;
    int quality_mode;
} audio_opt_t;

typedef struct cli_audio_t
{
    int (*open_track_lavf)( audio_hnd_t *handle, AVFormatContext *ctx, int track, int copy );
    int (*open_audio_file)( audio_hnd_t *handle, const char *filename, int track, int copy );
    int (*open_encoder)( audio_hnd_t *handle, audio_opt_t *opt );
    int64_t (*demux_audio)( audio_hnd_t *handle );
    int (*decode_audio)( audio_hnd_t *handle, uint8_t *buffer, int buffer_length );
    int (*encode_audio)( audio_hnd_t *handle, uint8_t *outbuf, int outbuf_length, uint8_t *inbuf, int inbuf_length );
    int (*close_filter)( audio_hnd_t *handle );
} cli_audio_t;

enum
{
    TRACK_ANY = -1,
    TRACK_NONE = -2,
};

enum
{
    AUDIO_NONE  = -4,
    AUDIO_ERROR = -3,
    AUDIO_QUEUE_EMPTY = -2,
    AUDIO_AGAIN = -1
};

extern const cli_audio_t lavcdec_audio;
extern const cli_audio_t lavcenc_audio;

audio_hnd_t *open_audio_decoder( cli_audio_t *dec, AVFormatContext *ctx, int *track, int copy );
AVFormatContext *open_lavf_demuxer( const char *filename );
void close_audio( audio_hnd_t *base );

static inline audio_hnd_t *open_external_audio( cli_audio_t *dec, const char *filename, int *track, int copy )
{
    audio_hnd_t *h = open_audio_decoder( dec, open_lavf_demuxer( filename ), track, copy );
    h->external = 1;
    h->first_dts = AV_NOPTS_VALUE;

    return h;
}

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

#endif
