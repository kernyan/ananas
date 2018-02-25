/* UTF-8 codec

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdbool.h>
#include <stdint.h>
#include <uchar.h>
#include <assert.h>
#include "_PDCLIB_encoding.h"

/* Use of the mbstate:
 *
 * _StUC[0] is the current decoding state
 * _St32[1] is the character accumulated so far
 */

static bool utf8_mbsinit( const mbstate_t *p_s )
{ return p_s->_StUC[0] == 0; }

enum {
    DecStart = 0,

    Dec2B2,

    Dec3B2,
    Dec3B3,

    Dec4B2,
    Dec4B3,
    Dec4B4
};

#define state (p_s->_StUC[0])
#define accum (p_s->_St32[1])

#define START_CONVERSION \
    bool          result = true;           \

#define END_CONVERSION      \
end_conversion:             \
    return result

#define FINISH(_r) do {     \
    result = (_r);          \
    goto end_conversion;    \
} while(0)

#define OUT32(_c)  do {             \
    if(*p_outbuf)                   \
        (*((*p_outbuf)++)) = (_c);  \
    (*p_outsz)--;                   \
    _PDCLIB_UNDEFINED(accum);       \
    state = DecStart;               \
} while(0)

#define CHECK_CONTINUATION \
    do { if((c & 0xC0) != 0x80) return false; } while(0)

static bool utf8toc32(
    char32_t       *restrict *restrict   p_outbuf,
    size_t                   *restrict   p_outsz,
    const char     *restrict *restrict   p_inbuf,
    size_t                   *restrict   p_insz,
    mbstate_t                *restrict   p_s
)
{
    START_CONVERSION
    while(*p_outsz && *p_insz) {
        unsigned char c = **p_inbuf;
        char32_t      c32;
        switch(state) {
        case DecStart:
            // 1 byte
            if(c <= 0x7F) {
                OUT32(c);
            } else if(c <= 0xDF) {
                accum = (c & 0x1F) << 6;
                state = Dec2B2;
            } else if(c <= 0xEF) {
                accum = (c & 0x0F) << 12;
                state = Dec3B2;
            } else if(c <= 0xF4) {
                accum = (c & 0x07) << 18;
                state = Dec4B2;
            } else {
                // 5+byte sequence illegal
                FINISH(false);
            }
            break;

        case Dec2B2:
            CHECK_CONTINUATION;

            c32 = accum | (c & 0x3F);

            // Overlong sequence (e.g. NUL injection)
            if(c32 <= 0x7F)
                FINISH(false);

            OUT32(c32);
            break;

        case Dec3B2:
            CHECK_CONTINUATION;
            accum |= (c & 0x3F) << 6;
            state = Dec3B3;
            break;

        case Dec3B3:
            CHECK_CONTINUATION;

            c32 = accum | (c & 0x3F);

            // Overlong
            if(c32 <= 0x07FF)
                FINISH(false);

            // Surrogate
            if(c32 >= 0xD800 && c32 <= 0xDFFF)
                FINISH(false);

            OUT32(c32);
            break;

        case Dec4B2:
            CHECK_CONTINUATION;
            accum |= (c & 0x3F) << 12;
            state = Dec4B3;
            break;

        case Dec4B3:
            CHECK_CONTINUATION;
            accum |= (c & 0x3F) << 6;
            state = Dec4B4;
            break;

        case Dec4B4:
            CHECK_CONTINUATION;

            c32 = accum | (c & 0x3F);

            // Overlong
            if(c32 <= 0xFFFF) FINISH(false);

            // Not in Unicode
            if(c32 > 0x10FFFF) FINISH(false);

            OUT32(c32);
            break;

        default:
            assert(!"Invalid state");
        }

        (*p_inbuf)++;
        (*p_insz)--;
    }
    END_CONVERSION;
}

enum {
    EncStart = 0,
    Enc1R,
    Enc2R,
    Enc3R,
};

static bool c32toutf8(
    char           *restrict *restrict  p_outbuf,
    size_t                   *restrict  p_outsz,
    const char32_t *restrict *restrict  p_inbuf,
    size_t                   *restrict  p_insz,
    mbstate_t                *restrict  p_s
)
{
    START_CONVERSION
    while(*p_outsz) {
        unsigned char outc = 0;
        switch(state) {
        case Enc3R:
            outc = 0x80 | ((accum >> 12) & 0x3F);
            state = Enc2R;
            break;

        case Enc2R:
            outc = 0x80 | ((accum >> 6) & 0x3F);
            state = Enc1R;
            break;

        case Enc1R:
            outc = 0x80 | (accum & 0x3F);
            state = EncStart;
            _PDCLIB_UNDEFINED(accum);
            break;

        case EncStart:
            if(*p_insz == 0)
                FINISH(true);

            accum  = **p_inbuf;
            (*p_inbuf)++;
            (*p_insz)--;

            if(accum <= 0x7F) {
                outc = accum;
                state = EncStart;
                _PDCLIB_UNDEFINED(accum);
            } else if(accum <= 0x7FF) {
                outc = 0xC0 | (accum >> 6);
                state = Enc1R;
            } else if(accum <= 0xFFFF) {
                outc = 0xE0 | (accum >> 12);
                state = Enc2R;
            } else if(accum <= 0x10FFFF) {
                outc = 0xF0 | (accum >> 18);
                state = Enc3R;
            } else {
                FINISH(false);
            }
            break;
        }

        if(*p_outbuf) {
            **p_outbuf = outc;
            (*p_outbuf)++;
        }
        (*p_outsz)--;
    }
    END_CONVERSION;
}

const struct _PDCLIB_charcodec_t _PDCLIB_utf8_codec = {
    .__mbsinit   = utf8_mbsinit,
    .__mbstoc32s = utf8toc32,
    .__c32stombs = c32toutf8,
    .__mb_max    = 4,
};
