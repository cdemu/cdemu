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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"


/**
 * mirage_helper_find_data_file:
 * @filename: declared filename
 * @path: path where to look for file (can be a filename)
 *
 * <para>
 * Attempts to find a file with filename @filename and path @path. @filename can
 * be file's basename or an absolute path. @path can be either directory path (in
 * this case, it must end with '/') or a filename (i.e. of file descriptor).
 * </para>
 * <para>If @filename is an absolute path, its existence is first checked. If it
 * doesn't exist, @path's dirname is combined with @filename's basename, and the
 * combination's existence is checked. If that fails as well, then directory, 
 * specified by @path's dirname is opened and its content is case-insensitively
 * compared to @filename's basename. This way, all possible case variations are
 * taken into account (i.e. file.iso, FILE.ISO, FiLe.IsO, etc.). If at any of 
 * above steps the result is positive, the resulting filename is returned as a
 * newly allocated string.
 * </para>
 * <para>
 * The returned string should be freed when no longer needed.
 * </para>
 *
 * Returns: a newly allocated string containing the fullpath of file, or %NULL.
 **/
gchar *mirage_helper_find_data_file (const gchar *filename, const gchar *path)
{
    gchar *ret_filename = NULL;
    gchar *dirname = NULL;
    gchar *basename = NULL;
    GDir *dir = NULL;
    
    /* If filename is an absolute path, try using it first */
    if (g_path_is_absolute(filename)) {
        if (g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
            ret_filename = g_strdup(filename);
            goto end;
        }
    }
    
    /* Get basename from filename and dirname from path */
    basename = g_path_get_basename(filename);
    dirname = g_path_get_dirname(path);
    
    /* Try combination of dirname and basename */
    ret_filename = g_build_filename(dirname, basename, NULL);
    if (g_file_test(ret_filename, G_FILE_TEST_IS_REGULAR)) {
        goto end;
    }
    g_free(ret_filename);
    
    /* Do case-insensitive search */
    dir = g_dir_open(dirname, 0, NULL);
    if (dir) {
        const gchar *cur_filename = NULL;
        
        /* Read name of every file in dir, and compare the names ignoring case */
        while ((cur_filename = g_dir_read_name(dir))) {
            if (!mirage_helper_strcasecmp(cur_filename, basename)) {
                ret_filename = g_build_filename(dirname, cur_filename, NULL);
                if (g_file_test(ret_filename, G_FILE_TEST_IS_REGULAR)) {
                    g_dir_close(dir);
                    goto end;
                }
                g_free(ret_filename);
            }
        }
    }
    g_dir_close(dir);
    
    /* Search failed */
    ret_filename = NULL;
    
end:
    g_free(dirname);
    g_free(basename);
        
    return ret_filename;
}


/**
 * mirage_helper_get_suffix:
 * @filename: filename
 *
 * <para>
 * Retrieves suffix from @filename.
 * </para>
 *
 * Returns: pointer to character in @filename at which the suffix starts.
 **/
gchar *mirage_helper_get_suffix (const gchar *filename)
{
    return g_strrstr(filename, ".");
}

/**
 * mirage_helper_has_suffix:
 * @filename: filename
 * @suffix: suffix
 *
 * <para>
 * Checks whether file name @filename ends with suffix @suffix.
 * </para>
 *
 * Returns: %TRUE if @filename contains suffix @suffix, %FALSE if not
 **/
gboolean mirage_helper_has_suffix (const gchar *filename, const gchar *suffix)
{
    gchar *file_suffix = NULL;
    
    g_return_val_if_fail(filename != NULL, FALSE);
    g_return_val_if_fail(suffix != NULL, FALSE);
    
    file_suffix = mirage_helper_get_suffix(filename);
    
    /* If file has no suffix, don't bother */
    if (!file_suffix) {
        return FALSE;
    }
    
    return mirage_helper_strcasecmp(file_suffix, suffix) == 0;
}


/**
 * mirage_helper_strcasecmp:
 * @str1: first string
 * @str2: second string
 *
 * <para>
 * Replacement function for g_strcasecmp/strcasecmp, which can properly handle UTF-8.
 * Glib docs state this is only an approximation, albeit it should be a fairly good one.
 * </para>
 *
 * <para>
 * It compares the two strings @str1 and @str2, ignoring the case of the characters. 
 * It returns an integer less than, equal to, or greater than zero if @str1 is found, 
 * respectively, to be less than, to match, or be greater than @str2.
 * </para>
 *
 * Returns: an integer less than, equal to, or greater than zero  if  @str1  
 * is  found, respectively, to  be less than, to match, or be greater than @str2.
 **/
gint mirage_helper_strcasecmp (const gchar *str1, const gchar *str2)
{
    gchar *s1 = g_utf8_casefold(str1, -1);
	gchar *s2 = g_utf8_casefold(str2, -1);
	gint rv;
	rv = g_utf8_collate(s1, s2);
	g_free(s1);
	g_free(s2);
	return rv;
}

