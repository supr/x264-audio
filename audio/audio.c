#include "audio/audio.h"
#include "audio/audio_internal.h"

audio_hnd_t *open_audio_decoder( cli_audio_t *dec, AVFormatContext *ctx, int *track, int copy )
{
    assert( dec && dec->open_track_lavf );
    assert( ctx );

    audio_hnd_t *h = calloc( 1, sizeof( audio_hnd_t ) );
    h->self = dec;
    int t = *track;
    if( ( t = dec->open_track_lavf( h, ctx, t, copy ) ) == AUDIO_ERROR )
    {
        fprintf( stderr, "audio [error]: error opening audio decoder\n" );
        return NULL;
    }
    *track = t;

    return h;
}

AVFormatContext *open_lavf_demuxer( const char *filename )
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

int audio_queue_avpacket( audio_hnd_t *h, AVPacket *pkt )
{
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

int audio_queue_rawdata( audio_hnd_t *h, uint8_t *buf, int buflen, int64_t dts )
{
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

int audio_dequeue_avpacket( audio_hnd_t *h, AVPacket *pkt )
{
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

void close_audio( audio_hnd_t *base )
{
    if( !base )
        return;
    audio_hnd_t *h;
    while( ( h = audio_pop_filter( base ) ) )
    {
        assert( h->self && h->self->close_filter );
        h->self->close_filter( h );
    }
    assert( h->self && h->self->close_filter );
    h->self->close_filter( base );
}
