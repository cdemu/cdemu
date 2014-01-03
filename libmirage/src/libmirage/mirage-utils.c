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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION: mirage-utils
 * @title: Utilities
 * @short_description: Various helper and utility functions.
 * @include: mirage-utils.h
 *
 * These functions cover various functionality. They are exported
 * because, while primarily designed to be used within libMirage, they
 * could also prove useful to other applications.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"


/**********************************************************************\
 *                           Data patterns                            *
\**********************************************************************/
/**
 * mirage_pattern_sync:
 *
 * A 12-byte sync pattern, found at the beginning of non-audio sectors.
 */
const guint8 mirage_pattern_sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };

/**
 * mirage_pattern_cd001:
 *
 * A 8-byte CD001 pattern, found at 16th sector of ISO data tracks.
 */
const guint8 mirage_pattern_cd001[8] = { 0x01, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00 };


/**
 * mirage_pattern_bea01:
 *
 * A 8-byte BEA01 pattern, found at 16th sector of UDF data tracks.
 */
const guint8 mirage_pattern_bea01[8] = { 0x00, 0x42, 0x45, 0x41, 0x30, 0x31, 0x01, 0x00 };


/**********************************************************************\
 *                       File helper functions                        *
\**********************************************************************/
/* Helper for mirage_find_data_file() */
static gchar *find_data_file (const gchar *path, const gchar *filename)
{
    const gchar *test_filename;
    gchar *ret_filename;
    GDir *dir;
    gint filename_len, test_filename_len, ret_filename_len;

    /* Try combining path and basename first */
    ret_filename = g_build_filename(path, filename, NULL);
    if (g_file_test(ret_filename, G_FILE_TEST_IS_REGULAR)) {
        return ret_filename;
    }
    g_free(ret_filename);

    /* Do case-insensitive search among the files found in path. Match
       only against beginning of the hypothesized filename, so that we
       also get results with additional suffices. E.g., if we search
       for file.dat, accept file.dat.gz as well. But always return the
       shortest match. */
    dir = g_dir_open(path, 0, NULL);
    if (!dir) {
        return NULL;
    }

    /* Store length of input filename */
    filename_len = strlen(filename);

    ret_filename = NULL;
    ret_filename_len = G_MAXINT;
    while ((test_filename = g_dir_read_name(dir))) {
        /* Make sure beginning matches */
        if (mirage_helper_strncasecmp(test_filename, filename, filename_len)) {
            continue;
        }

        /* Now check if the name is shorter than what we already have */
        test_filename_len = strlen(test_filename);

        if (test_filename_len < ret_filename_len) {
            /* Make sure file is valid */
            gchar *full_filename = g_build_filename(path, test_filename, NULL);

            if (g_file_test(full_filename, G_FILE_TEST_IS_REGULAR)) {
                g_free(ret_filename);
                ret_filename = full_filename;
                ret_filename_len = test_filename_len;
            } else {
                g_free(full_filename);
            }
        }
    }

    g_dir_close(dir);

    return ret_filename;
}

/**
 * mirage_helper_find_data_file:
 * @filename: (in): declared filename
 * @path: (in) (allow-none): path where to look for file (can be a filename), or %NULL
 *
 * Attempts to find a file with filename @filename and path @path. @filename can
 * be file's basename or an absolute path. @path can be either directory path (in
 * this case, it must end with '/') or a filename (i.e. of file descriptor).
 *
 * If @filename is an absolute path, its existence is first checked. If it
 * does not exist, search (see below) is performed in @filename's dirname. If
 * still no match is found and @path is not %NULL, @path's dirname is combined
 * with @filename's basename, and the combination's existence is checked. If
 * that fails as well, search (see below) is performed in @path's dirname.
 * Searching in the directory is performed as follows. Directory is opened
 * and its content is case-insensitively compared to @filename's basename.
 * All filenames whose beginning match @filename are considered, and the
 * shortest one is returned. This way, all possible case variations
 * (i.e. file.iso, FILE.ISO, FiLe.IsO, etc.) are taken into account.
 * This function can return a filename with additional suffices, but only if
 * a file without those extra suffices does not exist. E.g., if search is
 * done for 'data.img', and only 'data.img.gz' exists, it will be returned.
 * However, if both 'data.img' and 'data.img.gz' exist, the former will be
 * returned.
 * The returned string should be freed when no longer needed.
 *
 * Returns: a newly allocated string containing the fullpath of file, or %NULL.
 */
