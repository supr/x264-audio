#include "audio/audio.h"

typedef struct
{
    AVCodecContext *ctx;
} encoder_t;

typedef struct
{
    AVFormatContext *lavf;
} opaque_t;

int try_open_track( audio_hnd_t *h, int track, int copy )
{
    opaque_t *o = ( opaque_t* ) h->opaque;
    if( track < 0 || ( unsigned ) track >= o->lavf->nb_streams || o->lavf->streams[track]->codec->codec_type != CODEC_TYPE_AUDIO )
        return AUDIO_AGAIN;

    AVCodecContext *ac = o->lavf->streams[track]->codec;

    if( avcodec_open( ac, avcodec_find_decoder( ac->codec_id ) ) )
        return AUDIO_ERROR;
    else
    {
        h->time_base = &o->lavf->streams[track]->time_base;
        h->info = malloc( sizeof( audio_info_t ) );
        h->info->codec_id   = ac->codec_id;
        h->info->codec_name = ac->codec->name;
        h->info->samplerate = ac->sample_rate;
        h->info->samplefmt  = ac->sample_fmt;
        h->info->channels   = ac->channels;

        switch( ac->sample_fmt )
        {
        case SAMPLE_FMT_U8:
            h->info->samplesize = 1;
            break;
        case SAMPLE_FMT_S16:
            h->info->samplesize = 2;
            break;
        case SAMPLE_FMT_S32:
        case SAMPLE_FMT_FLT:
            h->info->samplesize = 4;
            break;
        case SAMPLE_FMT_DBL:
            h->info->samplesize = 8;
            break;
        default:
            fprintf( stderr, "lavc [error]: invalid sample format!\n" );
            avcodec_close( ac );
            return AUDIO_ERROR;
        }

        if( copy )
        {
            avcodec_close( ac );
            h->copy = 1;
        }
    }
    return track;
}

static int open_track_lavf( audio_hnd_t *h, AVFormatContext *ctx, int track, int copy )
{
    h->opaque = malloc( sizeof( opaque_t ) );
    opaque_t *o = (opaque_t*) h->opaque;
    o->lavf = ctx;

    int i, j = 0;
    if( track == TRACK_ANY )
    {
        for( i = 0; i < o->lavf->nb_streams && ( j = try_open_track( h, i, copy ) ) < 0; i++ )
            ;
    }
    else
    {
        if( ( j = try_open_track( h, track, copy ) ) < 0 )
            fprintf( stderr, "lavc [error]: erorr opening audio track %d\n", track );
    }

    h->track = j;

    return j >= 0 ? j : AUDIO_ERROR;
}

static int decode_audio( audio_hnd_t *h, uint8_t *buf, int buflen ) {
    if( h->track < 0 )
        return AUDIO_ERROR;

    AVCodecContext *ac = ( ( opaque_t* ) h->opaque )->lavf->streams[h->track]->codec;
    AVPacket *pkt = &h->pkt;
    AVPacket *pkt_temp = &h->pkt_temp;

    int len = 0, datalen = 0;

    if( h->copy && pkt_temp->size > 0 )
    {
        if( buflen < pkt->size )
        {
            fprintf( stderr, "lavc [error]: copy buffer too short (%d, needs %d)\n", buflen, pkt->size );
            return AUDIO_ERROR;
        }
        memcpy( buf, pkt->data, pkt->size );
        pkt_temp->size = 0;
        return pkt->size;
    }
    else
    {
        while( pkt_temp->size > 0 )
        {
            datalen = buflen;
            len = avcodec_decode_audio3( ac, (int16_t*) buf, &datalen, pkt_temp);

            if( len < 0 ) {
                pkt_temp->size = 0;
                break;
            }

            pkt_temp->data += len;
            pkt_temp->size -= len;

            if( datalen < 0 )
                continue;

            return datalen;
        }
    }

    if( pkt->data )
        av_free_packet( pkt );

    if( !audio_dequeue_avpacket( h, pkt ) )
        return AUDIO_QUEUE_EMPTY;

    pkt_temp->data = pkt->data;
    pkt_temp->size = pkt->size;

    return AUDIO_AGAIN;
}

