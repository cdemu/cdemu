/*
 *  libMirage: MDX image: multiplication in GF(2^128), required by LRW
 *  Copyright (C) 2025 Rok Mandeljc
 *
 *  Based on reverse-engineering effort from:
 *  https://github.com/Marisa-Chan/mdsx
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

#include "gf128mul.h"


/* NOTE: this is basic (= slow) implementation using peasant's algorithm.
 * If it turns out to be a bottleneck, we can optimize it via pre-computed
 * look-up table. */

static inline int is_bit_set_128 (const guint128_bbe *a, gint bit)
{
    return a->bytes[(127 - bit) / 8] & (0x80 >> ((127 - bit) % 8));
}

static inline void xor_128 (guint128_bbe *a, const guint128_bbe *b)
{
    a->words[0] ^= b->words[0];
    a->words[1] ^= b->words[1];
}

static inline void shift_left_128 (guint128_bbe *a)
{
    gint carry = 0;
    gint new_carry;

    for (gint i = 15; i >= 0; i--) {
        new_carry = (a->bytes[i] & 0x80) >> 7;
        a->bytes[i] = (a->bytes[i] << 1) | carry;
        carry = new_carry;
    }
}

void gf_mul_128 (const guint128_bbe *a, const guint128_bbe *b, guint128_bbe *p)
{
    guint128_bbe la = *a; /* Copy first operand to scratch buffer so we can left-shift it */
    p->words[0] = p->words[1] = 0;

    for (gint i = 0; i < 128; i++) {
        if (is_bit_set_128(b, i)) {
            xor_128(p, &la);
        }

        if (la.bytes[0] & 0x80) {
            shift_left_128(&la);
            la.bytes[15] ^= 0x87;
        } else {
            shift_left_128(&la);
        }
    }
}