gchar *mirage_helper_find_data_file (const gchar *filename, const gchar *path)
{
    gchar *ret_filename = NULL;
    gchar *dirname;
    gchar *basename;

    /* We'll need basename either way */
    basename = g_path_get_basename(filename);

    /* If filename is an absolute path, try using it first */
    if (g_path_is_absolute(filename)) {
        /* Directory name from filename */
        dirname = g_path_get_dirname(filename);

        /* Find the filename using our helper */
        ret_filename = find_data_file(dirname, basename);

        g_free(dirname);

        /* If found, return */
        if (ret_filename) {
            g_free(basename);
            return ret_filename;
        }
    }

    /* If path is provided, try using it */
    if (path) {
        /* Directory name from provided path */
        dirname = g_path_get_dirname(path);

        /* Find the filename using our helper */
        ret_filename = find_data_file(dirname, basename);

        g_free(dirname);
    }

    g_free(basename);

    /* Either we found it, or we return NULL */
    return ret_filename;
}


/**
 * mirage_helper_get_suffix:
 * @filename: (in): filename
 *
 * Retrieves suffix from @filename.
 *
 * Returns: (transfer none): pointer to character in @filename at which the suffix starts.
 */
const gchar *mirage_helper_get_suffix (const gchar *filename)
{
    return g_strrstr(filename, ".");
}

/**
 * mirage_helper_has_suffix:
 * @filename: (in): filename
 * @suffix: (in): suffix
 *
 * Checks whether file name @filename ends with suffix @suffix.
 *
 * Returns: %TRUE if @filename contains suffix @suffix, %FALSE if not
 */
gboolean mirage_helper_has_suffix (const gchar *filename, const gchar *suffix)
{
    g_return_val_if_fail(filename != NULL, FALSE);
    g_return_val_if_fail(suffix != NULL, FALSE);

    const gchar *file_suffix = mirage_helper_get_suffix(filename);

    /* If file has no suffix, don't bother */
    if (!file_suffix) {
        return FALSE;
    }

    return mirage_helper_strcasecmp(file_suffix, suffix) == 0;
}


/**
 * mirage_helper_strcasecmp:
 * @str1: (in): first string
 * @str2: (in): second string
 *
 * Replacement function for g_strcasecmp/strcasecmp, which can properly handle UTF-8.
 * Glib docs state this is only an approximation, albeit it should be a fairly good one.
 *
 * It compares the two strings @str1 and @str2, ignoring the case of the characters.
 * It returns an integer less than, equal to, or greater than zero if @str1 is found,
 * respectively, to be less than, to match, or be greater than @str2.
 *
 * Returns: an integer less than, equal to, or greater than zero  if  @str1
 * is  found, respectively, to  be less than, to match, or be greater than @str2.
 */
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
 * @str1: (in): first string
 * @str2: (in): second string
 * @len: (in): length of string to compare
 *
 * Replacement function for g_strncasecmp/strncasecmp, which can properly handle UTF-8.
 * Glib docs state this is only an approximation, albeit it should be a fairly good one.
 *
 * It compares first @len characters of string @str1 and @str2, ignoring the case of
 * the characters. It returns an integer less than, equal to, or greater than zero if
 * first @len characters of @str1 is found, respectively, to be less than, to match,
 * or be greater than first @len characters of @str2.
 *
 * Returns: an integer less than, equal to, or greater than zero  if  first @len
 * characters of @str1 is found, respectively, to  be less than, to match, or
 * be greater than first @len characters of @str2.
 */
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
 * @lba: (in): LBA address
 * @diff: (in): account for the difference
 * @m: (out) (allow-none): location to store minutes, or %NULL
 * @s: (out) (allow-none): location to store seconds, or %NULL
 * @f: (out) (allow-none): location to store frames, or %NULL
 *
 * Converts LBA sector address stored in @lba into MSF address, storing each field
 * into @m, @s and @f, respectively.
 *
 * If @diff is %TRUE, 150 frames difference is accounted for; this should be
 * used when converting absolute addresses. When converting relative addresses
 * (or lengths), @diff should be set to %FALSE.
 *
 */
void mirage_helper_lba2msf (gint lba, gboolean diff, guint8 *m, guint8 *s, guint8 *f)
{
    if (diff) {
        lba += 150;
    }

    if (lba < 0) {
        lba += 450000;
    }

    if (m) *m = lba/(75*60);
    if (s) *s = (lba /75) % 60;
    if (f) *f = lba % 75;

    return;
}

