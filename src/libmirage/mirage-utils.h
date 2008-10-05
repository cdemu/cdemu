/*
 *  libMirage: Utility functions and helpers
 *  Copyright (C) 2006-2008 Rok Mandeljc
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

/* Commonly used macros */
/**
 * MIRAGE_CHECK_ARG:
 * @arg: argument to be checked
 *
 * <para>
 * A macro to be used within libMirage objects. It checks whether @arg is valid
 * pointer argument (non-NULL). If it is not, it returns false and error is set
 * to %MIRAGE_E_INVALIDARG. The existance of local variable @error of type
 * #GError** is assumed.
 * </para>
 **/
#define MIRAGE_CHECK_ARG(arg) \
    if (!arg) { \
        mirage_error(MIRAGE_E_INVALIDARG, error); \
        return FALSE; \
    }

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
gchar *mirage_helper_get_suffix (gchar *filename);
gboolean mirage_helper_has_suffix (gchar *filename, gchar *suffix);
gboolean mirage_helper_match_suffixes (gchar *filename, gchar **suffixes);

/* Case-insensitive string comparison; works on UTF-8 strings */
gint mirage_helper_strcasecmp (const gchar *str1, const gchar *str2);
gint mirage_helper_strncasecmp (const gchar *str1, const gchar *str2, gint len);

/* Disc utility functions */
gint mirage_helper_guess_medium_type (MIRAGE_Disc *disc);
gboolean mirage_helper_add_redbook_pregap (MIRAGE_Disc *disc);

/* ISO Volume Descriptor utility functions */
gboolean mirage_helper_valid_iso_volume_descriptors(MIRAGE_Disc *self, FILE *file, off_t offset);

/* Parser and fragment info utility functions */
MIRAGE_ParserInfo *mirage_helper_create_parser_info (gchar *id, gchar *name, gchar *version, gchar *author, gboolean multi_file, gchar *description, gint num_suffixes, ...);
void mirage_helper_destroy_parser_info (MIRAGE_ParserInfo *info);
MIRAGE_FragmentInfo *mirage_helper_create_fragment_info (gchar *id, gchar *name, gchar *version, gchar *author, gchar *interface, gint num_suffixes, ...);
void mirage_helper_destroy_fragment_info (MIRAGE_FragmentInfo *info);

/* MSF/LBA utility functions */
void mirage_helper_lba2msf (gint lba, gboolean diff, guint8 *m, guint8 *s, guint8 *f);
gchar *mirage_helper_lba2msf_str (gint lba, gboolean diff);
gint mirage_helper_msf2lba (guint8 m, guint8 s, guint8 f, gboolean diff);
gint mirage_helper_msf2lba_str (gchar *msf, gboolean diff);

/* Hex/BCD utility functions */
gint mirage_helper_hex2bcd (gint hex);
gint mirage_helper_bcd2hex (gint bcd);

/* ASCII/ISRC utility functions */
guint8 mirage_helper_ascii2isrc (gchar c);
gchar mirage_helper_isrc2ascii (guint8 c);

/* Subchannel utility functions */
guint16 mirage_helper_subchannel_q_calculate_crc (guint8 *data);
void mirage_helper_subchannel_q_encode_mcn (guint8 *buf, gchar *mcn);
void mirage_helper_subchannel_q_decode_mcn (guint8 *buf, gchar *mcn);
void mirage_helper_subchannel_q_encode_isrc (guint8 *buf, gchar *isrc);
void mirage_helper_subchannel_q_decode_isrc (guint8 *buf, gchar *isrc);

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
typedef enum {
    SUBCHANNEL_W = 0,
    SUBCHANNEL_V = 1,
    SUBCHANNEL_U = 2,
    SUBCHANNEL_T = 3,
    SUBCHANNEL_S = 4,
    SUBCHANNEL_R = 5,
    SUBCHANNEL_Q = 6,
    SUBCHANNEL_P = 7,
} MIRAGE_SubChannel;

void mirage_helper_subchannel_interleave (gint subchan, guint8 *channel12, guint8 *channel96);
void mirage_helper_subchannel_deinterleave (gint subchan, guint8 *channel96, guint8 *channel12);

/* EDC/ECC utility functions */
void mirage_helper_sector_edc_ecc_compute_edc_block (const guint8 *src, guint16 size, guint8 *dest);
void mirage_helper_sector_edc_ecc_compute_ecc_block (guint8 *src, guint32 major_count, guint32 minor_count, guint32 major_mult, guint32 minor_inc, guint8 *dest);

G_END_DECLS

#endif /* __MIRAGE_UTILS_H__ */
