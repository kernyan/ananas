/* freopen( const char *, const char *, FILE * )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdio.h>
#include "_PDCLIB_io.h"
#include "_PDCLIB_glue.h"
#include <stdlib.h>
#include <string.h>

FILE* freopen(
    const char* _PDCLIB_restrict filename, const char* _PDCLIB_restrict mode,
    FILE* _PDCLIB_restrict stream)
{
    _PDCLIB_flockfile(stream);

    unsigned int status = stream->status & (_IONBF | _IOLBF | _IOFBF | _PDCLIB_FREEBUFFER |
                                            _PDCLIB_DELONCLOSE | _PDCLIB_STATIC);

    /* TODO: This function can change wide orientation of a stream */
    if (stream->status & _PDCLIB_FWRITE) {
        _PDCLIB_flushbuffer(stream);
    }
    if ((filename == NULL) && (stream->filename == NULL)) {
        /* TODO: Special handling for mode changes on std-streams */
        _PDCLIB_funlockfile(stream);
        return NULL;
    }
    stream->ops->close(stream->handle);

    /* TODO: It is not nice to do this on a stream we just closed.
       It does not matter with the current implementation of clearerr(),
       but it might start to matter if someone replaced that implementation.
    */
    _PDCLIB_clearerr_unlocked(stream);
    /* The new filename might not fit the old buffer */
    if (filename == NULL) {
        /* Use previous filename */
        filename = stream->filename;
    } else if ((stream->filename != NULL) && (strlen(stream->filename) >= strlen(filename))) {
        /* Copy new filename into existing buffer */
        strcpy(stream->filename, filename);
    } else {
        /* Allocate new buffer */
        if ((stream->filename = (char*)malloc(strlen(filename))) == NULL) {
            _PDCLIB_funlockfile(stream);
            return NULL;
        }
        strcpy(stream->filename, filename);
    }
    if ((mode == NULL) || (filename[0] == '\0')) {
        _PDCLIB_funlockfile(stream);
        return NULL;
    }
    if ((stream->status = _PDCLIB_filemode(mode)) == 0) {
        _PDCLIB_funlockfile(stream);
        return NULL;
    }
    /* Re-add the flags we saved above */
    stream->status |= status;
    stream->bufidx = 0;
    stream->bufend = 0;
    stream->ungetidx = 0;
    /* TODO: Setting mbstate */
    if (!_PDCLIB_open(&stream->handle, &stream->ops, filename, stream->status)) {
        _PDCLIB_funlockfile(stream);
        return NULL;
    }
    _PDCLIB_funlockfile(stream);
    return stream;
}