/**
 * mirage_helper_lba2msf_str:
 * @lba: (in): LBA address
 * @diff: (in): account for the difference
 *
 * Converts LBA sector address stored in @lba into MSF address.
 *
 * If @diff is %TRUE, 150 frames difference is accounted for; this should be
 * used when converting absolute addresses. When converting relative addresses
 * (or lengths), @diff should be set to %FALSE.
 *
 * Returns: a newly-allocated string containing MSF address; it should be freed
 * with g_free() when no longer needed.
 */
gchar *mirage_helper_lba2msf_str (gint lba, gboolean diff)
{
    gchar *ret;

    if (diff) {
        lba += 150;
    }

    if (lba < 0) {
        lba += 450000;
    }

    ret = g_strdup_printf("%02d:%02d:%02d", lba/(75*60), (lba/75) % 60, lba % 75);
    return ret;
}

/**
 * mirage_helper_msf2lba:
 * @m: (in): minutes
 * @s: (in): seconds
 * @f: (in): frames
 * @diff: (in): difference
 *
 * Converts MSF sector address stored in @m, @s and @f into LBA address.
 *
 * If @diff is %TRUE, 150 frames difference is accounted for; this should be
 * used when converting absolute addresses. When converting relative addresses
 * (or lengths), @diff should be set to %FALSE.
 *
 * Returns: integer representing LBA address
 */
gint mirage_helper_msf2lba (guint8 m, guint8 s, guint8 f, gboolean diff)
{
    gint lba = (m*60+s)*75 + f;

    if (diff) {
        lba -= 150;
    }

    if (m >= 90) {
        lba -= 450000;
    }

    return lba;
}


/**
 * mirage_helper_msf2lba_str:
 * @msf: (in): MSF string
 * @diff: (in): difference
 *
 * Converts MSF sector address stored in @msf string into LBA address.
 *
 * If @diff is %TRUE, 150 frames difference is accounted for; this should be
 * used when converting absolute addresses. When converting relative addresses
 * (or lengths), @diff should be set to %FALSE.
 *
 * Returns: integer representing LBA address or -1 on failure.
 */
gint mirage_helper_msf2lba_str (const gchar *msf, gboolean diff)
{
    gint ret, m, s, f;

    if (!msf) return -1;
    ret = sscanf(msf, "%d:%d:%d", &m, &s, &f);
    if (ret != 3) return -1;
    return mirage_helper_msf2lba(m, s, f, diff);
}


/**********************************************************************\
 *                      Hex/BCD utility functions                     *
\**********************************************************************/
/**
 * mirage_helper_hex2bcd:
 * @hex: (in): hex-encoded integer
 *
 * Converts hex-encoded integer into bcd-encoded integer.
 *
 * Returns: bcd-encoded integer
 */
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
 * @bcd: (in): bcd-encoded integer
 *
 * Converts bcd-encoded integer into hex-encoded integer.
 *
 * Returns: hex-encoded integer
 */
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
 * @c: (in): ASCII character
 *
 * Converts ASCII character @c into ISRC character.
 *
 * Returns: ISRC character
 */
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
 * @c: (in): ISRC character
 *
 * Converts ISRC character @c into ASCII character.
 *
 * Returns: ACSII character
 */
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

/**
 * mirage_helper_validate_isrc:
 * @isrc: (in) (array fixed-size=12): An ASCII encoded ISRC string.
 *
 * Performs a limited validation of an ISRC string.
 *
 * Returns: TRUE or FALSE
 */
gboolean mirage_helper_validate_isrc (const gchar *isrc)
{
    if (!isrc) return FALSE;

    if (g_ascii_isalpha(isrc[ 0]) &&
        g_ascii_isalpha(isrc[ 1]) &&

        g_ascii_isalnum(isrc[ 2]) &&
        g_ascii_isalnum(isrc[ 3]) &&
        g_ascii_isalnum(isrc[ 4]) &&

        g_ascii_isdigit(isrc[ 5]) &&
        g_ascii_isdigit(isrc[ 6]) &&

        g_ascii_isdigit(isrc[ 7]) &&
        g_ascii_isdigit(isrc[ 8]) &&
        g_ascii_isdigit(isrc[ 9]) &&
        g_ascii_isdigit(isrc[10]) &&
        g_ascii_isdigit(isrc[11]))
    {
        return TRUE;
    }

    return FALSE;
}


/**********************************************************************\
 *               Cyclic Redundancy Check (CRC) routines               *
\**********************************************************************/

