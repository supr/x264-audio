#include "audio/audio.h"

int audio_queue_avpacket( audio_hnd_t *h, AVPacket *pkt )
{
    AVPacketList *pkt1 = av_malloc(sizeof(AVPacketList));
    if( !pkt1 || av_dup_packet(pkt) < 0 )
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    if( !h->last_pkt )
        h->first_pkt = pkt1;
    else
        h->last_pkt->next = pkt1;
    h->last_pkt = pkt1;
    h->pktcount++;
    h->pktsize += pkt1->pkt.size;

    return 0;
}

int audio_queue_rawdata( audio_hnd_t *h, int8_t *buf, int buflen, int64_t dts )
{
    AVPacket pkt;
    av_init_packet( &pkt );
    pkt.dts = dts * h->time_base;
    pkt.size = buflen;
    memcpy( pkt.data, buf, buflen );

    return audio_queue_avpacket( h, &pkt );
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

