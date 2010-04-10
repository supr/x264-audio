#include "audio/audio.h"
#include "audio/audio_internal.h"
#include "audio/lavc.h"

int try_open_track( hnd_t handle, int track, int copy )
{
    audio_hnd_t *h = handle;
    if( track < 0 || ( unsigned ) track >= h->lavf->nb_streams || h->lavf->streams[track]->codec->codec_type != CODEC_TYPE_AUDIO )
        return AUDIO_AGAIN;

    AVCodecContext *ac = h->lavf->streams[track]->codec;

    if( avcodec_open( ac, avcodec_find_decoder( ac->codec_id ) ) )
        return AUDIO_ERROR;
    else
    {
        h->time_base = malloc( sizeof( rational_t ) );
        from_avrational( h->time_base, &h->lavf->streams[track]->time_base );
        h->info = malloc( sizeof( audio_info_t ) );
        h->info->codec_id       = ac->codec_id;
        h->info->codec_name     = ac->codec->name;
        h->info->samplerate     = ac->sample_rate;
        h->info->samplefmt      = ac->sample_fmt;
        h->info->channels       = ac->channels;
        h->info->extradata      = ac->extradata;
        h->info->extradata_size = ac->extradata_size;

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

        h->seek_dts  = AV_NOPTS_VALUE;
        h->framelen  = to_time_base( ac->frame_size, h->time_base ) / h->info->samplerate;
        h->framesize = ac->frame_size * h->info->channels * h->info->samplesize;
    }
    return track;
}

static int open_track_lavf( hnd_t handle, AVFormatContext *ctx, int track, int copy )
{
    audio_hnd_t *h = handle;
    assert( !h->opaque && !h->lavf );
    h->lavf = ctx;

    int i, j = -1;
    if( track == TRACK_ANY )
    {
        for( i = 0; i < h->lavf->nb_streams && j < 0; i++ )
            j = try_open_track( h, i, copy );
    }
    else
    {
        if( ( j = try_open_track( h, track, copy ) ) < 0 )
            fprintf( stderr, "lavc [error]: error opening audio track %d\n", track );
    }

    if( j >= 0 )
        fprintf( stderr, "lavc [audio]: opened track %d, codec %s\n", j, h->info->codec_name );

    h->trackid = j;

    return j >= 0 ? j : AUDIO_ERROR;
}

static int decode_audio( hnd_t handle, uint8_t *buf, int buflen ) {
    audio_hnd_t *h = handle;
    if( h->trackid < 0 )
        return AUDIO_ERROR;

    AVCodecContext *ac = h->lavf->streams[h->trackid]->codec;
    AVPacket *pkt = &h->pkt;
    AVPacket *pkt_temp = &h->pkt_temp;

    int len = 0, datalen = 0;

    if( pkt_temp->size )
        assert( pkt->dts >= h->seek_dts );

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
        assert( len >= 0 );
    }

    if( pkt->data )
        av_free_packet( pkt );

    if( !audio_dequeue_avpacket( h, pkt ) )
        return AUDIO_QUEUE_EMPTY;

    pkt_temp->data = pkt->data;
    pkt_temp->size = pkt->size;

    return AUDIO_AGAIN;
}

static int close_filter( hnd_t handle )
{
    audio_hnd_t *h = handle;
    assert( h );
    AVPacket pkt = {};
    while( audio_dequeue_avpacket( h, &pkt ) )
        av_free_packet( &pkt );

    if( ! h->copy )
    {
        assert( h->lavf );
        AVCodecContext *ac = h->lavf->streams[h->trackid]->codec;
        avcodec_close( ac );
    }
    free( h->info );
    free( h->opaque );

    return 0;
}

const cli_audio_t lavcdec_audio = {
    .open_track_lavf = open_track_lavf,
    .demux_audio     = demux_audio,
    .decode_audio    = decode_audio,
    .close_filter    = close_filter
};
