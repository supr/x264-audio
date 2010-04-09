#include "audio/audio.h"
#include "audio/audio_internal.h"

hnd_t open_audio_decoder( cli_audio_t *dec, struct AVFormatContext *ctx, int *track, int copy )
{
    assert( dec && dec->open_track_lavf );
    assert( ctx );

    audio_hnd_t *h = calloc( 1, sizeof( audio_hnd_t ) );
    h->self = dec;
    int t = *track;
    if( ( t = dec->open_track_lavf( h, ctx, t, copy ) ) == AUDIO_ERROR )
    {
        fprintf( stderr, "audio [error]: error opening audio decoder\n" );
        free( h );
        return NULL;
    }
    *track = t;

    return h;
}

static struct AVFormatContext *open_lavf_demuxer( const char *filename )
{
    AVFormatContext *lavf;

    av_register_all();
    if( !strcmp( filename, "-" ) )
        filename = "pipe:";

    if( av_open_input_file( &lavf, filename, NULL, 0, NULL ) )
    {
        fprintf( stderr, "audio [error]: could not open audio file\n" );
        goto fail;
    }

    if( av_find_stream_info( lavf ) < 0 )
    {
        fprintf( stderr, "audio [error]: could not find stream info\n" );
        goto fail;
    }

    unsigned track;
    for( track = 0;
            track < lavf->nb_streams && lavf->streams[track]->codec->codec_type != CODEC_TYPE_AUDIO; )
        ++track;

    if( track == lavf->nb_streams )
    {
        fprintf( stderr, "audio [error]: could not find any audio track on the external file" );
        goto fail;
    }
    return lavf;

fail:
    if( lavf )
        av_close_input_file( lavf );
    return NULL;
}

hnd_t open_external_audio( cli_audio_t *dec, const char *filename, int *track, int copy )
{
    audio_hnd_t *h = open_audio_decoder( dec, open_lavf_demuxer( filename ), track, copy );
    h->external = 1;
    h->first_dts = AV_NOPTS_VALUE;

    return h;
}

int64_t demux_audio( hnd_t handle )
{
    assert( handle );
    audio_hnd_t *h = handle;
    if( h->external )
    {
        assert( h->lavf );
        AVPacket pkt;
        av_init_packet( &pkt );
        pkt.stream_index = TRACK_NONE;
        int ret = 0;
        while( pkt.stream_index != h->trackid && ret >= 0 )
            ret = av_read_frame( h->lavf, &pkt );
        if( ret < 0 )
        {
            fprintf( stderr, "lavc [error]: error demuxing audio packet\n" );
            return ret;
        }

        if( audio_queue_avpacket( h, &pkt ) && h->first_dts == AV_NOPTS_VALUE )
            h->first_dts = pkt.dts;

        return pkt.dts;
    }
    // else, packets are queued by the video demuxer

    return AUDIO_NONE;
}

int audio_queue_avpacket( hnd_t handle, AVPacket *pkt )
{
    audio_hnd_t *h = handle;
    if( h->seek_dts && pkt->dts < h->seek_dts )
        return AUDIO_AGAIN;
    AVPacketList *pkt1 = av_malloc(sizeof(AVPacketList));
    if( !pkt1 || av_dup_packet(pkt) < 0 )
        return 0;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    if( !h->last_pkt )
        h->first_pkt = pkt1;
    else
        h->last_pkt->next = pkt1;
    h->last_pkt = pkt1;
    h->pktcount++;
    h->pktsize += pkt1->pkt.size;

    return 1;
}

int audio_queue_rawdata( hnd_t handle, uint8_t *buf, int buflen, int64_t dts )
{
    audio_hnd_t *h = handle;
    AVPacket pkt;
    av_init_packet( &pkt );
    pkt.dts = to_time_base( dts, h->time_base );
    pkt.size = buflen;
    pkt.data = av_malloc( buflen );
    memcpy( pkt.data, buf, buflen );

    int ret = audio_queue_avpacket( h, &pkt );
    if( ret == 0 || ret == AUDIO_AGAIN )
        av_free_packet( &pkt );
    return ret;
}

int audio_dequeue_avpacket( hnd_t handle, AVPacket *pkt )
{
    audio_hnd_t *h = handle;
    AVPacketList *pkt1 = h->first_pkt;
    if (pkt1) {
        h->first_pkt = pkt1->next;
        if( !h->first_pkt )
            h->last_pkt = NULL;
        h->pktcount--;
        h->pktsize -= pkt1->pkt.size;
        *pkt = pkt1->pkt;
        av_free(pkt1);
        return 1;
    }
    return 0;
}

void close_audio( hnd_t base )
{
    if( !base )
        return;
    audio_hnd_t *h;
    while( ( h = audio_pop_filter( base ) ) )
    {
        assert( h->self && h->self->close_filter );
        h->self->close_filter( h );
        if( h == base )
            base = NULL;
        free( h );
    }
}
