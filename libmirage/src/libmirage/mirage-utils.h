/*
 *  libMirage: Utility functions and helpers
 *  Copyright (C) 2006-2012 Rok Mandeljc
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __MIRAGE_UTILS_H__
#define __MIRAGE_UTILS_H__

G_BEGIN_DECLS


/**
 * MIRAGE_CAST_DATA:
 * @buf: buffer to get data from
 * @off: offset in buffer at which to get data
 * @type: data type (i.e. 'guint64')
 *
 * <para>
 * A macro for easy retrieval of data from (unsigned integer) buffer. Mostly to
 * be used in binary image parsers, for example, to retrieve guint32 or guint16
 * value from buffer.
 * </para>
**/
#define MIRAGE_CAST_DATA(buf,off,type) (*((type *)(buf+off)))

/**
 * MIRAGE_CAST_PTR:
 * @buf: buffer to place pointer in
 * @off: offset in buffer at which to place pointer
 * @type: pointer type (i.e. 'gchar *')
 *
 * <para>
 * A macro for easy placing of pointers within (unsigned integer) buffer. Mostly
 * to be used in binary image parsers, for example, to retrieve a string or a
 * structure from buffer.
 * </para>
**/
#define MIRAGE_CAST_PTR(buf,off,type) ((type)(buf+off))

/**
 * G_LIST_FOR_EACH:
 * @cursor: cursor
 * @list: list
 *
 * <para>
 * A macro providing for loop on GList. @cursor is a cursor of type #GList*, and
 * is used to store current element in the list. @list is a GLib's double linked
 * list that for loop should be performed on.
 * </para>
 **/
#define G_LIST_FOR_EACH(cursor,list) \
    for ((cursor) = (list); (cursor); (cursor) = (cursor)->next)

/* File finding */
gchar *mirage_helper_find_data_file (const gchar *filename, const gchar *path);

/* File suffix retrieval and matching */
gchar *mirage_helper_get_suffix (const gchar *filename);
gboolean mirage_helper_has_suffix (const gchar *filename, const gchar *suffix);

/* Case-insensitive string comparison; works on UTF-8 strings */
gint mirage_helper_strcasecmp (const gchar *str1, const gchar *str2);
gint mirage_helper_strncasecmp (const gchar *str1, const gchar *str2, gint len);

/* MSF/LBA utility functions */
void mirage_helper_lba2msf (gint lba, gboolean diff, guint8 *m, guint8 *s, guint8 *f);
gchar *mirage_helper_lba2msf_str (gint lba, gboolean diff);
gint mirage_helper_msf2lba (guint8 m, guint8 s, guint8 f, gboolean diff);
gint mirage_helper_msf2lba_str (const gchar *msf, gboolean diff);

/* Hex/BCD utility functions */
gint mirage_helper_hex2bcd (gint hex);
gint mirage_helper_bcd2hex (gint bcd);

/* ASCII/ISRC utility functions */
guint8 mirage_helper_ascii2isrc (gchar c);
gchar mirage_helper_isrc2ascii (guint8 c);

/* Subchannel utility functions */
guint16 mirage_helper_subchannel_q_calculate_crc (const guint8 *data);
void mirage_helper_subchannel_q_encode_mcn (guint8 *buf, const gchar *mcn);
void mirage_helper_subchannel_q_decode_mcn (const guint8 *buf, gchar *mcn);
void mirage_helper_subchannel_q_encode_isrc (guint8 *buf, const gchar *isrc);
void mirage_helper_subchannel_q_decode_isrc (const guint8 *buf, gchar *isrc);

/**
 * MIRAGE_SubChannel:
 * @SUBCHANNEL_W: W subchannel data
 * @SUBCHANNEL_V: V subchannel data
 * @SUBCHANNEL_U: U subchannel data
 * @SUBCHANNEL_T: T subchannel data
 * @SUBCHANNEL_S: S subchannel data
 * @SUBCHANNEL_R: R subchannel data
 * @SUBCHANNEL_Q: Q subchannel data
 * @SUBCHANNEL_P: P subchannel data
 *
 * Subchannel type for interleaving/deinterleaving.
 **/
typedef enum
{
    SUBCHANNEL_W = 0,
    SUBCHANNEL_V = 1,
    SUBCHANNEL_U = 2,
    SUBCHANNEL_T = 3,
    SUBCHANNEL_S = 4,
    SUBCHANNEL_R = 5,
    SUBCHANNEL_Q = 6,
    SUBCHANNEL_P = 7,
} MIRAGE_SubChannel;

void mirage_helper_subchannel_interleave (gint subchan, const guint8 *channel12, guint8 *channel96);
void mirage_helper_subchannel_deinterleave (gint subchan, const guint8 *channel96, guint8 *channel12);

/* EDC/ECC utility functions */
void mirage_helper_sector_edc_ecc_compute_edc_block (const guint8 *src, guint16 size, guint8 *dest);
void mirage_helper_sector_edc_ecc_compute_ecc_block (const guint8 *src, guint32 major_count, guint32 minor_count, guint32 major_mult, guint32 minor_inc, guint8 *dest);

gint mirage_helper_determine_sector_type (const guint8 *buf);

G_END_DECLS

#endif /* __MIRAGE_UTILS_H__ */