/**
 * mirage_helper_strncasecmp:
 * @str1: first string
 * @str2: second string
 * @len: length of string to compare
 *
 * <para>
 * Replacement function for g_strncasecmp/strncasecmp, which can properly handle UTF-8.
 * Glib docs state this is only an approximation, albeit it should be a fairly good one.
 * </para>
 *
 * <para>
 * It compares first @len characters of string @str1 and @str2, ignoring the case of 
 * the characters. It returns an integer less than, equal to, or greater than zero if 
 * first @len characters of @str1 is found, respectively, to be less than, to match, 
 * or be greater than first @len characters of @str2.
 * </para>
 *
 * Returns: an integer less than, equal to, or greater than zero  if  first @len 
 * characters of @str1 is found, respectively, to  be less than, to match, or 
 * be greater than first @len characters of @str2.
 **/
gint mirage_helper_strncasecmp (const gchar *str1, const gchar *str2, gint len)
{
    gchar *s1 = g_utf8_casefold(str1, len);
	gchar *s2 = g_utf8_casefold(str2, len);
	gint rv;
	rv = g_utf8_collate(s1, s2);
	g_free(s1);
	g_free(s2);
	return rv;
}


/**********************************************************************\
 *                      MSF/LBA utility functions                     *
\**********************************************************************/
/**
 * mirage_helper_lba2msf:
 * @lba: LBA address
 * @diff: account for the difference
 * @m: location to store minutes, or %NULL
 * @s: location to store seconds, or %NULL
 * @f: location to store frames, or %NULL
 *
 * <para>
 * Converts LBA sector address stored in @lba into MSF address, storing each field
 * into @m, @s and @f, respectively. 
 * </para>
 *
 * <para>
 * If @diff is %TRUE, 150 frames difference is accounted for; this should be 
 * used when converting absolute addresses. When converting relative addresses 
 * (or lengths), @diff should be set to %FALSE.
 * </para>
 *
 **/
void mirage_helper_lba2msf (gint lba, gboolean diff, guint8 *m, guint8 *s, guint8 *f)
{
    if (diff) {
        lba += 150;
    }
    if (m) *m = lba/(75*60);
    if (s) *s = (lba /75) % 60;
    if (f) *f = lba % 75;
        
    return;
}

/**
 * mirage_helper_lba2msf_str:
 * @lba: LBA address
 * @diff: account for the difference
 *
 * <para>
 * Converts LBA sector address stored in @lba into MSF address. 
 * </para>
 *
 * <para>
 * If @diff is %TRUE, 150 frames difference is accounted for; this should be 
 * used when converting absolute addresses. When converting relative addresses 
 * (or lengths), @diff should be set to %FALSE.
 * </para>
 *
 * Returns: a newly-allocated string containing MSF address; it should be freed 
 * with g_free() when no longer needed.
 **/
gchar *mirage_helper_lba2msf_str (gint lba, gboolean diff)
{
    gchar *ret = (gchar *)g_malloc0(10);
    
    if (diff) {
        lba += + 150;
    }
    
    sprintf(ret, "%02d:%02d:%02d", lba/(75*60), (lba/75) % 60, lba % 75);    
    return ret;
}

/**
 * mirage_helper_msf2lba:
 * @m: minutes
 * @s: seconds
 * @f: frames
 * @diff: difference
 *
 * <para>
 * Converts MSF sector address stored in @m, @s and @f into LBA address. 
 * </para>
 *
 * <para>
 * If @diff is %TRUE, 150 frames difference is accounted for; this should be 
 * used when converting absolute addresses. When converting relative addresses 
 * (or lengths), @diff should be set to %FALSE.
 * </para>
 *
 * Returns: integer representing LBA address
 **/
gint mirage_helper_msf2lba (guint8 m, guint8 s, guint8 f, gboolean diff)
{
    gint lba = (m*60+s)*75 + f;
    
    if (diff) {
        lba -= 150;
    }

    return lba;
}


/**
 * mirage_helper_msf2lba_str:
 * @msf: MSF string
 * @diff: difference
 *
 * <para>
 * Converts MSF sector address stored in @msf string into LBA address. 
 * </para>
 *
 * <para>
 * If @diff is %TRUE, 150 frames difference is accounted for; this should be 
 * used when converting absolute addresses. When converting relative addresses 
 * (or lengths), @diff should be set to %FALSE.
 * </para>
 *
 * Returns: integer representing LBA address
 **/
gint mirage_helper_msf2lba_str (const gchar *msf, gboolean diff)
{
    gint m, s, f;
    
    sscanf(msf, "%d:%d:%d", &m, &s, &f);
    return mirage_helper_msf2lba(m, s, f, diff);
}


