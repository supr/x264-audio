//#include "optparse.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>


typedef struct x264_opt_s
{
    char *name;
    char *strvalue;
    struct x264_opt_s *next;
} x264_opt_t;

enum opt_types {
    OPT_TYPE_STRING = 0x00, // 's'
    OPT_TYPE_BOOL   = 0x01, // 'b'
    OPT_TYPE_INT    = 0x02, // 'i' or 'd'
    OPT_TYPE_LONG   = 0x04, // 'l'
    OPT_TYPE_FLOAT  = 0x08, // 'f'
    OPT_TYPE_CODE   = 0x10  // 'c'
};

struct optparse_cache {
    char *name;
    enum opt_types type;
    void *value;
};

/*
 * x264_optparse( x264_opt_t *option_list, 
 *                char *option_name_1, void *option_value_1,
 *                ..., ...,
 *                NULL );
 * 
 * WARNING
 * Failure to put the NULL in the proper position (odd-numbered argument) WILL
 * cause stack overread and possible execution of arbitrary code.
 *
 * DESCRIPTION
 *
 * Validates and assigns the parsed options in the option_list linked list,
 * based on a key-value-like argument list with the option name/type specifier
 * in the option_name (odd) arguments and the pointers in the option_value
 * (even) arguments. The list must end with a NULL option_name argument.
 *
 * option_list is by default parsed in-order. If the 'name' value is non-NULL
 * in any of the elements, parsing happens in-order until that element is
 * reached. Afterwards, it will be used to assign only to the referred
 * option_name's option_value and only elements with the 'name' value set are
 * accepted, elements with it undefined will raise an error.
 *
 * option_value arguments are only touched if its option_name is found in the
 * option_list or, if in positional mode, there are enough arguments in
 * option_list to reach its position in the argument list. Therefore, assign
 * default values to the variables before calling x264_optparse.
 *
 * RETURNS
 * The number of arguments parsed, or a forced-negative errno on error.
 *
 * OPTION TYPE SPECIFIERS
 *
 * Every option_name argument is a string in the form "argument_name=type".
 * If omitted the "=type" part is assumed to be "=s".
 * Here are the current types:
 *
 * * =s - string (just assigns the argument string to option_value)
 * * =b - boolean
 *        (assigns the argument string to option_value as a truth value (NULL = false))
 * * =i / =d - 'int'-sized integer
 * * =l - 'long long'-sized integer
 * * =f - double-sized floating point number
 * * =c - option_value is treated as a function that returns void and receives
 *        a x264_opt_t* and is called with its own matched option struct.
 */
int x264_optparse( x264_opt_t *option_list, ... )
{
    if( !option_list ) {
        return 0;
    }
    va_list ap, ap2;
    int error = 0;
    int i, aplen;
    va_start( ap, option_list );
    va_copy( ap2, ap );
    
    /* CACHE - caches the va_list */
    for( aplen = 0; va_arg( ap2, char* ); aplen++ ) {
        va_arg( ap2, void* ); // only the key is checked for NULLness
    }
    va_end( ap2 );

    x264_opt_t *o = option_list;
    struct optparse_cache *cache = calloc( sizeof( struct optparse_cache ), (aplen+1)*2 );
    // the last item in the cache will be NULL
    if( !cache ) {
        fprintf( stderr, "out of memory!\n" );
        error = ENOMEM;
        goto tail;
    }
    i = 0;
    while( i < aplen ) {
        cache[i].name = strdup( va_arg( ap, char* ) );
        cache[i].value = va_arg( ap, void* );
        char *split = strstr( cache[i].name, "=" );
        if( split ) {
            *split++ = '\0';
            switch (*split) {
                case 's':
                    cache[i++].type = OPT_TYPE_STRING;
                    break;
                case 'b':
                    cache[i++].type = OPT_TYPE_BOOL;
                    break;
                case 'i':
                case 'd':
                    cache[i++].type = OPT_TYPE_INT;
                    break;
                case 'l':
                    cache[i++].type = OPT_TYPE_LONG;
                    break;
                case 'f':
                    cache[i++].type = OPT_TYPE_FLOAT;
                    break;
                case 'c':
                    cache[i++].type = OPT_TYPE_CODE;
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
    for( i = 0; o = iter; iter = iter->next, i++ ) {
        struct optparse_cache *op = NULL;
        if( o->name ) {
            int j;
            for( j = 0; cache[j].name; j++ )
                if( !strcmp( cache[j].name, o->name ) ) {
                    op = &cache[j];
                    break;
                }
            if( !op ) {
                fprintf( stderr, "Invalid option specified: no option named '%s'\n", o->name );
                error = EINVAL;
                goto tail;
            }
            named = 1;
        } else if( named ) {
            fprintf( stderr, "Positional option received after named option\n" );
            error = EPERM;
            goto tail;
        } else if( i >= aplen ) {
            fprintf( stderr, "Too many arguments\n" );
            error = E2BIG;
            goto tail;
        } else {
            op = &cache[i];
        }
        
        switch( op->type ) {
            case OPT_TYPE_INT:
                *(int*)op->value = atoi( o->strvalue );
                break;
            case OPT_TYPE_LONG:
                *(long long*)op->value = atoll( o->strvalue );
                break;
            case OPT_TYPE_FLOAT:
                *(double*)op->value = atof( o->strvalue );
                break;
            case OPT_TYPE_BOOL:
            case OPT_TYPE_STRING:
                *(char**)op->value = o->strvalue;
                break;
            case OPT_TYPE_CODE: {
                void (*func)( x264_opt_t *self ) = op->value;
                func( o );
                break;
            }
        }
    }

tail:
    for( i = 0; cache[i].name ; i++ )
        free( cache[i].name );
    free( cache );
    if( error )
        return (error>0?-error:error);
    return i;
}