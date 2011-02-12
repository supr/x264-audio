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

enum opt_types
{
    OPT_TYPE_FLAG,   // none
    OPT_TYPE_STRING, // 's'
    OPT_TYPE_BOOL,   // 'b'
    OPT_TYPE_INT,    // 'd'
    OPT_TYPE_LONG,   // 'l'
    OPT_TYPE_FLOAT,  // 'f'
};

struct optparse_cache
{
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
 * A pointer to the last x264_opt_t parsed or NULL on error.
 *
 * The returned value may not be the last in the linked list when there are
 * more items than option_names or when an unnamed argument is given after a
 * named one. This can be used to parse variadic arguments.
 *
 * OPTION TYPE SPECIFIERS
 *
 * Every option_name argument is a string in the form "argument_name=type".
 *
 * The "=type" part should not be present if the option is meant to be an
 * argumentless flag; in that case x264_optparse will set option_value to true
 * if the option_name is found at all and automatically adds a corresponding
 * "noargument_name" version that sets the flag to false if found.
 *
 * Here are the current types:
 *
 * * (no specifier) - 'int'-sized boolean
 *                    (presence is truth, presence of "no" variant is false)
 * * =s - string (just assigns the argument directly to option_value)
 * * =b - 'int'-sized boolean
 *        ("true", "yes", "1" are true, everything else false)
 * * =d - 'int'-sized integer
 * * =l - 'long long'-sized integer
 * * =f - 'double'-sized floating point number
 */
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