/* The lookup table was generated from the polynomial given as:
 * P(x) = x^16 + x^12 + x^5 + x^0 =
 * x^16 + x^12 + x^5 + 1
 * Where only the 16 lowest bits are used:
 * 0x11021 & 0xFFFF = 0x1021
 */
guint16 *crc16_1021_lut = NULL;

/* The lookup table was generated from the polynomial given as:
 * P(x) = (x^16 + x^15 + x^2 + x^0) * (x^16 + x^2 + x^1 + x^0) =
 * x^32 + x^31 + x^16 + x^15 + x^4 + x^3 + x + 1
 * Where only the 32 lowest bits are used:
 * 0x18001801B & 0xFFFFFFFF = 0x8001801B
 * This value is then reflected (reversed bitwise):
 * 0x8001801B -> 0xD8018001
 */
guint32 *crc32_d8018001_lut = NULL;

/**
 * mirage_helper_init_crc16_lut:
 * @genpoly: (in): generator polynomial
 *
 * Calculates a look-up table for CRC16 based on the generator polynomial.
 *
 * Returns: Pointer to the CRC16 look-up table or NULL on failure.
 */
guint16 *mirage_helper_init_crc16_lut(guint16 genpoly)
{
    guint16 *crc16_lut = g_try_new(guint16, 256);
    if (!crc16_lut) {
        return NULL;
    }

    /* Generate look-up table */
    for (guint i = 0; i < 256; ++i) {
        guint16 value = 0;
        guint16 temp = i << 8;

        for(guint j = 0; j < 8; ++j) {
            if ((value ^ temp) & 0x8000) {
                value = ((value << 1) ^ genpoly);
            } else {
                value <<= 1;
            }
            temp <<= 1;
        }

        crc16_lut[i] = value;
    }

    return crc16_lut;
}

#define CRC32_LUT(tab, idx) crc32_lut[(tab) * 256 + (idx)]

/**
 * mirage_helper_init_crc32_lut:
 * @genpoly: (in): generator polynomial
 * @slices: (in): number of bytes to process at once
 *
 * Calculates a look-up table for CRC32 based on the generator polynomial.
 * The size of the lookup table depends on @slices. The standard algorithm
 * processes 1 byte at a time and has a look-up table size of 1KiB, whereas
 * The slice-by-4 and slice-by-8 algorithms use 4 and 8 KiB look-up tables that
 * are derived from the initial look-up table.
 *
 * Returns: Pointer to the CRC32 look-up table or NULL on failure.
 */
guint32 *mirage_helper_init_crc32_lut(guint32 genpoly, guint slices)
{
    /* Check if slices in in the valid range */
    if (slices < 1 || slices > 8) {
        return NULL;
    }

    guint32 *crc32_lut = g_try_new(guint32, slices * 256);
    if (!crc32_lut) {
        return NULL;
    }

    /* Generate look-up table for slice-by-1 */
    if (slices >= 1) {
        for (guint i = 0; i < 256; i++) {
            guint32 crc = i;
            for (guint j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ ((crc & 1) * genpoly);
            }
            CRC32_LUT(0, i) = crc;
        }
    }

    /* Generate look-up tables for slice-by-4 (and slice-by-8) */
    if (slices >= 4) {
        for (guint i = 0; i <= 256; i++) {
            CRC32_LUT(1, i) = (CRC32_LUT(0, i) >> 8) ^ CRC32_LUT(0, CRC32_LUT(0, i) & 0xFF);
            CRC32_LUT(2, i) = (CRC32_LUT(1, i) >> 8) ^ CRC32_LUT(0, CRC32_LUT(1, i) & 0xFF);
            CRC32_LUT(3, i) = (CRC32_LUT(2, i) >> 8) ^ CRC32_LUT(0, CRC32_LUT(2, i) & 0xFF);
        }
    }

    /* Generate look-up tables for slice-by-8 */
    if (slices >= 8) {
        for (guint i = 0; i <= 256; i++) {
            CRC32_LUT(4, i) = (CRC32_LUT(3, i) >> 8) ^ CRC32_LUT(0, CRC32_LUT(3, i) & 0xFF);
            CRC32_LUT(5, i) = (CRC32_LUT(4, i) >> 8) ^ CRC32_LUT(0, CRC32_LUT(4, i) & 0xFF);
            CRC32_LUT(6, i) = (CRC32_LUT(5, i) >> 8) ^ CRC32_LUT(0, CRC32_LUT(5, i) & 0xFF);
            CRC32_LUT(7, i) = (CRC32_LUT(6, i) >> 8) ^ CRC32_LUT(0, CRC32_LUT(6, i) & 0xFF);
        }
    }

    return crc32_lut;
}

