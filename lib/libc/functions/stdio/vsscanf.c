/* vsscanf( const char *, const char *, va_list )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include "_PDCLIB_io.h"

// Testing covered by scanf.cpp
int vsscanf(const char* _PDCLIB_restrict s, const char* _PDCLIB_restrict format, va_list arg)
{
    /* TODO: This function should interpret format as multibyte characters.  */
    struct _PDCLIB_status_t status;
    status.base = 0;
    status.flags = 0;
    status.n = 0;
    status.i = 0;
    status.current = 0;
    status.s = (char*)s;
    status.width = 0;
    status.prec = 0;
    status.stream = NULL;
    va_copy(status.arg, arg);

    while (*format != '\0') {
        const char* rc;
        if ((*format != '%') || ((rc = _PDCLIB_scan(format, &status)) == format)) {
            /* No conversion specifier, match verbatim */
            if (isspace(*format)) {
                /* Whitespace char in format string: Skip all whitespaces */
                /* No whitespaces in input do not result in matching error */
                while (isspace(*status.s)) {
                    ++status.s;
                    ++status.i;
                }
            } else {
                /* Non-whitespace char in format string: Match verbatim */
                if (*status.s != *format) {
                    if (*status.s == '\0' && status.n == 0) {
                        /* Early input error */
                        return EOF;
                    }
                    /* Matching error */
                    return status.n;
                } else {
                    ++status.s;
                    ++status.i;
                }
            }
            ++format;
        } else {
            /* NULL return code indicates error */
            if (rc == NULL) {
                if ((*status.s == '\n') && (status.n == 0)) {
                    status.n = EOF;
                }
                break;
            }
            /* Continue parsing after conversion specifier */
            format = rc;
        }
    }
    va_end(status.arg);
    return status.n;
}
