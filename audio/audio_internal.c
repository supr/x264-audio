#include "audio/audio_internal.h"


void audio_readjust_last( audio_hnd_t *base )
{
    assert( base );
    if( !base->next )
    {
        base->last = NULL;
        return;
    }

    audio_hnd_t *h = base;
    while( h->next )
        h = h->next;
    while( base->next )
    {
        base->last = h;
        base = base->next;
    }
    base->last = h;
}

audio_hnd_t *audio_push_filter( audio_hnd_t *base, audio_hnd_t *filter )
{
    if( !base || !filter )
        return NULL;

    assert( !base->enc ); // Cannot append filters after the encoder

    if( base->last )
    {
        base->last->next = filter;
        audio_readjust_last( base );
    }
    else
        base->next = base->last = filter;

    return filter;
}

audio_hnd_t *audio_pop_filter( audio_hnd_t *base )
{
    assert( base );
    audio_hnd_t *pre_h = base, *h = base;
    while( h->next )
    {
        pre_h = h;
        h = h->next;
    }
    pre_h->next = NULL;
    audio_readjust_last( base );
    return h;
}
