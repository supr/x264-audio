#include "audio/audio_internal.h"


void audio_readjust_last( hnd_t base )
{
    assert( base );
    audio_hnd_t *b = base;
    if( !b->next )
    {
        b->last = NULL;
        return;
    }

    audio_hnd_t *h = b;
    while( h->next )
        h = h->next;
    while( b->next )
    {
        b->last = h;
        b = b->next;
    }
    b->last = h;
}

hnd_t audio_push_filter( hnd_t base, hnd_t filter )
{
    if( !base || !filter )
        return NULL;
    audio_hnd_t *b = base, *f = filter;

    assert( !b->enc ); // Cannot append filters after the encoder

    if( b->last )
    {
        b->last->next = filter;
        audio_readjust_last( b );
    }
    else
        b->next = b->last = filter;

    return f;
}

hnd_t audio_pop_filter( hnd_t base )
{
    if( !base )
        return NULL;
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
