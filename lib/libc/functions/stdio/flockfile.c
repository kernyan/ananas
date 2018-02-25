/* flockfile(FILE * )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdio.h>
#include <stdarg.h>
#include "_PDCLIB_io.h"
#include <threads.h>
#include <stdlib.h>

// Not tested here - tested by other stdio test drivers
void _PDCLIB_flockfile( FILE * file )
{
    if( mtx_lock( &file->lock ) != thrd_success ) {
        abort();
    }
}