/**
 * mirage_helper_calculate_crc16:
 * @data: (in) (array length=length): buffer containing data
 * @length: (in): length of data
 * @crctab: (in) (array fixed-size=256): pointer to CRC polynomial table
 * @reflected: (in): whether to use the reflected algorithm
 * @invert: (in): whether the result should be inverted
 *
 * Calculates the CRC-16 checksum of the data stored in @data.
 *
 * Returns: CRC-16 checksum of data
 */
guint16 mirage_helper_calculate_crc16(const guint8 *data, guint length, const guint16 *crctab, gboolean reflected, gboolean invert)
{
    guint16 crc = 0;

    g_assert(data && crctab);

    if (!reflected) {
        while (length--) {
            crc = (crc << 8) ^ crctab[(crc >> 8) ^ *data++];
        }
    } else {
        while (length--) {
            crc = (crc >> 8) ^ crctab[(crc & 0xFF) ^ *data++];
        }
    }

    if (invert) {
        crc = ~crc;
    }

    return crc;
}

/**
 * mirage_helper_calculate_crc32f:
 * @data: (in) (array length=length): buffer containing data
 * @length: (in): length of data
 * @crctab: (in) (array fixed-size=2048): pointer to CRC polynomial table
 * @reflected: (in): whether to use the reflected algorithm
 * @invert: (in): whether the initial value and result should be inverted
 *
 * Calculates the CRC-32 checksum of the data stored in @data.
 * Fast slice-by-8 implementation that processes 8 bytes at a time.
 *
 * Returns: CRC-32 checksum of data
 */
guint32 mirage_helper_calculate_crc32f(const guint8 *data, guint length, const guint32 *crctab, gboolean reflected, gboolean invert)
{
    guint32 crc = 0;

    const guint32 *crc32_lut = crctab;

    guint32 *current = (guint32*) data;
    guint8 *current2 = (guint8*) data;

    g_assert(data && crctab);

    if (invert) {
        crc = ~crc;
    }

    if (!reflected) {
        /* FIXME: Implement non-reflected version of slicing-by-8 algorithm */
        while (length--) {
            crc = (crc << 8) ^ crctab[(crc >> 24) ^ *data++];
        }
    } else {
        /* Process any initial un-aligned bytes */
        guint ub = ((gulong) current) % sizeof(guint64);

        if (ub) {
            guint temp = ub = sizeof(guint64) - ub;

            while (temp--) {
                crc = (crc >> 8) ^ CRC32_LUT(0, (crc & 0xFF) ^ *current2++);
            }

            current = (guint32*) ((guint8*) current + ub);
            length -= ub;
        }

        /* Make sure we are 64-bit aligned here */
        g_assert((((gulong) current) % sizeof(guint64)) == 0);

        /* Process eight bytes at once */
        while (length >= 8) {
            #if G_BYTE_ORDER == G_LITTLE_ENDIAN
            guint32 one = *current++ ^ crc;
            guint32 two = *current++;
            crc = CRC32_LUT(0, (two >> 24)       ) ^
                  CRC32_LUT(1, (two >> 16) & 0xFF) ^
                  CRC32_LUT(2, (two >> 8 ) & 0xFF) ^
                  CRC32_LUT(3, (two      ) & 0xFF) ^
                  CRC32_LUT(4, (one >> 24)       ) ^
                  CRC32_LUT(5, (one >> 16) & 0xFF) ^
                  CRC32_LUT(6, (one >> 8 ) & 0xFF) ^
                  CRC32_LUT(7, (one      ) & 0xFF);
            #elif G_BYTE_ORDER == G_BIG_ENDIAN
            guint32 one = *current++ ^ GUINT32_SWAP_LE_BE(crc);
            guint32 two = *current++;
            crc = CRC32_LUT(0, (two      ) & 0xFF) ^
                  CRC32_LUT(1, (two >> 8 ) & 0xFF) ^
                  CRC32_LUT(2, (two >> 16) & 0xFF) ^
                  CRC32_LUT(3, (two >> 24)       ) ^
                  CRC32_LUT(4, (one      ) & 0xFF) ^
                  CRC32_LUT(5, (one >> 8 ) & 0xFF) ^
                  CRC32_LUT(6, (one >> 16) & 0xFF) ^
                  CRC32_LUT(7, (one >> 24)       );
            #else
            g_assert_not_reached();
            #endif
            length -= 8;
        }

        current2 = (guint8*) current;

        /* Process remaining 1 to 7 bytes */
        while (length--) {
            crc = (crc >> 8) ^ CRC32_LUT(0, (crc & 0xFF) ^ *current2++);
        }
    }

    if (invert) {
        crc = ~crc;
    }

    return crc;
}

