/*
 *  libMirage: MDX image: multiplication in GF(2^128), required by LRW
 *  Copyright (C) 2025 Rok Mandeljc
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

#pragma once

#include <glib.h>

/* By convention, LRW uses BBE (big-big-endian) bit-string representation
 * for elements of GF(2^128); the 128 bits representing the polynomial
 * coefficients are stored in two 64-bit words, with bytes in the words
 * and the bits in those bytes both being stored in big-endian order:
 *
 * 10000000 00000000 00000000 00000000 .... 00000000 00000000 00000000
 *   b[0]     b[1]     b[2]     b[3]          b[13]    b[14]    b[15]
 *
 * The left-most byte (b[0]) is the most significant byte (MSB), and the
 * left-most bit in it is the most significant bit. Therefore, the above
 * buffer represents the polynomial X^127.
 *
 * The polynomial X^7+X^2+X^1+1 has the following representation:
 *
 * 00000000 00000000 00000000 00000000 .... 00000000 00000000 10000111
 *   b[0]     b[1]     b[2]     b[3]          b[13]    b[14]    b[15]
 */

typedef union
{
    guint64 words[2];
    guint8 bytes[16];
} guint128_bbe;


void gf_mul_128 (const guint128_bbe *a, const guint128_bbe *b, guint128_bbe *p);
