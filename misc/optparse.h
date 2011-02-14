#ifndef X264_OPTPARSE_H
#define X264_OPTPARSE_H

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
x264_opt_t *x264_optparse( x264_opt_t *option_list, ... );

#endif