/**
 * mirage_helper_calculate_crc32s:
 * @data: (in) (array length=length): buffer containing data
 * @length: (in): length of data
 * @crctab: (in) (array fixed-size=256): pointer to CRC polynomial table
 * @reflected: (in): whether to use the reflected algorithm
 * @invert: (in): whether the initial value and result should be inverted
 *
 * Calculates the CRC-32 checksum of the data stored in @data.
 * Standard inplementation that processes 1 byte at a time.
 *
 * Returns: CRC-32 checksum of data
 */
guint32 mirage_helper_calculate_crc32s(const guint8 *data, guint length, const guint32 *crctab, gboolean reflected, gboolean invert)
{
    guint32 crc = 0;

    if (invert) {
        crc = ~crc;
    }

    g_assert(data && crctab);

    if (!reflected) {
        while (length--) {
            crc = (crc << 8) ^ crctab[(crc >> 24) ^ *data++];
        }
    } else {
        while (length--) {
            crc = (crc >> 8) ^ crctab[(crc & 0xFF) ^ *data++];
        }
    }

    if (invert) {
        crc = ~crc;
    }

    return crc;
}


/**********************************************************************\
 *                    Subchannel utility functions                    *
\**********************************************************************/
/**
 * mirage_helper_subchannel_q_calculate_crc:
 * @data: (in) (array fixed-size=10): buffer containing Q subchannel data (10 bytes)
 *
 * Calculates the CRC-16 checksum of the Q subchannel data stored in @data.
 *
 * Returns: CRC-16 checksum of Q subchannel data
 */
guint16 mirage_helper_subchannel_q_calculate_crc (const guint8 *data)
{
    return mirage_helper_calculate_crc16(data, 10, crc16_1021_lut, FALSE, TRUE);
}

/**
 * mirage_helper_subchannel_q_encode_mcn:
 * @buf: (out caller-allocates) (array fixed-size=7): buffer to encode MCN into (7 bytes)
 * @mcn: (in) (array fixed-size=13): MCN string (13 bytes)
 *
 * Encodes MCN string @mcn into buffer @buf.
 */
void mirage_helper_subchannel_q_encode_mcn (guint8 *buf, const gchar *mcn)
{
    const gchar *m = mcn;

    for (guint i = 0; i < 6; i++) {
        guint8 val;

        val  = (*m++ - '0') << 4;
        val |= (*m++ - '0') & 0x0F;

        buf[i] = val;
    }
    buf[6] = (*m++ - '0') << 4;
}

/**
 * mirage_helper_subchannel_q_decode_mcn:
 * @buf: (in) (array fixed-size=7): buffer containing encoded MCN (7 bytes)
 * @mcn: (out caller-allocates) (array fixed-size=13): string to decode MCN into (13 bytes)
 *
 * Decodes MCN stored in @buf into string @mcn.
 */
void mirage_helper_subchannel_q_decode_mcn (const guint8 *buf, gchar *mcn)
{
    gchar *m = mcn;

    for (guint i = 0; i < 6; i++) {
        *m++ = (buf[i] >> 4) + '0';
        *m++ = (buf[i] & 0x0F) + '0';
    }
    *m++ = (buf[6] >> 4) + '0';
}


/**
 * mirage_helper_subchannel_q_encode_isrc:
 * @buf: (out caller-allocates) (array fixed-size=8): buffer to encode ISRC into (8 bytes)
 * @isrc: (in) (array fixed-size=12): ISRC string (12 bytes)
 *
 * Encodes ISRC string @isrc into buffer @buf.
 */
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
 * @buf: (in) (array fixed-size=8): buffer containing encoded ISRC (8 bytes)
 * @isrc: (out caller-allocates) (array fixed-size=12): string to decode ISRC into (12 bytes)
 *
 * Decodes ISRC stored in @buf into string @isrc.
 */
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
 * @subchan: (in): subchannel type
 * @channel12: (in) (array fixed-size=12): buffer containing subchannel data to interleave (12 bytes)
 * @channel96: (out caller-allocates) (array fixed-size=96): buffer to interleave subchannel data into (96 bytes)
 *
 * Interleaves subchannel data of type @subchan stored in @channel12 into
 * subchannel data stored in @subchannel96.
 */