static int open_encoder( audio_hnd_t *h, audio_opt_t *opt )
{
    h->enc_hnd = calloc( 1, sizeof( audio_hnd_t ) );
    audio_info_t *info = h->enc_hnd->info = malloc( sizeof( audio_info_t ) );
    memcpy( info, h->info, sizeof( audio_info_t ) );

    if( h->copy || !strcmp( opt->encoder_name, "copy" ) )
        h->copy = 1;
    else if( !strcmp( opt->encoder_name, "raw" ) )
    {
        info->codec_name = "pcm";
        h->raw = 1;
    }
    else
    {
        info->codec_name = opt->encoder_name;

        if( !strcmp( info->codec_name, "mp3" ) )
            info->codec_name = "libmp3lame";
        else if( !strcmp( info->codec_name, "vorbis" ) )
            info->codec_name = "libvorbis";

        AVCodec *codec = avcodec_find_encoder_by_name( info->codec_name );

        if( ! codec && !strcmp( info->codec_name, "aac" ) )
        {
            info->codec_name = "libfaac";
            codec = avcodec_find_encoder_by_name( info->codec_name );
        }
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

        h->framelen  = to_time_base( ctx->frame_size, h->time_base ) / info->samplerate;
        h->framesize = ctx->frame_size * ctx->channels * info->samplesize;
        h->encoder   = calloc( 1, sizeof( encoder_t ) );
        ((encoder_t*) h->encoder)->ctx = ctx;
    }
    return 1;
}

static int encode_audio( audio_hnd_t *h, uint8_t *outbuf, int outbuflen, uint8_t *inbuf, int inbuflen )
{
    encoder_t *enc = (encoder_t*) h->encoder;

    if( h->copy || h->raw )
    {
        if( outbuflen < inbuflen )
        {
            fprintf( stderr, "lavc [error]: output buffer too short (%d / %d)\n", outbuflen, inbuflen );
            return AUDIO_ERROR;
        }
        memcpy( outbuf, inbuf, inbuflen );
        return inbuflen;
    }
    if( inbuflen < h->framesize )
    {
        fprintf( stderr, "lavc [error]: input buffer too short (%d / %d)\n", inbuflen, h->framesize );
        return AUDIO_ERROR;
    }

    int bytes = avcodec_encode_audio( enc->ctx, outbuf, outbuflen, ( int16_t* ) inbuf );
    return bytes < 0 ? AUDIO_ERROR : bytes == 0 ? AUDIO_AGAIN : bytes;
}

static int close_encoder( audio_hnd_t *h )
{
    encoder_t *enc = (encoder_t*) h->encoder;

    if( !enc )
        return 0;

    avcodec_close( enc->ctx );
    av_free( enc->ctx );
    free( enc );
    h->encoder = NULL;

    return 0;
}

static int close_track( audio_hnd_t *h )
{
    close_encoder( h );
    if( h->enc_hnd )
        close_track( h->enc_hnd );

    AVPacket *pkt = NULL;
    while( audio_dequeue_avpacket( h, pkt ) )
        av_free_packet( pkt );

    if( ! h->copy && h->opaque )
    {
        AVCodecContext *ac = ( ( opaque_t * ) h->opaque )->lavf->streams[h->track]->codec;
        avcodec_close( ac );
    }
    free( h->info );
    free( h->opaque );

    return 0;
}

const cli_audio_t lavc_audio = {
    .open_track_lavf = open_track_lavf,
    .open_encoder    = open_encoder,
    .decode_audio    = decode_audio,
    .encode_audio    = encode_audio,
    .close_encoder   = close_encoder,
    .close_track     = close_track
};
