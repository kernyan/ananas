/* strlcat( char *, const char *, size_t )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <string.h>

#pragma weak strlcat = _PDCLIB_strlcat
size_t _PDCLIB_strlcat(char* restrict dst, const char* restrict src, size_t dstsize);

size_t _PDCLIB_strlcat(char* restrict dst, const char* restrict src, size_t dstsize)
{
    size_t needed = 0;
    size_t j = 0;

    while (dst[needed])
        needed++;

    while (needed < dstsize && (dst[needed] = src[j]))
        needed++, j++;

    while (src[j++])
        needed++;
    needed++;

    if (needed > dstsize && dstsize)
        dst[dstsize - 1] = 0;

    return needed;
}