void mirage_helper_subchannel_interleave (gint subchan, const guint8 *channel12, guint8 *channel96)
{
    guint8 *ptr = channel96;
    guint8 val;

    for (gint i = 0; i < 12; i++) {
        for (gint j = 0; j < 8; j++) {
            val = (channel12[i] & (0x01 << j)) >> j; /* Make it 1 or 0 */
            ptr[7-j] |= (val << subchan);
        }
        ptr += 8;
    }
}

/**
 * mirage_helper_subchannel_deinterleave:
 * @subchan: (in): subchannel type
 * @channel96: (in) (array fixed-size=96): buffer containing subchannel data to deinterleave (96 bytes)
 * @channel12: (out caller-allocates) (array fixed-size=12): buffer to deinterleave subchannel data into (12 bytes)
 *
 * Deinterleaves subchannel data of type @subchan from subchannel data stored in
 * @channel96 and writes the resulting subhcannel data into @subchannel12.
 */
void mirage_helper_subchannel_deinterleave (gint subchan, const guint8 *channel96, guint8 *channel12)
{
    for (gint i = 0; i < 12; i++) {
        for (gint j = 0; j < 8; j++) {
            guint8 val = (channel96[i*8+j] & (0x01 << subchan)) >> subchan;
            channel12[i] |= (val << (7-j));
        }
    }
}


/**********************************************************************\
 *                     EDC/ECC utility functions                      *
\**********************************************************************/
/* The following code is based on Neill Corlett's ECM code */
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

/**
 * mirage_helper_sector_edc_ecc_compute_edc_block:
 * @src: (in) (array length=size): data to calculate EDC data for
 * @size: (in): size of data in @src
 * @dest: (out caller-allocates) (array fixed-size=4): buffer to write calculated EDC data into (4 bytes)
 *
 * Calculates EDC (error detection code) for data in @src of length @size and
 * writes the result into @dest.
 *
 * To calculate EDC for different types of sectors and store it into sector data, use:
 * <itemizedlist>
 * <listitem>
 * Mode 1 sector:
 * <programlisting>
 * mirage_helper_sector_edc_ecc_compute_edc_block(sector_buffer+0x00, 0x810, sector_buffer+0x810);
 * </programlisting>
 * </listitem>
 * <listitem>
 * Mode 2 Form 1 sector:
 * <programlisting>
 * mirage_helper_sector_edc_ecc_compute_edc_block(sector_buffer+0x10, 0x808, sector_buffer+0x818);
 * </programlisting>
 * </listitem>
 * <listitem>
 * Mode 2 Form 2 sector:
 * <programlisting>
 * mirage_helper_sector_edc_ecc_compute_edc_block(sector_buffer+0x10, 0x91C, sector_buffer+0x92C);
 * </programlisting>
 * </listitem>
 * </itemizedlist>
 * (This is assuming all other sector data is already stored in sector_buffer and that sector_buffer is 2532 bytes long)
 */
void mirage_helper_sector_edc_ecc_compute_edc_block (const guint8 *src, guint16 size, guint8 *dest)
{
    guint32 edc;
    guint32 *dest2 = (guint32 *) dest;

    edc = mirage_helper_calculate_crc32(src, size, crc32_d8018001_lut, TRUE, FALSE);
    *dest2 = GUINT32_TO_LE(edc);
}

/**
 * mirage_helper_sector_edc_ecc_compute_ecc_block:
 * @src: (in): data to calculate ECC data for
 * @major_count: (in): major count
 * @minor_count: (in): minor count
 * @major_mult: (in): major multiplicator
 * @minor_inc: (in): minor increment
 * @dest: (out caller-allocates): buffer to write calculated ECC data into
 *
 * Calculates ECC (error correction code) for data in @src and writes the result
 * into @dest. The code assumes 2352 byte sectors. It can calculate both P and Q
 * layer of ECC data, depending on @major_count, @minor_count, @major_mult and
 * minor_inc.
 *
 * To calculate ECC (first P, then Q layer) for different types of sectors and store it into sector data, use:
 * <itemizedlist>
 * <listitem>
 * Mode 1 sector:
 * <programlisting>
 * mirage_helper_sector_edc_ecc_compute_ecc_block(sector_buffer+0xC, 86, 24, 2, 86, sector_buffer+0x81C);
 * mirage_helper_sector_edc_ecc_compute_ecc_block(sector_buffer+0xC, 52, 43, 86, 88, sector_buffer+0x8C8);
 * </programlisting>
 * </listitem>
 * <listitem>
 * Mode 2 Form 1 sector:
 * <programlisting>
 * mirage_helper_sector_edc_ecc_compute_ecc_block(sector_buffer+0xC, 86, 24, 2, 86, sector_buffer+0x81C); \n
 * mirage_helper_sector_edc_ecc_compute_ecc_block(sector_buffer+0xC, 52, 43, 86, 88, sector_buffer+0x8C8);
 * </programlisting>
 * </listitem>
 * </itemizedlist>
 * (This is assuming all other sector data, including EDC, is already stored in sector_buffer and that sector_buffer is 2532 bytes long)
 */
