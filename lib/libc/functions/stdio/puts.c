/* puts( const char * )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdio.h>
#include "_PDCLIB_io.h"

extern char * _PDCLIB_eol;

int _PDCLIB_puts_unlocked( const char * s )
{
    if ( _PDCLIB_prepwrite( stdout ) == EOF )
    {
        return EOF;
    }
    while ( *s != '\0' )
    {
        stdout->buffer[ stdout->bufidx++ ] = *s++;
        if ( stdout->bufidx == stdout->bufsize )
        {
            if ( _PDCLIB_flushbuffer( stdout ) == EOF )
            {
                return EOF;
            }
        }
    }
    stdout->buffer[ stdout->bufidx++ ] = '\n';
    if ( ( stdout->bufidx == stdout->bufsize ) ||
         ( stdout->status & ( _IOLBF | _IONBF ) ) )
    {
        return _PDCLIB_flushbuffer( stdout );
    }
    else
    {
        return 0;
    }
}

int puts_unlocked( const char * s )
{
    return _PDCLIB_puts_unlocked( s );
}

int puts( const char * s )
{
    _PDCLIB_flockfile( stdout );
    int r = _PDCLIB_puts_unlocked( s );
    _PDCLIB_funlockfile( stdout );
    return r;
}
