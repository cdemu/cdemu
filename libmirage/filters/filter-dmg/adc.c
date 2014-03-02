/*
 *  libMirage: DMG filter: Apple Data Compression (ADC) decompression
 *  Copyright (C) 2012-2014 Henrik Stokseth
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "adc.h"

typedef enum {
    PLAIN     = 1,
    TWOBYTE   = 2,
    THREEBYTE = 3
} ADC_ChunkType;

/* Helper functions */
static inline ADC_ChunkType adc_chunk_type(guint8 byte)
{
    ADC_ChunkType chunk_type;

    if (byte & 0x80)
        chunk_type = PLAIN;
    else if (byte & 0x40)
        chunk_type = THREEBYTE;
    else
        chunk_type = TWOBYTE;

    return chunk_type;
}

static inline guint8 adc_chunk_size(guint8 byte)
{
    guint8 chunk_size;

    switch (adc_chunk_type(byte)) {
        case PLAIN:
            chunk_size = (byte & 0x7F) + 1;
            break;
        case TWOBYTE:
            chunk_size = ((byte & 0x3F) >> 2) + 3;
            break;
        case THREEBYTE:
            chunk_size = (byte & 0x3F) + 4;
            break;
    }

    return chunk_size;
}

static inline guint16 adc_chunk_offset(guint8 *chunk_start)
{
    guint16 chunk_offset;

    switch (adc_chunk_type(*chunk_start)) {
        case PLAIN:
            chunk_offset = 0;
            break;
        case TWOBYTE:
            chunk_offset = ((((guint8) *chunk_start & 0x03)) << 8) + (guint8) *(chunk_start + 1);
            break;
        case THREEBYTE:
            chunk_offset = (((guint8) *(chunk_start + 1)) << 8) + (guint8) *(chunk_start + 2);
            break;
    }

    return chunk_offset;
}

/* Decompression routine */
gsize adc_decompress(gsize in_size, guint8 *input, gsize avail_size, guint8 *output, gsize *bytes_written)
{
    gboolean output_full = FALSE;

    guint8 *inp  = input;
    guint8 *outp = output;

    ADC_ChunkType chunk_type;

    guint8  chunk_size;
    guint16 offset;

    if (in_size == 0)
        return 0;

    while (inp - input < in_size) {
        chunk_type = adc_chunk_type(*inp);
        switch (chunk_type) {
            case PLAIN:
                chunk_size = adc_chunk_size(*inp);
                if (outp + chunk_size - output > avail_size) {
                    output_full = TRUE;
                    break;
                }
                memcpy(outp, inp + 1, chunk_size);
                inp += chunk_size + 1;
                outp += chunk_size;
                break;

            case TWOBYTE:
                chunk_size = adc_chunk_size(*inp);
                offset = adc_chunk_offset(inp);
                if (outp + chunk_size - output > avail_size) {
                    output_full = TRUE;
                    break;
                }
                if (offset == 0) {
                    memset(outp, *(outp - offset - 1), chunk_size);
                    outp += chunk_size;
                    inp += 2;
                } else {
                    for (gint16 i = 0; i < chunk_size; i++) {
                        memcpy(outp, outp - offset - 1, 1);
                        outp++;
                    }
                    inp += 2;
                }
                break;

            case THREEBYTE:
                chunk_size = adc_chunk_size(*inp);
                offset = adc_chunk_offset(inp);
                if (outp + chunk_size - output > avail_size) {
                    output_full = TRUE;
                    break;
                }
                if (offset == 0) {
                    memset(outp, *(outp - offset - 1), chunk_size);
                    outp += chunk_size;
                    inp += 3;
                } else {
                    for (gint16 i = 0; i < chunk_size; i++) {
                        memcpy(outp, outp - offset - 1, 1);
                        outp++;
                    }
                    inp += 3;
                }
                break;
        }

        if (output_full)
            break;
    }

    *bytes_written = outp - output;

    return inp - input;
}