/**********************************************************************\
 *                      Hex/BCD utility functions                     *
\**********************************************************************/
/**
 * mirage_helper_hex2bcd:
 * @hex: hex-encoded integer
 *
 * <para>
 * Converts hex-encoded integer into bcd-encoded integer.
 * </para>
 *
 * Returns: bcd-encoded integer
 **/
gint mirage_helper_hex2bcd (gint hex)
{
    if (hex >= 0 && hex <= 99) {
        return ((hex / 10) << 4) | (hex % 10);
    } else {
        return hex;
    }
}

/**
 * mirage_helper_bcd2hex:
 * @bcd: bcd-encoded integer
 *
 * <para>
 * Converts bcd-encoded integer into hex-encoded integer.
 * </para>
 *
 * Returns: hex-encoded integer
 **/
gint mirage_helper_bcd2hex (gint bcd)
{
    guint8 d1 = bcd & 0x0f;
    guint8 d2 = bcd >> 4;
    
    if (d1 <= 9 && d2 <= 9) {
        return d2 * 10 + d1;
    } else {
        return bcd;
    }
}


/**********************************************************************\
 *                     ASCII/ISRC utility functions                   *
\**********************************************************************/
/**
 * mirage_helper_ascii2isrc:
 * @c: ASCII character
 *
 * <para>
 * Converts ASCII character @c into ISRC character.
 * </para>
 *
 * Returns: ISRC character
 **/
guint8 mirage_helper_ascii2isrc (gchar c)
{
    if (g_ascii_isdigit(c)) {
        return (c - '0') & 0x3F;
    }
    if (g_ascii_isupper(c)) {
        return (c - 'A' + 17) & 0x3F;
    }
    if (g_ascii_islower(c)) {
        return (c - 'a' + 17) & 0x3F;
    }
    return 0;
}

/**
 * mirage_helper_isrc2ascii:
 * @c: ISRC character
 *
 * <para>
 * Converts ISRC character @c into ASCII character.
 * </para>
 *
 * Returns: ACSII character
 **/
gchar mirage_helper_isrc2ascii (guint8 c)
{
    if (c <= 9) {
        return '0' + c;
    }
    
    if (c >= 17 && c <= 42) {
        return 'A' + (c - 17);
    }
    
    return 0;
}

