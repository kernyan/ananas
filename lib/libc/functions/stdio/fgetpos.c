/* fgetpos( FILE * , fpos_t * )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdio.h>
#include "_PDCLIB_io.h"

int _PDCLIB_fgetpos_unlocked( FILE * _PDCLIB_restrict stream, _PDCLIB_fpos_t * _PDCLIB_restrict pos )
{
    pos->offset = stream->pos.offset + stream->bufidx - stream->ungetidx;
    pos->mbs    = stream->pos.mbs;
    /* TODO: Add mbstate. */
    return 0;
}

int fgetpos_unlocked( FILE * _PDCLIB_restrict stream, _PDCLIB_fpos_t * _PDCLIB_restrict pos )
{
    return _PDCLIB_fgetpos_unlocked( stream, pos );
}

int fgetpos( FILE * _PDCLIB_restrict stream, _PDCLIB_fpos_t * _PDCLIB_restrict pos )
{
    _PDCLIB_flockfile( stream );
    int res = _PDCLIB_fgetpos_unlocked( stream, pos );
    _PDCLIB_funlockfile( stream );
    return res;
}
