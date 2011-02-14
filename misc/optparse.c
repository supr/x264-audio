#include "optparse.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>

struct optparse_cache
{
    char *name;
    enum opt_types type;
    void *value;
};

x264_opt_t *x264_optparse( x264_opt_t *option_list, ... )
{
    if( !option_list )
        return 0;

    va_list ap, ap2;
    int error = 0;
    int i, aplen;
    va_start( ap, option_list );
    va_copy( ap2, ap );
    
    /* CACHE - caches the va_list */
    for( aplen = 0; va_arg( ap2, char* ); aplen++ )
        va_arg( ap2, void* ); // only the key is checked for NULLness
    va_end( ap2 );

    x264_opt_t *o = option_list;
    struct optparse_cache *cache = calloc( sizeof( struct optparse_cache ), (aplen+1)*2 );
    // the last item in the cache will be NULL
    if( !cache )
    {
        fprintf( stderr, "out of memory!\n" );
        error = ENOMEM;
        goto tail;
    }
    i = 0;
    while( i < aplen )
    {
        cache[i].name = strdup( va_arg( ap, char* ) );
        if( !cache[i].name )
        {
            fprintf( stderr, "out of memory!\n" );
            error = ENOMEM;
            goto tail;
        }
        cache[i].value = va_arg( ap, void* );
        char *split = strstr( cache[i].name, "=" );

        if( !split )
            cache[i++].type = OPT_TYPE_FLAG;
        else
        {
            *split++ = '\0';
            switch (*split)
            {
                case 's':
                    cache[i++].type = OPT_TYPE_STRING;
                    break;
                case 'b':
                    cache[i++].type = OPT_TYPE_BOOL;
                    break;
                case 'd':
                    cache[i++].type = OPT_TYPE_INT;
                    break;
                case 'l':
                    cache[i++].type = OPT_TYPE_LONG;
                    break;
                case 'f':
                    cache[i++].type = OPT_TYPE_FLOAT;
                    break;
                default:
                    fprintf( stderr, "Invalid option type specifier on %s\n", cache[i].name );
                    error = EINVAL;
                    goto tail;
            }
        }
    }

    x264_opt_t *iter = option_list;
    int named = 0;
    for( i = 0; o = iter; iter = iter->next, i++ )
    {
        struct optparse_cache *op = NULL;
        int flag = 1;
        if( o->name ) {
            int j;
            for( j = 0; cache[j].name; j++ )
                if( !strcmp( cache[j].name, o->name ) )
                {
                    op = &cache[j];
                    break;
                }
            if( !op )
            {
                for( j = 0; cache[j].name; j++ )
                {
                    if( cache[j].type == OPT_TYPE_FLAG )
                    {
                        if( strstr( o->name, "no" ) == o->name &&
                            !strcmp( cache[j].name, o->name + 2 ) )
                        {
                            op = &cache[j];
                            flag = 0;
                        }
                    }
                }
                if( !op )
                {
                    fprintf( stderr, "Invalid option specified: no option named '%s'\n", o->name );
                    error = EINVAL;
                    goto tail;
                }
            }
            named = 1;
        }
        else if( named || i >= aplen ) // Possible variadic arguments
            break; // Should this validate that everything after is unnamed?
        else
            op = &cache[i];
        
        switch( op->type )
        {
            case OPT_TYPE_FLAG:
                *(int*)op->value = flag;
                break;
            case OPT_TYPE_STRING:
                *(char**)op->value = o->strvalue;
                break;
            case OPT_TYPE_BOOL:
                *(int*)op->value = !strcasecmp( o->strvalue, "true" ) ||
                                   !strcmp( o->strvalue, "1" ) ||
                                   !strcasecmp( o->strvalue, "yes" );
                break;
            case OPT_TYPE_INT:
                *(int*)op->value = atoi( o->strvalue );
                break;
            case OPT_TYPE_LONG:
                *(long long*)op->value = atoll( o->strvalue );
                break;
            case OPT_TYPE_FLOAT:
                *(double*)op->value = atof( o->strvalue );
                break;
        }
    }

tail:
    if( cache )
        for( i = 0; cache[i].name; i++ )
            free( cache[i].name );
    free( cache );
    if( error )
    {
        errno = error;
        return NULL;
    }
    return o;
}