void mirage_helper_sector_edc_ecc_compute_ecc_block (const guint8 *src, guint32 major_count, guint32 minor_count, guint32 major_mult, guint32 minor_inc, guint8 *dest)
{
    guint32 size = major_count * minor_count;
    guint32 index;
    guint8 ecc_a, ecc_b, temp;

    for (guint32 major = 0; major < major_count; major++) {
        index = (major >> 1) * major_mult + (major & 1);
        ecc_a = 0;
        ecc_b = 0;
        for (guint32 minor = 0; minor < minor_count; minor++) {
            temp = src[index];
            index += minor_inc;
            if (index >= size) {
                index -= size;
            }
            ecc_a ^= temp;
            ecc_b ^= temp;
            ecc_a = ecc_f_lut[ecc_a];
        }
        ecc_a = ecc_b_lut[ecc_f_lut[ecc_a] ^ ecc_b];
        dest[major              ] = ecc_a;
        dest[major + major_count] = ecc_a ^ ecc_b;
    }
}


/**
 * mirage_helper_determine_sector_type:
 * @buf: (in): buffer containing at least first 16 bytes of sector's data
 *
 * Determines sector type from its data, based on first 16 bytes, which
 * correspond to sync pattern and header.
 *
 * This function is intened to be used in image parsers, for determining
 * track mode in cases when full (2352-byte) sector data is available.
 *
 * Returns: sector type (one of %MirageTrackModes)
 */
MirageTrackModes mirage_helper_determine_sector_type (const guint8 *buf)
{
    if (!memcmp(buf, mirage_pattern_sync, sizeof(mirage_pattern_sync))) {
        switch (buf[15]) {
            case 0: return MIRAGE_MODE_MODE0;
            case 1: return MIRAGE_MODE_MODE1;
            case 2: return MIRAGE_MODE_MODE2_MIXED;
        }
    }

    /* No sync pattern; assume audio sector */
    return MIRAGE_MODE_AUDIO;
}


/**********************************************************************\
 *                         Text data encoding                         *
\**********************************************************************/
static const guint8 bom_utf32be[] = { 0x00, 0x00, 0xFE, 0xFF };
static const gchar utf32be[] = "utf-32be";

static const guint8 bom_utf32le[] = { 0xFF, 0xFE, 0x00, 0x00 };
static const gchar utf32le[] = "utf-32le";

static const guint8 bom_utf16be[] = { 0xFE, 0xFF };
static const gchar utf16be[] = "utf-16be";

static const guint8 bom_utf16le[] = { 0xFF, 0xFE };
static const gchar utf16le[] = "utf-16le";

/**
 * mirage_helper_encoding_from_bom:
 * @buffer: (in) (transfer none) (array fixed-size=4): a 4-byte buffer containing BOM
 *
 * Tries to decode BOM provided in @buffer, and based on the result
 * returns the following encodings: UTF-32BE, UTF32-LE, UTF-16LE, UTF-16BE
 * or UTF-8 (if BOM is not valid).
 *
 * Returns: (transfer none): the name of encoding, or %NULL if UTF-8 is
 * assumed. The string is statically stored and should not be modified.
 */
const gchar *mirage_helper_encoding_from_bom (const guint8 *buffer)
{
    /* Identify the encoding */
    if (!memcmp(buffer, bom_utf32be, sizeof(bom_utf32be))) {
        return utf32be;
    } else if (!memcmp(buffer, bom_utf32le, sizeof(bom_utf32le))) {
        return utf32le;
    } else if (!memcmp(buffer, bom_utf16be, sizeof(bom_utf16be))) {
        return utf16be;
    } else if (!memcmp(buffer, bom_utf16le, sizeof(bom_utf16le))) {
        return utf16le;
    }

    return NULL;
}
