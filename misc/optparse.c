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

struct optparse_cache {
    char *name;
    x264_opt_t *pointer;
};

enum opt_types {
    OPT_TYPE_STRING = 0x00, // 's'
    OPT_TYPE_BOOL   = 0x01, // 'b'
    OPT_TYPE_INT    = 0x02, // 'i' or 'd'
    OPT_TYPE_LONG   = 0x04, // 'l'
    OPT_TYPE_FLOAT  = 0x08, // 'f'
    OPT_TYPE_CODE   = 0x10  // 'c'
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
    va_list ap;
    int error = 0;
    int i;
    va_start( ap, option_list );
    
    /* CACHE - caches the named options in option_list to avoid doing *
     *         a linked list linear search for each option verified.  *
     *         Instead an array linear search will be done :)         */
    x264_opt_t *o = option_list;
    struct optparse_cache *cache = malloc( sizeof( struct optparse_cache ) * 10 );
    if( !cache ) {
        fprintf( stderr, "out of memory!\n" );
        error = ENOMEM;
        goto tail;
    }
    i = 0;
    do {
        if( !o->name ) {
            o = o->next;
            continue;
        }
        cache[i]  .name    = o->name;
        cache[i++].pointer = o;
        if( i >= 9 ) {
            cache = realloc( cache, sizeof( struct optparse_cache ) * (i+1) );
            if( !cache ) {
                fprintf( stderr, "out of memory!\n" );
                error = ENOMEM;
                goto tail;
            }
        }
        o = o->next;
    } while( o );
    cache[i] = (struct optparse_cache) {};
    
    x264_opt_t *iter = option_list;
    o = NULL;
    int named = 0;
    for( i = 0; 1; i++ ) {
        char *opname = va_arg( ap, char* );
        if( !opname ) break; // end of list
        void *opptr  = va_arg( ap, void* );
        char *split = strstr( opname, "=" );
        enum opt_types opt_type = OPT_TYPE_STRING;
        if( split++ ) {
            switch (*split) {
                case 's': // the default
                    break;
                case 'b':
                    opt_type = OPT_TYPE_BOOL;
                    break;
                case 'i':
                case 'd':
                    opt_type = OPT_TYPE_INT;
                    break;
                case 'l':
                    opt_type = OPT_TYPE_LONG;
                    break;
                case 'f':
                    opt_type = OPT_TYPE_FLOAT;
                    break;
                case 'c':
                    opt_type = OPT_TYPE_CODE;
                    break;
                default:
                    fprintf( stderr, "Invalid option type specifier on %s\n", opname );
                    error = EINVAL;
                    goto tail;
            }
        }
        
        if( cache[0].name ) {
            int j;
            for( j = 0; cache[j].name; j++ )
                if(! strcmp( cache[j].name, opname ) ) {
                    o = cache[j].pointer;
                    named = 1;
                    // do not break, only the last argument passed should be used
                }
            if( !o ) {
                if( named ) {
                    fprintf( stderr, "Positional option received after named option\n" );
                    error = EPERM;
                    goto tail;
                }
                if( !iter ) {
                    fprintf( stderr, "Too many arguments\n" );
                    error = E2BIG;
                    goto tail;
                }
                o = iter;
                iter = iter->next;
            }
        }
        
        switch( opt_type ) {
            case OPT_TYPE_INT:
                *(int*)opptr = atoi( o->strvalue );
                break;
            case OPT_TYPE_LONG:
                *(long long*)opptr = atoll( o->strvalue );
                break;
            case OPT_TYPE_FLOAT:
                *(double*)opptr = atof( o->strvalue );
                break;
            case OPT_TYPE_BOOL:
            case OPT_TYPE_STRING:
                *(char**)opptr = o->strvalue;
                break;
            case OPT_TYPE_CODE: {
                void (*func)( x264_opt_t *self ) = opptr;
                func( o );
                break;
            }
        }
    }
    tail:
    free( cache );
    if( error )
        return (error>0?-error:error);
    return i;
}