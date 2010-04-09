#include "muxers.h"

#ifndef X264_AUDIO_H
#define X264_AUDIO_H

typedef struct audio_opt_t
{
    char *encoder_name;
    int bitrate;
    int quality;
    int quality_mode;
} audio_opt_t;

struct AVFormatContext;
struct AVPacket;

typedef struct cli_audio_t
{
    int (*open_track_lavf)( hnd_t handle, struct AVFormatContext *ctx, int track, int copy );
    int (*open_encoder)( hnd_t handle, audio_opt_t *opt );
    int64_t (*demux_audio)( hnd_t handle );
    int (*decode_audio)( hnd_t handle, uint8_t *buffer, int buffer_length );
    int (*encode_audio)( hnd_t handle, uint8_t *outbuf, int outbuf_length, uint8_t *inbuf, int inbuf_length );
    int (*close_filter)( hnd_t handle );
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

hnd_t open_audio_decoder( cli_audio_t *dec, struct AVFormatContext *ctx, int *track, int copy );
hnd_t open_external_audio( cli_audio_t *dec, const char *filename, int *track, int copy );
void close_audio( hnd_t base );

/**
 * Puts an AVPacket in the decoding queue.
 * @returns 0 on success, -1 on error
 */
int audio_queue_avpacket( hnd_t handle, struct AVPacket *pkt );
/**
 * Calls audio_queue_avpacket with an AVPacket with .data = buffer, .size = buffer_length, .dts = dts.
 * The .data is memcpyed from the buffer.
 * @returns the result of audio_queue_avpacket
 */
int audio_queue_rawdata( hnd_t handle, uint8_t *buffer, int buffer_length, int64_t dts );
/**
 * Gets the next AVPacket in the decoding queue and puts in pkt.
 * @returns 1 on success, 0 if the queue is empty
 */
int audio_dequeue_avpacket( hnd_t handle, struct AVPacket *pkt );

#endif
