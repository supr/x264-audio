#include "audio/audio.h"
#include "audio/audio_internal.h"
#include "audio/lavc.h"

static int open_encoder( audio_hnd_t *h, audio_opt_t *opt )
{
    h->enc = audio_push_filter( h, calloc( 1, sizeof( audio_hnd_t ) ) );

    audio_info_t *info = h->enc->info = malloc( sizeof( audio_info_t ) );
    memcpy( info, h->info, sizeof( audio_info_t ) );
    
    if( !strcmp( opt->encoder_name, "copy" ) )
    {
        h->enc->copy = h->copy = 1;
    }
    else if( !strcmp( opt->encoder_name, "raw" ) )
    {
        info->codec_name     = "pcm";
        info->extradata      = NULL;
        info->extradata_size = 0;
        h->enc->copy = 1;
    }
    else
    {
        info->codec_name = opt->encoder_name;

        if( !strcmp( info->codec_name, "mp3" ) )
            info->codec_name = "libmp3lame";
        else if( !strcmp( info->codec_name, "vorbis" ) )
            info->codec_name = "libvorbis";
        else if( !strcmp( info->codec_name, "aac" ) )
            info->codec_name = "libfaac";

        AVCodec *codec = avcodec_find_encoder_by_name( info->codec_name );

        if( !codec )
        {
            fprintf( stderr, "lavc [error]: could not find codec %s\n", opt->encoder_name );
            return 0;
        }
        info->codec_id = codec->id;

        AVCodecContext *ctx = avcodec_alloc_context();
        ctx->sample_rate = info->samplerate;
        ctx->sample_fmt  = info->samplefmt;
        ctx->channels    = info->channels;
        ctx->flags2     |= CODEC_FLAG2_BIT_RESERVOIR; // mp3
        ctx->flags      |= CODEC_FLAG_GLOBAL_HEADER; // aac

        if( !strcmp( info->codec_name, "aac" ) || !strcmp( info->codec_name, "libfaac" ) )
            ctx->profile = FF_PROFILE_AAC_LOW;

        if( opt->quality_mode )
        {
            ctx->flags         |= CODEC_FLAG_QSCALE;
            ctx->global_quality = FF_QP2LAMBDA * opt->quality;
        }
        else
            ctx->bit_rate = opt->bitrate * 1000;

        if( avcodec_open( ctx, codec ) < 0 )
        {
            fprintf( stderr, "lavc [error]: could not open encoder\n" );
            return 0;
        }

        info->extradata      = ctx->extradata;
        info->extradata_size = ctx->extradata_size;

        h->enc->framelen  = to_time_base( ctx->frame_size, h->time_base ) / ctx->sample_rate;
        h->enc->framesize = ctx->frame_size * ctx->channels * info->samplesize;

        encoder_t *enc = calloc( 1, sizeof( encoder_t ) );
        enc->ctx       = ctx;

        h->enc->opaque = (hnd_t) enc;
    }
    return 1;
}

static int encode_audio( audio_hnd_t *h, uint8_t *outbuf, int outbuflen, uint8_t *inbuf, int inbuflen )
{
    assert( h && h->enc );
    assert( outbuf && outbuflen );
    assert( inbuf && inbuflen );

    if( h->enc->copy )
    {
        if( outbuflen < inbuflen )
        {
            fprintf( stderr, "lavc [error]: output buffer too short (%d / %d)\n", outbuflen, inbuflen );
            return AUDIO_ERROR;
        }
        memcpy( outbuf, inbuf, inbuflen );
        return inbuflen;
    }
    if( inbuflen < h->enc->framesize )
    {
        fprintf( stderr, "lavc [error]: input buffer too short (%d / %d)\n", inbuflen, h->enc->framesize );
        return AUDIO_ERROR;
    }

    assert( h->enc->opaque );
    encoder_t *enc = (encoder_t*) h->enc->opaque;

    assert( enc->ctx );
    int bytes = avcodec_encode_audio( enc->ctx, outbuf, outbuflen, ( int16_t* ) inbuf );
    return bytes < 0 ? AUDIO_ERROR : bytes == 0 ? AUDIO_AGAIN : bytes;
}

static int close_filter( audio_hnd_t *h )
{
    assert( h && h->info );

    encoder_t *enc = (encoder_t*) h->opaque;

    if( enc )
    {
        avcodec_close( enc->ctx );
        av_free( enc->ctx );
        free( enc );
    }

    AVPacket *pkt = NULL;
    while( audio_dequeue_avpacket( h, pkt ) )
        av_free_packet( pkt );
    free( h->info );

    return 0;
}

const cli_audio_t lavcenc_audio = {
    .open_encoder = open_encoder,
    .encode_audio = encode_audio,
    .close_filter = close_filter
};
