#include "audio/audio.h"
#include "audio/audio_internal.h"

hnd_t open_audio_decoder( cli_audio_t *dec, struct AVFormatContext *ctx, int *track, int copy )
{
    assert( dec && dec->open_track_lavf );
    assert( ctx );

    audio_hnd_t *h = calloc( 1, sizeof( audio_hnd_t ) );
    h->self = dec;
    h->type = FILTER_TYPE_DECODER;
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
            h->external = 0; // XXX: HACK
            return AUDIO_OTHER;
        }

        ret = audio_queue_avpacket( h, &pkt );
        if( ret <= 0 )
            return ret;
        if( h->first_dts == AV_NOPTS_VALUE )
            h->first_dts = pkt.dts;

        return pkt.dts;
    }
    // else, packets are queued by the video demuxer

    return AUDIO_OTHER;
}

int audio_decode_main( hnd_t handle )
{
    assert( handle );
    audio_hnd_t *h = handle;
    assert( h->self && h->self->decode_audio );
    assert( h->next && h->next->info );
    assert( h->enc != h );
    if( h == h->last )
        return 0;
    if( !h->self->decode_audio )
        return AUDIO_ERROR;
    if( h->pktcount == 0 )
        return AUDIO_QUEUE_EMPTY;

    int samples_len = AUDIO_BUFSIZE * h->info->channels * h->info->samplesize;
    uint8_t *samples = malloc( samples_len );

    int len, count = 0, full = 0;
    while( ( len = h->self->decode_audio( h, samples, samples_len ) ) == AUDIO_AGAIN || len > 0 )
    {
        if( len > 0 )
        {
            if( audio_queue_rawdata( h->next, samples, len, h->pkt.dts ) == AUDIO_QUEUE_FULL )
            {
                full = 1;
                break;
            }
            ++count;
        }
    }
    assert( len != AUDIO_ERROR );
    if( len == AUDIO_ERROR )
        return AUDIO_ERROR;
    if( h->next && h->next != h->enc )
        return audio_decode_main( h->next );
    return full ? AUDIO_QUEUE_FULL : count;
}

int audio_encode( hnd_t base )
{
    audio_hnd_t *h = base;
    assert( h && h->enc && h->enc->self && h->enc->self->encode_audio );
    assert( h->muxer );

    AVPacket *pkt     = &h->enc->pkt;
    AVPacket *samples = &h->enc->pkt_temp;
    static int samples_len;
    if( samples->data == NULL )
    {
        av_init_packet( samples );
        samples_len   = AUDIO_BUFSIZE * h->info->channels * h->info->samplesize;
        samples->pos  = 0;
        samples->size = 0;
        samples->data = av_malloc( samples_len );
    }
    int enc_len = AUDIO_BUFSIZE * h->enc->info->channels * h->enc->info->samplesize;

    static int64_t dts = AV_NOPTS_VALUE;
    if( dts == AV_NOPTS_VALUE )
    {
        if( h->first_dts )
            dts = h->first_dts;
        else
            dts = h->enc->framelen;
    }
    static FILE *f = NULL, *ef = NULL;
    if( f == NULL )
    {
        f  = fopen( "audio.dump", "w" );
        ef = fopen( "audio.enc.mp3", "w" );
    }

    int bytes = 0;
    int ret = AUDIO_OK;
    while( ret != AUDIO_QUEUE_FULL && audio_dequeue_avpacket( h->enc, pkt ) )
    {
        assert( samples->size + pkt->size < samples_len );
        memcpy( samples->data + samples->pos, pkt->data, pkt->size );
        samples->size = samples->pos + pkt->size;
        samples->pos = 0;
        fwrite( pkt->data, 1, pkt->size, f );
        av_free_packet( pkt );

        while( samples->size >= h->enc->framesize )
        {
            AVPacket enc;
            av_init_packet( &enc );
            enc.data = av_malloc( enc_len );
            enc.dts  = dts;

            do
            {
                enc.size = h->enc->self->encode_audio( h, enc.data, enc_len, samples->data + samples->pos, samples->size );
            } while( enc.size == AUDIO_AGAIN );
            assert( enc.size >= 0 );
            if( enc.size > 0 )
            {
                samples->pos  += h->enc->framesize;
                samples->size -= h->enc->framesize;
                bytes += enc.size;
                ret = audio_queue_avpacket( h->muxer, &enc );
                fwrite( enc.data, 1, enc.size, ef );
                dts += h->enc->framelen;
                if( ret == AUDIO_QUEUE_FULL )
                    break;
                assert( ret == AUDIO_OK );
            }
            else
                break;
        }
        if( samples->size && samples->pos > 0)
            memmove( samples->data, samples->data + samples->pos, samples->size );
        samples->pos = samples->size;
    }

    return bytes;
}

int64_t audio_write( hnd_t base, int64_t maxdts )
{
    audio_hnd_t *h = base;
    assert( h && h->muxer && h->muxer->self && h->muxer->self->write_audio );
    AVPacket pkt = {};
    if( maxdts != -1 && h->muxer->first_pkt->pkt.dts > maxdts )
        return AUDIO_OTHER;
    int count = 0;
    int64_t lastdts = 0;

    static FILE *f = NULL;
    if( !f )
        f = fopen( "audio.mp3", "w" );

    while( maxdts == -1 || h->muxer->first_pkt->pkt.dts <= maxdts )
    {
        if( audio_dequeue_avpacket( h->muxer, &pkt ) == AUDIO_QUEUE_EMPTY )
            return AUDIO_QUEUE_EMPTY;
        fwrite( pkt.data, 1, pkt.size, f );
        h->muxer->self->write_audio( h->muxer, pkt.dts, pkt.data, pkt.size );
        ++count;
        lastdts = pkt.dts;
        av_free_packet(&pkt);
    }
    return lastdts;
}

int audio_queue_avpacket( hnd_t handle, AVPacket *pkt )
{
    audio_hnd_t *h = handle;
    if( h->seek_dts && pkt->dts < h->seek_dts )
        return AUDIO_AGAIN;
    if( h->pktcount >= AUDIO_MAX_QUEUE_LENGTH )
        return AUDIO_QUEUE_FULL;
    AVPacketList *pkt1 = av_malloc(sizeof(AVPacketList));
    if( !pkt1 || av_dup_packet(pkt) < 0 )
        return 0; // TODO: change to AUDIO_ERROR
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    if( !h->last_pkt )
        h->first_pkt = pkt1;
    else
        h->last_pkt->next = pkt1;
    h->last_pkt = pkt1;
    h->pktcount++;
    h->pktsize += pkt1->pkt.size;

    return h->pktcount >= AUDIO_MAX_QUEUE_LENGTH ? AUDIO_QUEUE_FULL : AUDIO_OK;
}

int audio_queue_rawdata( hnd_t handle, uint8_t *buf, int buflen, int64_t dts )
{
    assert( handle && buf && buflen > 0 );
    audio_hnd_t *h = handle;
    AVPacket pkt;
    av_init_packet( &pkt );
    pkt.dts = dts;
    pkt.size = buflen;
    pkt.data = av_malloc( buflen );
    assert( pkt.data );
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
        return AUDIO_OK;
    }
    return AUDIO_QUEUE_EMPTY;
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