/**********************************************************************\
 *                    Subchannel utility functions                    *
\**********************************************************************/
static const guint16 q_crc_lut[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7, 0x8108,
    0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF, 0x1231, 0x0210,
    0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6, 0x9339, 0x8318, 0xB37B,
    0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE, 0x2462, 0x3443, 0x0420, 0x1401,
    0x64E6, 0x74C7, 0x44A4, 0x5485, 0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE,
    0xF5CF, 0xC5AC, 0xD58D, 0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6,
    0x5695, 0x46B4, 0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D,
    0xC7BC, 0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B, 0x5AF5,
    0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12, 0xDBFD, 0xCBDC,
    0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A, 0x6CA6, 0x7C87, 0x4CE4,
    0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41, 0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD,
    0xAD2A, 0xBD0B, 0x8D68, 0x9D49, 0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13,
    0x2E32, 0x1E51, 0x0E70, 0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A,
    0x9F59, 0x8F78, 0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E,
    0xE16F, 0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E, 0x02B1,
    0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256, 0xB5EA, 0xA5CB,
    0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D, 0x34E2, 0x24C3, 0x14A0,
    0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xA7DB, 0xB7FA, 0x8799, 0x97B8,
    0xE75F, 0xF77E, 0xC71D, 0xD73C, 0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657,
    0x7676, 0x4615, 0x5634, 0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9,
    0xB98A, 0xA9AB, 0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882,
    0x28A3, 0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92, 0xFD2E,
    0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9, 0x7C26, 0x6C07,
    0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1, 0xEF1F, 0xFF3E, 0xCF5D,
    0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8, 0x6E17, 0x7E36, 0x4E55, 0x5E74,
    0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

/**
 * mirage_helper_subchannel_q_calculate_crc:
 * @data: buffer containing Q subchannel data
 *
 * <para>
 * Calculates the CRC-16 checksum of the Q subchannel data stored in @data.
 * </para>
 *
 * Returns: CRC-16 checksum of Q subchannel data
 **/
guint16 mirage_helper_subchannel_q_calculate_crc (const guint8 *data)
{
    guint16 crc = 0;
    int i;
    
    for (i = 0; i < 10; i++) {
        crc = q_crc_lut[(crc >> 8) ^ data[i]] ^ (crc << 8);
    }
    
    return ~crc;
}

/**
 * mirage_helper_subchannel_q_encode_mcn:
 * @buf: buffer to encode MCN into
 * @mcn: MCN string
 *
 * <para>
 * Encodes MCN string @mcn into buffer @buf.
 * </para>
 **/
void mirage_helper_subchannel_q_encode_mcn (guint8 *buf, const gchar *mcn)
{
    buf[0] = ((mcn[0] - '0') << 4)  | ((mcn[1] - '0') & 0x0F);
    buf[1] = ((mcn[2] - '0') << 4)  | ((mcn[3] - '0') & 0x0F);
    buf[2] = ((mcn[4] - '0') << 4)  | ((mcn[5] - '0') & 0x0F);
    buf[3] = ((mcn[6] - '0') << 4)  | ((mcn[7] - '0') & 0x0F);
    buf[4] = ((mcn[8] - '0') << 4)  | ((mcn[9] - '0') & 0x0F);
    buf[5] = ((mcn[10] - '0') << 4) | ((mcn[11] - '0') & 0x0F);
    buf[6] = ((mcn[12] - '0') << 4);
}

/**
 * mirage_helper_subchannel_q_decode_mcn:
 * @buf: buffer containing encoded MCN
 * @mcn: string to decode MCN into
 *
 * <para>
 * Decodes MCN stored in @buf into string @mcn.
 * </para>
 **/
void mirage_helper_subchannel_q_decode_mcn (const guint8 *buf, gchar *mcn)
{
    mcn[0]  = ((buf[0] >> 4) & 0x0F) + '0';
    mcn[1]  = (buf[0] & 0x0F) + '0';
    mcn[2]  = ((buf[1] >> 4) & 0x0F) + '0';
    mcn[3]  = (buf[1] & 0x0F) + '0';
    mcn[4]  = ((buf[2] >> 4) & 0x0F) + '0';
    mcn[5]  = (buf[2] & 0x0F) + '0';
    mcn[6]  = ((buf[3] >> 4) & 0x0F) + '0';
    mcn[7]  = (buf[3] & 0x0F) + '0';
    mcn[8]  = ((buf[4] >> 4) & 0x0F) + '0';
    mcn[9]  = (buf[4] & 0x0F) + '0';
    mcn[10] = ((buf[5] >> 4) & 0x0F) + '0';
    mcn[11] = (buf[5] & 0x0F) + '0';
    mcn[12] = ((buf[6] >> 4) & 0x0F) + '0';
}


/**
 * mirage_helper_subchannel_q_encode_isrc:
 * @buf: buffer to encode ISRC into
 * @isrc: ISRC string
 *
 * <para>
 * Encodes ISRC string @isrc into buffer @buf.
 * </para>
 **/
void mirage_helper_subchannel_q_encode_isrc (guint8 *buf, const gchar *isrc)
{
    guint8 d;
    
    buf[0] = mirage_helper_ascii2isrc(isrc[0]) << 2;
        
    d = mirage_helper_ascii2isrc(isrc[1]);
    buf[0] |= d >> 4;
    buf[1] = d << 4;
    
    d = mirage_helper_ascii2isrc(isrc[2]);
    buf[1] |= d >> 2;
    buf[2] = d << 6;
    
    buf[2] |= mirage_helper_ascii2isrc(isrc[3]);
    
    buf[3] =  mirage_helper_ascii2isrc(isrc[4]) << 2;
    
    buf[4] = ((isrc[5] - '0') << 4)  | ((isrc[6] - '0') & 0x0F);
    buf[5] = ((isrc[7] - '0') << 4)  | ((isrc[8] - '0') & 0x0F);
    buf[6] = ((isrc[9] - '0') << 4)  | ((isrc[10] - '0') & 0x0F);
    buf[7] = ((isrc[11] - '0') << 4);
}

/**
 * mirage_helper_subchannel_q_decode_isrc:
 * @buf: buffer containing encoded ISRC
 * @isrc: string to decode ISRC into
 *
 * <para>
 * Decodes ISRC stored in @buf into string @isrc.
 * </para>
 **/
void mirage_helper_subchannel_q_decode_isrc (const guint8 *buf, gchar *isrc)
{
    guint8 d;
    
    d = (buf[0] >> 2) & 0x3F;
    isrc[0] = mirage_helper_isrc2ascii(d);
    
    d = ((buf[0] & 0x03) << 4) | ((buf[1] >> 4) & 0x0F);
    isrc[1] = mirage_helper_isrc2ascii(d);
    
    d = ((buf[1] & 0x0F) << 2) | ((buf[2] >> 6) & 0x03);
    isrc[2] = mirage_helper_isrc2ascii(d);
    
    d = buf[2] & 0x3F;
    isrc[3] = mirage_helper_isrc2ascii(d);
    
    d = (buf[3] >> 2) & 0x3F;
    isrc[4] = mirage_helper_isrc2ascii(d);
    
    isrc[5] = ((buf[4] >> 4) & 0x0F) + '0';
    isrc[6] = (buf[4] & 0x0F) + '0';
    isrc[7] = ((buf[5] >> 4) & 0x0F) + '0';
    isrc[8] = (buf[5] & 0x0F) + '0';
    isrc[9] = ((buf[6] >> 4) & 0x0F) + '0';
    isrc[10] = (buf[6] & 0x0F) + '0';
    isrc[11] = ((buf[7] >> 4) & 0x0F) + '0';
}

/**
 * mirage_helper_subchannel_interleave:
 * @subchan: subchannel type
 * @channel12: buffer containing subchannel data to interleave
 * @channel96: buffer to interleave subchannel data into
 *
 * <para>
 * Interleaves subchannel data of type @subchan stored in @channel12 into
 * subchannel data stored in @subchannel96.
 * </para>
 **/
void mirage_helper_subchannel_interleave (gint subchan, const guint8 *channel12, guint8 *channel96)
{
    gint i, j;
    guint8 *ptr = channel96;
    
    for (i = 0; i < 12; i++) {
        for (j = 0; j < 8; j++) {
            guint8 val = (channel12[i] & (0x01 << j)) >> j; /* Make it 1 or 0 */
            ptr[7-j] |= (val << subchan);
        }
        ptr += 8;
    }
}

/**
 * mirage_helper_subchannel_deinterleave: 
 * @subchan: subchannel type
 * @channel96: buffer containing subchannel data to deinterleave
 * @channel12: buffer to deinterleave subchannel data into
 *
 * <para>
 * Deinterleaves subchannel data of type @subchan from subchannel data stored in 
 * @channel96 and writes the resulting subhcannel data into @subchannel12.
 * </para>
 **/
void mirage_helper_subchannel_deinterleave (gint subchan, const guint8 *channel96, guint8 *channel12)
{
    gint i, j;
    
    for (i = 0; i < 12; i++) {
        for (j = 0; j < 8; j++) {        
            guint8 val = (channel96[i*8+j] & (0x01 << subchan)) >> subchan;
            channel12[i] |= (val << (7-j));
        }
    }
}


/**********************************************************************\
 *                     EDC/ECC utility functions                      *
\**********************************************************************/
/* Following code is based on Neull Corlett's ECM code */
static const guint8 ecc_f_lut[256] = {
    0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16, 
    0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22, 0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E, 
    0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E, 0x40, 0x42, 0x44, 0x46, 
    0x48, 0x4A, 0x4C, 0x4E, 0x50, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x5C, 0x5E, 
    0x60, 0x62, 0x64, 0x66, 0x68, 0x6A, 0x6C, 0x6E, 0x70, 0x72, 0x74, 0x76, 
    0x78, 0x7A, 0x7C, 0x7E, 0x80, 0x82, 0x84, 0x86, 0x88, 0x8A, 0x8C, 0x8E, 
    0x90, 0x92, 0x94, 0x96, 0x98, 0x9A, 0x9C, 0x9E, 0xA0, 0xA2, 0xA4, 0xA6, 
    0xA8, 0xAA, 0xAC, 0xAE, 0xB0, 0xB2, 0xB4, 0xB6, 0xB8, 0xBA, 0xBC, 0xBE, 
    0xC0, 0xC2, 0xC4, 0xC6, 0xC8, 0xCA, 0xCC, 0xCE, 0xD0, 0xD2, 0xD4, 0xD6, 
    0xD8, 0xDA, 0xDC, 0xDE, 0xE0, 0xE2, 0xE4, 0xE6, 0xE8, 0xEA, 0xEC, 0xEE, 
    0xF0, 0xF2, 0xF4, 0xF6, 0xF8, 0xFA, 0xFC, 0xFE, 0x1D, 0x1F, 0x19, 0x1B, 
    0x15, 0x17, 0x11, 0x13, 0x0D, 0x0F, 0x09, 0x0B, 0x05, 0x07, 0x01, 0x03, 
    0x3D, 0x3F, 0x39, 0x3B, 0x35, 0x37, 0x31, 0x33, 0x2D, 0x2F, 0x29, 0x2B, 
    0x25, 0x27, 0x21, 0x23, 0x5D, 0x5F, 0x59, 0x5B, 0x55, 0x57, 0x51, 0x53, 
    0x4D, 0x4F, 0x49, 0x4B, 0x45, 0x47, 0x41, 0x43, 0x7D, 0x7F, 0x79, 0x7B, 
    0x75, 0x77, 0x71, 0x73, 0x6D, 0x6F, 0x69, 0x6B, 0x65, 0x67, 0x61, 0x63, 
    0x9D, 0x9F, 0x99, 0x9B, 0x95, 0x97, 0x91, 0x93, 0x8D, 0x8F, 0x89, 0x8B, 
    0x85, 0x87, 0x81, 0x83, 0xBD, 0xBF, 0xB9, 0xBB, 0xB5, 0xB7, 0xB1, 0xB3, 
    0xAD, 0xAF, 0xA9, 0xAB, 0xA5, 0xA7, 0xA1, 0xA3, 0xDD, 0xDF, 0xD9, 0xDB, 
    0xD5, 0xD7, 0xD1, 0xD3, 0xCD, 0xCF, 0xC9, 0xCB, 0xC5, 0xC7, 0xC1, 0xC3, 
    0xFD, 0xFF, 0xF9, 0xFB, 0xF5, 0xF7, 0xF1, 0xF3, 0xED, 0xEF, 0xE9, 0xEB, 
    0xE5, 0xE7, 0xE1, 0xE3
};

static const guint8 ecc_b_lut[256] = {
    0x00, 0xF4, 0xF5, 0x01, 0xF7, 0x03, 0x02, 0xF6, 0xF3, 0x07, 0x06, 0xF2, 
    0x04, 0xF0, 0xF1, 0x05, 0xFB, 0x0F, 0x0E, 0xFA, 0x0C, 0xF8, 0xF9, 0x0D, 
    0x08, 0xFC, 0xFD, 0x09, 0xFF, 0x0B, 0x0A, 0xFE, 0xEB, 0x1F, 0x1E, 0xEA, 
    0x1C, 0xE8, 0xE9, 0x1D, 0x18, 0xEC, 0xED, 0x19, 0xEF, 0x1B, 0x1A, 0xEE, 
    0x10, 0xE4, 0xE5, 0x11, 0xE7, 0x13, 0x12, 0xE6, 0xE3, 0x17, 0x16, 0xE2, 
    0x14, 0xE0, 0xE1, 0x15, 0xCB, 0x3F, 0x3E, 0xCA, 0x3C, 0xC8, 0xC9, 0x3D, 
    0x38, 0xCC, 0xCD, 0x39, 0xCF, 0x3B, 0x3A, 0xCE, 0x30, 0xC4, 0xC5, 0x31, 
    0xC7, 0x33, 0x32, 0xC6, 0xC3, 0x37, 0x36, 0xC2, 0x34, 0xC0, 0xC1, 0x35, 
    0x20, 0xD4, 0xD5, 0x21, 0xD7, 0x23, 0x22, 0xD6, 0xD3, 0x27, 0x26, 0xD2, 
    0x24, 0xD0, 0xD1, 0x25, 0xDB, 0x2F, 0x2E, 0xDA, 0x2C, 0xD8, 0xD9, 0x2D, 
    0x28, 0xDC, 0xDD, 0x29, 0xDF, 0x2B, 0x2A, 0xDE, 0x8B, 0x7F, 0x7E, 0x8A, 
    0x7C, 0x88, 0x89, 0x7D, 0x78, 0x8C, 0x8D, 0x79, 0x8F, 0x7B, 0x7A, 0x8E, 
    0x70, 0x84, 0x85, 0x71, 0x87, 0x73, 0x72, 0x86, 0x83, 0x77, 0x76, 0x82, 
    0x74, 0x80, 0x81, 0x75, 0x60, 0x94, 0x95, 0x61, 0x97, 0x63, 0x62, 0x96, 
    0x93, 0x67, 0x66, 0x92, 0x64, 0x90, 0x91, 0x65, 0x9B, 0x6F, 0x6E, 0x9A, 
    0x6C, 0x98, 0x99, 0x6D, 0x68, 0x9C, 0x9D, 0x69, 0x9F, 0x6B, 0x6A, 0x9E, 
    0x40, 0xB4, 0xB5, 0x41, 0xB7, 0x43, 0x42, 0xB6, 0xB3, 0x47, 0x46, 0xB2, 
    0x44, 0xB0, 0xB1, 0x45, 0xBB, 0x4F, 0x4E, 0xBA, 0x4C, 0xB8, 0xB9, 0x4D, 
    0x48, 0xBC, 0xBD, 0x49, 0xBF, 0x4B, 0x4A, 0xBE, 0xAB, 0x5F, 0x5E, 0xAA, 
    0x5C, 0xA8, 0xA9, 0x5D, 0x58, 0xAC, 0xAD, 0x59, 0xAF, 0x5B, 0x5A, 0xAE, 
    0x50, 0xA4, 0xA5, 0x51, 0xA7, 0x53, 0x52, 0xA6, 0xA3, 0x57, 0x56, 0xA2, 
    0x54, 0xA0, 0xA1, 0x55
};

static const guint32 edc_lut[256] = {
    0x00000000, 0x90910101, 0x91210201, 0x01B00300, 0x92410401, 0x02D00500, 
    0x03600600, 0x93F10701, 0x94810801, 0x04100900, 0x05A00A00, 0x95310B01, 
    0x06C00C00, 0x96510D01, 0x97E10E01, 0x07700F00, 0x99011001, 0x09901100, 
    0x08201200, 0x98B11301, 0x0B401400, 0x9BD11501, 0x9A611601, 0x0AF01700, 
    0x0D801800, 0x9D111901, 0x9CA11A01, 0x0C301B00, 0x9FC11C01, 0x0F501D00, 
    0x0EE01E00, 0x9E711F01, 0x82012001, 0x12902100, 0x13202200, 0x83B12301, 
    0x10402400, 0x80D12501, 0x81612601, 0x11F02700, 0x16802800, 0x86112901, 
    0x87A12A01, 0x17302B00, 0x84C12C01, 0x14502D00, 0x15E02E00, 0x85712F01, 
    0x1B003000, 0x8B913101, 0x8A213201, 0x1AB03300, 0x89413401, 0x19D03500, 
    0x18603600, 0x88F13701, 0x8F813801, 0x1F103900, 0x1EA03A00, 0x8E313B01, 
    0x1DC03C00, 0x8D513D01, 0x8CE13E01, 0x1C703F00, 0xB4014001, 0x24904100, 
    0x25204200, 0xB5B14301, 0x26404400, 0xB6D14501, 0xB7614601, 0x27F04700, 
    0x20804800, 0xB0114901, 0xB1A14A01, 0x21304B00, 0xB2C14C01, 0x22504D00, 
    0x23E04E00, 0xB3714F01, 0x2D005000, 0xBD915101, 0xBC215201, 0x2CB05300, 
    0xBF415401, 0x2FD05500, 0x2E605600, 0xBEF15701, 0xB9815801, 0x29105900, 
    0x28A05A00, 0xB8315B01, 0x2BC05C00, 0xBB515D01, 0xBAE15E01, 0x2A705F00, 
    0x36006000, 0xA6916101, 0xA7216201, 0x37B06300, 0xA4416401, 0x34D06500, 
    0x35606600, 0xA5F16701, 0xA2816801, 0x32106900, 0x33A06A00, 0xA3316B01, 
    0x30C06C00, 0xA0516D01, 0xA1E16E01, 0x31706F00, 0xAF017001, 0x3F907100, 
    0x3E207200, 0xAEB17301, 0x3D407400, 0xADD17501, 0xAC617601, 0x3CF07700, 
    0x3B807800, 0xAB117901, 0xAAA17A01, 0x3A307B00, 0xA9C17C01, 0x39507D00, 
    0x38E07E00, 0xA8717F01, 0xD8018001, 0x48908100, 0x49208200, 0xD9B18301, 
    0x4A408400, 0xDAD18501, 0xDB618601, 0x4BF08700, 0x4C808800, 0xDC118901, 
    0xDDA18A01, 0x4D308B00, 0xDEC18C01, 0x4E508D00, 0x4FE08E00, 0xDF718F01, 
    0x41009000, 0xD1919101, 0xD0219201, 0x40B09300, 0xD3419401, 0x43D09500, 
    0x42609600, 0xD2F19701, 0xD5819801, 0x45109900, 0x44A09A00, 0xD4319B01, 
    0x47C09C00, 0xD7519D01, 0xD6E19E01, 0x46709F00, 0x5A00A000, 0xCA91A101, 
    0xCB21A201, 0x5BB0A300, 0xC841A401, 0x58D0A500, 0x5960A600, 0xC9F1A701, 
    0xCE81A801, 0x5E10A900, 0x5FA0AA00, 0xCF31AB01, 0x5CC0AC00, 0xCC51AD01, 
    0xCDE1AE01, 0x5D70AF00, 0xC301B001, 0x5390B100, 0x5220B200, 0xC2B1B301, 
    0x5140B400, 0xC1D1B501, 0xC061B601, 0x50F0B700, 0x5780B800, 0xC711B901, 
    0xC6A1BA01, 0x5630BB00, 0xC5C1BC01, 0x5550BD00, 0x54E0BE00, 0xC471BF01, 
    0x6C00C000, 0xFC91C101, 0xFD21C201, 0x6DB0C300, 0xFE41C401, 0x6ED0C500, 
    0x6F60C600, 0xFFF1C701, 0xF881C801, 0x6810C900, 0x69A0CA00, 0xF931CB01, 
    0x6AC0CC00, 0xFA51CD01, 0xFBE1CE01, 0x6B70CF00, 0xF501D001, 0x6590D100, 
    0x6420D200, 0xF4B1D301, 0x6740D400, 0xF7D1D501, 0xF661D601, 0x66F0D700, 
    0x6180D800, 0xF111D901, 0xF0A1DA01, 0x6030DB00, 0xF3C1DC01, 0x6350DD00, 
    0x62E0DE00, 0xF271DF01, 0xEE01E001, 0x7E90E100, 0x7F20E200, 0xEFB1E301, 
    0x7C40E400, 0xECD1E501, 0xED61E601, 0x7DF0E700, 0x7A80E800, 0xEA11E901, 
    0xEBA1EA01, 0x7B30EB00, 0xE8C1EC01, 0x7850ED00, 0x79E0EE00, 0xE971EF01, 
    0x7700F000, 0xE791F101, 0xE621F201, 0x76B0F300, 0xE541F401, 0x75D0F500, 
    0x7460F600, 0xE4F1F701, 0xE381F801, 0x7310F900, 0x72A0FA00, 0xE231FB01, 
    0x71C0FC00, 0xE151FD01, 0xE0E1FE01, 0x7070FF00
};

/**
 * mirage_helper_sector_edc_ecc_compute_edc_block:
 * @src: data to calculate EDC data for
 * @size: size of data in @src
 * @dest: buffer to write calculated EDC data into
 *
 * <para>
 * Calculates EDC (error detection code) for data in @src of length @size and
 * writes the result into @dest.
 * </para>
 *
 * <para>
 * To calculate EDC for different types of sectors and store it into sector data, use:
 * <itemizedlist>
 *  <listitem>
 *    Mode 1 sector: 
 *    <para><code>
 *    mirage_helper_sector_edc_ecc_compute_edc_block(sector_buffer+0x00, 0x810, sector_buffer+0x810);
 *    </code></para>
 *  </listitem>
 *  <listitem>
 *    Mode 2 Form 1 sector:
 *    <para><code>
 *    mirage_helper_sector_edc_ecc_compute_edc_block(sector_buffer+0x10, 0x808, sector_buffer+0x818);
 *    </code></para>
 *  </listitem>
 *  <listitem>
 *    Mode 2 Form 2 sector: 
 *    <para><code>
 *    mirage_helper_sector_edc_ecc_compute_edc_block(sector_buffer+0x10, 0x91C, sector_buffer+0x92C);
 *    </code></para>
 *  </listitem>
 * </itemizedlist>
 * (This is assuming all other sector data is already stored in sector_buffer and that sector_buffer is 2532 bytes long)
 * </para>
 **/
void mirage_helper_sector_edc_ecc_compute_edc_block (const guint8 *src, guint16 size, guint8 *dest)
{
    guint32 edc = 0;
    
    while (size--) {
        edc = (edc >> 8) ^ edc_lut[(edc ^ (*src++)) & 0xFF];
    }
    
    dest[0] = (edc >>  0) & 0xFF;
    dest[1] = (edc >>  8) & 0xFF;
    dest[2] = (edc >> 16) & 0xFF;
    dest[3] = (edc >> 24) & 0xFF;
}

/**
 * mirage_helper_sector_edc_ecc_compute_ecc_block:
 * @src: data to calculate ECC data for
 * @major_count: major count
 * @minor_count: minor count
 * @major_mult: major multiplicator
 * @minor_inc: minor increment
 * @dest: buffer to write calculated ECC data into
 *
 * <para>
 * Calculates ECC (error correction code) for data in @src and writes the result 
 * into @dest. The code assumes 2352 byte sectors. It can calculate both P and Q
 * layer of ECC data, depending on @major_count, @minor_count, @major_mult and 
 * minor_inc.
 * </para>
 *
 * <para>
 * To calculate ECC (first P, then Q layer) for different types of sectors and store it into sector data, use:
 * <itemizedlist>
 *  <listitem>
 *    Mode 1 sector:
 *    <para><code>
 *    mirage_helper_sector_edc_ecc_compute_ecc_block(sector_buffer+0xC, 86, 24, 2, 86, sector_buffer+0x81C);
 *    mirage_helper_sector_edc_ecc_compute_ecc_block(sector_buffer+0xC, 52, 43, 86, 88, sector_buffer+0x8C8);
 *    </code></para>
 *  </listitem>
 *  <listitem>
 *    Mode 2 Form 1 sector:
 *    <para><code>
 *    mirage_helper_sector_edc_ecc_compute_ecc_block(sector_buffer+0xC, 86, 24, 2, 86, sector_buffer+0x81C);
 *    mirage_helper_sector_edc_ecc_compute_ecc_block(sector_buffer+0xC, 52, 43, 86, 88, sector_buffer+0x8C8);
 *    </code></para>
 *  </listitem>
 * </itemizedlist>
 * (This is assuming all other sector data, including EDC, is already stored in sector_buffer and that sector_buffer is 2532 bytes long)
 * </para>
 **/
void mirage_helper_sector_edc_ecc_compute_ecc_block (const guint8 *src, guint32 major_count, guint32 minor_count, guint32 major_mult, guint32 minor_inc, guint8 *dest)
{
    guint32 size = major_count * minor_count;
    guint32 major = 0, minor = 0;
    
    for (major = 0; major < major_count; major++) {
        guint32 index = (major >> 1) * major_mult + (major & 1);
        guint8 ecc_a = 0;
        guint8 ecc_b = 0;
        for (minor = 0; minor < minor_count; minor++) {
            guint8 temp = src[index];
            index += minor_inc;
            if (index >= size) index -= size;
            ecc_a ^= temp;
            ecc_b ^= temp;
            ecc_a = ecc_f_lut[ecc_a];
        }
        ecc_a = ecc_b_lut[ecc_f_lut[ecc_a] ^ ecc_b];
        dest[major              ] = ecc_a;
        dest[major + major_count] = ecc_a ^ ecc_b;
    }
}
