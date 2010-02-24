/*
 *  libMirage: CDI image parser: Parser object
 *  Copyright (C) 2007-2009 Rok Mandeljc
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

#include "image-cdi.h"

#define __debug__ "CDI-Parser"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_CDI_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_CDI, MIRAGE_Parser_CDIPrivate))

typedef struct {
    GObject *disc;
    
    gchar *cdi_filename;
    
    gboolean medium_type_set;
    
    guint64 cur_offset; /* Current offset within track file */
    
    guint8 *cdi_data;
    guint8 *cur_ptr;
} MIRAGE_Parser_CDIPrivate;


/* NOTE: as far as my experiments show, descriptor has the following structure:
    - number of sessions: first byte
    - session descriptor: 15 bytes; one for every session, followed by track 
        descriptors for tracks in that session
    - track descriptor: 228 bytes; this is "bare" track descriptor length, 
        meaning it doesn't account for filename length, nor for index descriptors,
        nor CD-Text block... even though, it should be noted that seemingly there
        are always at least two index entries present
    - at the end of session/track descriptors, there seems to be another session
      descriptor, with 0 tracks
    - disc descriptor: 114 bytes; located at the end; this is also "bare" length,
        and it does include the descriptor lenght field at the end of file
        
    -- Rok
*/

/* Self-explanatory */
#define WHINE_ON_UNEXPECTED

#ifdef WHINE_ON_UNEXPECTED
typedef struct {
    gint offset;
    gint expected;
} ExpectedField;

static void __mirage_parser_cdi_whine_on_unexpected (MIRAGE_Parser *self, guint8 *data, ExpectedField *fields, gint fields_len, gchar *func_name, gchar *extra_comment) {
    gint z;
    
    for (z = 0; z < fields_len; z++) {
        if (data[fields[z].offset] != fields[z].expected) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: data[%i] = 0x%X (expected 0x%X); extra comment: %s; seems we have a problem there, Dave...\n", func_name, fields[z].offset, data[fields[z].offset], fields[z].expected, extra_comment);
        }
    }
}
#endif


static gboolean __mirage_parser_cdi_decode_medium_type (MIRAGE_Parser *self, gint medium_type, GError **error) {
    MIRAGE_Parser_CDIPrivate *_priv = MIRAGE_PARSER_CDI_GET_PRIVATE(self);
    
    /* Decode and set medium type only if we haven't done it yet */
    if (!_priv->medium_type_set) {        
        switch (medium_type) {
            case 0x98: {
                /* CD-ROM */
                mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), MIRAGE_MEDIUM_CD, NULL);
                break;
            }
            case 0x38: {
                /* DVD-ROM */
                mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), MIRAGE_MEDIUM_DVD, NULL);
                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid medium type: 0x%X!\n", __debug__, medium_type);
            }
        }
        
        _priv->medium_type_set = TRUE;
    }
    
    return TRUE;
}

static gboolean __mirage_parser_cdi_decode_track_mode (MIRAGE_Parser *self, gint raw_mode, gint *decoded_mode, gint *tfile_format, GError **error) {
    /* Simple; raw mode represents track mode. And if it happens to be audio, guess
       what the data format will be? */
    switch (raw_mode) {
        case 0: {
            *decoded_mode = MIRAGE_MODE_AUDIO;
            *tfile_format = FR_BIN_TFILE_AUDIO;
            break;
        }
        case 1: {
            *decoded_mode = MIRAGE_MODE_MODE1;
            *tfile_format = FR_BIN_TFILE_DATA;
            break;
        }
        case 2: {
            *decoded_mode = MIRAGE_MODE_MODE2_MIXED;
            *tfile_format = FR_BIN_TFILE_DATA;
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid track mode: %d!\n", __debug__, raw_mode);
            mirage_error(MIRAGE_E_PARSER, error);
            return FALSE;
        }
    }
    
    return TRUE;
}


static gboolean __mirage_parser_cdi_decode_read_mode (MIRAGE_Parser *self, gint read_mode, gint *tfile_sectsize, gint *sfile_sectsize, gint *sfile_format, GError **error) {
    switch (read_mode) {
        case 0: {
            /* 2048-byte sectors (valid only with Mode 1 tracks) */
            *tfile_sectsize = 2048;
            break;
        }
        case 1: {
            /* 2336-byte sectors (valid only for Mode 2 tracks) */
            *tfile_sectsize = 2336;
            break;
        }
        case 2: {
            /* 2352-byte sectors (Audio tracks or any other read in raw mode) */
            *tfile_sectsize = 2352;
            break;
        }
        case 3: {
            /* 2352+16-byte sectors (any track read raw + PQ subchannel) */
            *tfile_sectsize = 2352;
            *sfile_sectsize = 16;
            *sfile_format = FR_BIN_SFILE_PQ16 | FR_BIN_SFILE_INT; /* PQ, internal */
            break;
        }
        case 4: {
            /* 2352+96-byte sectors (any track read raw + PW subchannel) */
            *tfile_sectsize = 2352;
            *sfile_sectsize = 96;
            *sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT; /* PW96 interleaved, internal */
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid read mode: %d!\n", __debug__, read_mode);
            mirage_error(MIRAGE_E_PARSER, error);
            return FALSE;
        }
    }
    
    return TRUE;
}

static gboolean __mirage_parser_cdi_decode_session_type (MIRAGE_Parser *self, gint raw_session_type, gint *session_type, GError **error) {
    switch (raw_session_type) {
        case 0: {
            /* CD-DA */
            *session_type = MIRAGE_SESSION_CD_DA;
            break;
        }
        case 1: {
            /* CD-ROM */
            *session_type = MIRAGE_SESSION_CD_ROM;
            break;
        }
        case 2: {
            /* CD-ROM XA */
            *session_type = MIRAGE_SESSION_CD_ROM_XA;
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid session type: %d!\n", __debug__, raw_session_type);
            mirage_error(MIRAGE_E_PARSER, error);
            return FALSE;
        }
    }
    
    return TRUE;
}

/* Function for parsing header that appears at the beginning of every track block
   and at the beginning of the disc block */
static gboolean __mirage_parser_cdi_parse_header (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_CDIPrivate *_priv = MIRAGE_PARSER_CDI_GET_PRIVATE(self);
    /* Recongised fields */
    gint num_all_tracks = 0;
    gint filename_length = 0;
    gchar *filename = NULL;
    gint disc_capacity = 0;
    gint medium_type = 0;
    
    /* The header seems to consist of following:
        - 16 bytes forming what seems a fixed pattern
        - 1 byte representing filename length
        - filename of variable length
        - 29 bytes with fixed values 
        - 2 bytes that form up medium type */
#ifdef WHINE_ON_UNEXPECTED
    {
    ExpectedField fields[] = {
        {  0, 0xFF },
        {  1, 0xFF },
        {  2, 0x00 },
        {  3, 0x00 },
        {  4, 0x01 },
        {  5, 0x00 },
        {  6, 0x00 },
        {  7, 0x00 },
        {  8, 0xFF },
        {  9, 0xFF },
        { 10, 0xFF },
        { 11, 0xFF },
        /* The following values vary; the last one is positively number of all
           tracks on the disc */
        /*{ 12, 0x64 },
        { 13, 0x05 },
        { 14, 0x2A },
        { 15, 0x06 }*/
    };
    __mirage_parser_cdi_whine_on_unexpected(self, _priv->cur_ptr, fields, G_N_ELEMENTS(fields), (gchar *)__debug__, "Pre-filename fields");
    }
#endif
    num_all_tracks = MIRAGE_CAST_DATA(_priv->cur_ptr, 15, guint8);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of all tracks: %d\n", __debug__, num_all_tracks);
    _priv->cur_ptr += 16;
    
    /* 17th byte is filename length */
    filename_length = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint8);
    _priv->cur_ptr += sizeof(guint8);
    
    /* At 18th byte, filename starts */
    filename = MIRAGE_CAST_PTR(_priv->cur_ptr, 0, gchar *);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: filename length: %d\n", __debug__, filename_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: filename: %.*s\n", __debug__, filename_length, filename);
    _priv->cur_ptr += filename_length;
    
    /* 31 bytes after filename aren't deciphered yet */
#ifdef WHINE_ON_UNEXPECTED
    {
    ExpectedField fields[] = {
        {  0, 0x00 },
        {  1, 0x00 },
        {  2, 0x00 },
        {  3, 0x00 },
        {  4, 0x00 },
        {  5, 0x00 },
        {  6, 0x00 },
        {  7, 0x00 },
        {  8, 0x00 },
        {  9, 0x00 },
        { 10, 0x00 },
        { 11, 0x02 },
        { 12, 0x00 },
        { 13, 0x00 },
        { 14, 0x00 },
        { 15, 0x00 },
        { 16, 0x00 },
        { 17, 0x00 },
        { 18, 0x00 },
        { 19, 0x00 },
        { 20, 0x00 },
        { 21, 0x00 },
        { 22, 0x80 },
        /* Following fields almost positively represent disc capacity */
        /*{ 23, 0x40 },
        { 24, 0x7E },
        { 25, 0x05 },
        { 26, 0x00 },*/
        { 27, 0x00 },
        { 28, 0x00 },
    };
    __mirage_parser_cdi_whine_on_unexpected(self, _priv->cur_ptr, fields, G_N_ELEMENTS(fields), (gchar *)__debug__, "Post-filename fields");
    }
#endif
    disc_capacity = MIRAGE_CAST_DATA(_priv->cur_ptr, 23, guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc capacity: 0x%X\n", __debug__, disc_capacity);
    _priv->cur_ptr += 29;
    
    /* Medium type */
    medium_type = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint16);
    _priv->cur_ptr += sizeof(guint16);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: medium type: 0x%X\n", __debug__, medium_type);
    
    __mirage_parser_cdi_decode_medium_type(self, medium_type, NULL);
        
    return TRUE;
}

static gboolean __mirage_parser_cdi_parse_cdtext (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_CDIPrivate *_priv = MIRAGE_PARSER_CDI_GET_PRIVATE(self);

    gint i;
    
    /* It would seem that each CD-TEXT block for track consists of 18 bytes, each (?)
       denoting length of field it represents; if it's non-zero, it's followed by declared
       size of bytes... */
    for (i = 0; i < 18; i++) {
        gint length = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint8);
        _priv->cur_ptr += sizeof(guint8);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: field [%i] length: %i\n", __debug__, i, length);
        if (length) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: field [%i] data: %.*s\n", __debug__, i, length, _priv->cur_ptr);
            _priv->cur_ptr += length;
        }
            
    }
    
    return TRUE;
}

static gboolean __mirage_parser_cdi_load_track (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_CDIPrivate *_priv = MIRAGE_PARSER_CDI_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    gint i;
    
    /* Recongised fields */
    gint num_indices = 0;
    gint *indices = NULL;
    gint track_mode = 0;
    gint track_idx = 0;
    gint session_idx = 0;
    gint num_cdtext_blocks = 0;
    gint start_address = 0;
    gint track_length = 0;
    gint read_mode = 0;
    gint track_ctl = 0;
    gchar *isrc = NULL;
    gint isrc_valid = 0;
    /* These are the ones I'm not quite certain of */
    gint session_type = 0;
    gint not_last_track = 0;
    gint address_at_the_end = 0;
    
    /**********************************************************************\
     *                         Track data parsing                         *
    \**********************************************************************/
    
    /* Header */
    if (!__mirage_parser_cdi_parse_header(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse header!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Index fields follow */
    num_indices = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint16);
    _priv->cur_ptr += sizeof(guint16);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of indices: %d\n", __debug__, num_indices);
    
    indices = g_new0(gint, num_indices);
    for (i = 0; i < num_indices; i++) {
        indices[i] = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
        _priv->cur_ptr += sizeof(guint32);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: index %i: 0x%X\n", __debug__, i, indices[i]);
    }
    
    /* Next is (4-byte?) field that, if set to 1, indicates presence of CD-Text data */
    num_cdtext_blocks = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of CD-TEXT blocks: %i\n", __debug__, num_cdtext_blocks);
    
    for (i = 0; i < num_cdtext_blocks; i++) {
        /* Parse CD-Text */
        __mirage_parser_cdi_parse_cdtext(self, error);
    }
    
    /* 2 bytes after indices are undeciphered yet */
#ifdef WHINE_ON_UNEXPECTED
    {
    ExpectedField fields[] = {
        { 0, 0x00 },
        { 1, 0x00 },
    };
    __mirage_parser_cdi_whine_on_unexpected(self, _priv->cur_ptr, fields, G_N_ELEMENTS(fields), (gchar *)__debug__, "2 bytes after CD-TEXT");
    }
#endif
    _priv->cur_ptr += 2;
    
    
    /* Track mode follows (FIXME: is it really 4-byte?) */
    track_mode = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: %i\n", __debug__, track_mode);

    /* 4 bytes follow that have not been deciphered yet */
#ifdef WHINE_ON_UNEXPECTED
    {
    ExpectedField fields[] = {
        {  0, 0x00 },
        {  1, 0x00 },
        {  2, 0x00 },
        {  3, 0x00 },
    };
    __mirage_parser_cdi_whine_on_unexpected(self, _priv->cur_ptr, fields, G_N_ELEMENTS(fields), (gchar *)__debug__, "4 bytes after track mode");
    }
#endif
    _priv->cur_ptr += 4;
    
    
    /* Session index (i.e. which session block this track block belongs to)... */
    session_idx = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: session index: %i\n", __debug__, session_idx);
    
    /* Track index (i.e. which track block is this)... */
    track_idx = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track index: %i\n", __debug__, track_idx);
    
    /* Next is track start address... */
    start_address = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track start: 0x%X\n", __debug__, start_address);
    
    /* ... followed by track length */
    track_length = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track length: 0x%X\n", __debug__, track_length);
    
    
    /* 16 undeciphered bytes */
#ifdef WHINE_ON_UNEXPECTED
    {
    ExpectedField fields[] = {
        {  0, 0x00 },
        {  1, 0x00 },
        {  2, 0x00 },
        {  3, 0x00 },
        {  4, 0x00 },
        {  5, 0x00 },
        {  6, 0x00 },
        {  7, 0x00 },
        {  8, 0x00 },
        {  9, 0x00 },
        { 10, 0x00 },
        { 11, 0x00 },
        { 12, 0x00 },
        { 13, 0x00 },
        { 14, 0x00 },
        { 15, 0x00 },
    };
    __mirage_parser_cdi_whine_on_unexpected(self, _priv->cur_ptr, fields, G_N_ELEMENTS(fields), (gchar *)__debug__, "16 bytes after track length");
    }
#endif
    _priv->cur_ptr += 16;
    
    
    /* Field that indicates read mode */
    read_mode = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: read mode: %d\n", __debug__, read_mode);
    
    
    /* Field that has track's CTL stored */
    track_ctl = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track's CTL: %X\n", __debug__, track_ctl);
    
    
    /* 9 undeciphered bytes */
#ifdef WHINE_ON_UNEXPECTED
    {
    ExpectedField fields[] = {
        {  0, 0x00 },
        /* These seem to be a repeated track length */
        {  1, (track_length & 0x000000FF) >>  0 },
        {  2, (track_length & 0x0000FF00) >>  8 },
        {  3, (track_length & 0x00FF0000) >> 16 },
        {  4, (track_length & 0xFF000000) >> 24 },
        
        {  5, 0x00 },
        {  6, 0x00 },
        {  7, 0x00 },
        {  8, 0x00 },
    };
    __mirage_parser_cdi_whine_on_unexpected(self, _priv->cur_ptr, fields, G_N_ELEMENTS(fields), (gchar *)__debug__, "9 bytes after track CTL");
    }
#endif
    _priv->cur_ptr += 9;
    
    
    /* ISRC and ISRC valid */
    isrc = MIRAGE_CAST_PTR(_priv->cur_ptr, 0, gchar *);
    _priv->cur_ptr += 12;
    isrc_valid = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);    
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: ISRC valid: %i\n", __debug__, isrc_valid);
    if (isrc_valid) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: ISRC: %.12s\n", __debug__, isrc);
    }
    
    
    /* Remaining 99 undeciphered bytes */
#ifdef WHINE_ON_UNEXPECTED
    {
    ExpectedField fields[] = {
        {  0, 0x00 },
        {  1, 0xFF },
        {  2, 0xFF },
        {  3, 0xFF },
        {  4, 0xFF },
        {  5, 0xFF },
        {  6, 0xFF },
        {  7, 0xFF },
        {  8, 0xFF },
        {  9, 0x01 },
        { 10, 0x00 },
        { 11, 0x00 },
        { 12, 0x00 },
        { 13, 0x80 },
        { 14, 0x00 },
        { 15, 0x00 },
        { 16, 0x00 },
        { 17, 0x02 },
        { 18, 0x00 },
        { 19, 0x00 },
        { 20, 0x00 },
        { 21, 0x10 },
        { 22, 0x00 },
        { 23, 0x00 },
        { 24, 0x00 },
        { 25, 0x44 },
        { 26, 0xAC },
        { 27, 0x00 },
        { 28, 0x00 },
        { 29, 0x00 },
        { 30, 0x00 },
        { 31, 0x00 },
        { 32, 0x00 },
        { 33, 0x00 },
        { 34, 0x00 },
        { 35, 0x00 },
        { 36, 0x00 },
        { 37, 0x00 },
        { 38, 0x00 },
        { 39, 0x00 },
        { 40, 0x00 },
        { 41, 0x00 },
        { 42, 0x00 },
        { 43, 0x00 },
        { 44, 0x00 },
        { 45, 0x00 },
        { 46, 0x00 },
        { 47, 0x00 },
        { 48, 0x00 },
        { 49, 0x00 },
        { 50, 0x00 },
        { 51, 0x00 },
        { 52, 0x00 },
        { 53, 0x00 },
        { 54, 0x00 },
        { 55, 0x00 },
        { 56, 0x00 },
        { 57, 0x00 },
        { 58, 0x00 },
        { 59, 0x00 },
        { 60, 0x00 },
        { 61, 0x00 },
        { 62, 0x00 },
        { 63, 0x00 },
        { 64, 0x00 },
        { 65, 0x00 },
        { 66, 0x00 },
        { 67, 0x00 },
        { 68, 0x00 },
        { 69, 0x00 },
        { 70, 0x00 },
        { 71, 0xFF },
        { 72, 0xFF },
        { 73, 0xFF },
        { 74, 0xFF },
        { 75, 0x00 },
        { 76, 0x00 },
        { 77, 0x00 },
        { 78, 0x00 },
        { 79, 0x00 },
        { 80, 0x00 },
        { 81, 0x00 },
        { 82, 0x00 },
        { 83, 0x00 },
        { 84, 0x00 },
        { 85, 0x00 },
        { 86, 0x00 },
        /* Session type, in case it's last track of a session */
        /*{ 87, 0x00 },*/
        { 88, 0x00 },
        { 89, 0x00 },
        { 90, 0x00 },
        { 91, 0x00 },
        { 92, 0x00 },
        /* This one's set to 0 in last track for any session; otherwise it's 1 */
        /*{ 93, 0x01 },*/
        { 94, 0x00 },
        /* These seem to be some sort of an address for last track of a session...
           otherwise, they're set to 00 00 FF FF */
        /*{ 95, 0x00 },
        { 96, 0x00 },
        { 97, 0xFF },
        { 98, 0xFF },*/
    };
    __mirage_parser_cdi_whine_on_unexpected(self, _priv->cur_ptr, fields, G_N_ELEMENTS(fields), (gchar *)__debug__, "99 bytes at the end");
    }
#endif
    session_type = MIRAGE_CAST_DATA(_priv->cur_ptr, 87, guint8);
    not_last_track = MIRAGE_CAST_DATA(_priv->cur_ptr, 93, guint8);
    address_at_the_end = MIRAGE_CAST_DATA(_priv->cur_ptr, 95, guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: session type: %i\n", __debug__, session_type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: not the last track: %i\n", __debug__, not_last_track);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: address at the end: 0x%X\n", __debug__, address_at_the_end);
    _priv->cur_ptr += 99;    
    
    /**********************************************************************\
     *                         Track data setting                         *
    \**********************************************************************/
    FILE *tfile_handle = NULL;
    gint tfile_sectsize = 0;
    guint64 tfile_offset = _priv->cur_offset;
    gint tfile_format = 0;
    
    gint sfile_sectsize = 0;
    gint sfile_format = 0;
    
    gint fragment_len = track_length;
    
    GObject *data_fragment = NULL;
        
    GObject *cur_session = NULL;
    GObject *cur_track = NULL;
    
    
    /* Prepare stuff for BINARY fragment */
    tfile_handle = g_fopen(_priv->cdi_filename, "r");    
    
    /* Track mode; also determines BINARY format */
    gint decoded_mode = 0;
    if (!__mirage_parser_cdi_decode_track_mode(self, track_mode, &decoded_mode, &tfile_format, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to decode track mode!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Read mode; determines sector size for both main channel and subchannel */
    if (!__mirage_parser_cdi_decode_read_mode(self, read_mode, &tfile_sectsize, &sfile_sectsize, &sfile_format, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to decode read mode!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Fetch current session */
    if (!mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), -1, &cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get last session!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Add track */
    if (!mirage_session_add_track_by_index(MIRAGE_SESSION(cur_session), -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to add track!\n", __debug__);
        succeeded = FALSE;
        goto free_session;
    }
    
    /* Set track mode */
    mirage_track_set_mode(MIRAGE_TRACK(cur_track), decoded_mode, NULL);
    
    /* Create BINARY fragment */    
    data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, _priv->cdi_filename, error);
    if (!data_fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create BINARY fragment!\n", __debug__);
        goto free_track;
    }
    
    mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len, NULL);
    
    mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_handle, NULL);
    mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
    mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
    mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);
            
    mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
    mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_format, NULL);
            
    mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, NULL);
            
    g_object_unref(data_fragment);
    
    
    /* Set track flags, based on CTL */
    mirage_track_set_ctl(MIRAGE_TRACK(cur_track), track_ctl, NULL);
    
    /* Set ISRC */
    if (isrc_valid) {
        /* Don't check for error here; if fragment was created with subchannel
           data, then this call will fail, but it doesn't matter anyway... */
        mirage_track_set_isrc(MIRAGE_TRACK(cur_track), isrc, NULL);
    }
    
    /* Indices; each entry represents length of corresponding index, whereas
       libMirage uses index starting points to denote indices. In CDI, there
       always seem to be at least two entries present; first one is for track
       start (or rather, length of start pregap), and the second is for index 1.
       So here we loop over everything in between first and last entry; first
       entry is used to set track start outside the loop, whereas the last entry
       isn't needed, because it spans to the end of the track, anyway */
    gint index_address = indices[0];
    mirage_track_set_track_start(MIRAGE_TRACK(cur_track), indices[0], NULL);
    for (i = 1; i < num_indices - 1; i++) {
        index_address += indices[i];
        mirage_track_add_index(MIRAGE_TRACK(cur_track), index_address, NULL, NULL);
    }
    
    
    /* Set session type, if this is the last track in session */
    if (!not_last_track) {
        __mirage_parser_cdi_decode_session_type(self, session_type, &session_type, NULL);
        mirage_session_set_session_type(MIRAGE_SESSION(cur_session), session_type, NULL);
    }
    
    
    /* Update current offset within image */
    _priv->cur_offset += (tfile_sectsize + sfile_sectsize) * fragment_len;
    
free_track:
    g_object_unref(cur_track);
free_session:
    g_object_unref(cur_session);
    
end:
    
    return succeeded;
}

static gboolean __mirage_parser_cdi_load_session (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_CDIPrivate *_priv = MIRAGE_PARSER_CDI_GET_PRIVATE(self);    
    gint num_tracks = 0;
    gint i;
    
    /* As far as session descriptor goes, second byte is number of tracks... */
    num_tracks = MIRAGE_CAST_DATA(_priv->cur_ptr, 1, guint8);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of tracks: %d\n", __debug__, num_tracks);

#ifdef WHINE_ON_UNEXPECTED
    {
    ExpectedField fields[] = {
        {  0, 0x00 },
        /* 1: Known */
        {  2, 0x00 },
        {  3, 0x00 },
        {  4, 0x00 },
        {  5, 0x00 },
        {  6, 0x00 },
        {  7, 0x00 },
        {  8, 0x00 },
        {  9, 0x01 },
        { 10, 0x00 },
        { 11, 0x00 },
        { 12, 0x00 },
        { 13, 0xFF },
        { 14, 0xFF },
    };
    __mirage_parser_cdi_whine_on_unexpected(self, _priv->cur_ptr, fields, G_N_ELEMENTS(fields), (gchar *)__debug__, "Session fields");
    }
#endif
    
    _priv->cur_ptr += 15;
    
    if (num_tracks) {
        /* Add session */
        if (!mirage_disc_add_session_by_index(MIRAGE_DISC(_priv->disc), -1, NULL, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to add session to parser!\n", __debug__);
            return FALSE;
        }
        
        /* Load tracks */
        for (i = 0; i < num_tracks; i++) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: *** Loading track %i ***\n", __debug__, i);
            if (!__mirage_parser_cdi_load_track(self, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to load track!\n", __debug__);
                return FALSE;
            }
        }
    } else {
        /* This is expected; it would seem that the session block that follows
           last track in last session has 0 tracks... so we do nothing here */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of tracks in session is 0... this is alright if this is the descriptor that follows last track entry in last session... otherwise we might have a problem, Dave...\n", __debug__);
    }
    
    return TRUE;
}

static gboolean __mirage_parser_cdi_load_disc (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_CDIPrivate *_priv = MIRAGE_PARSER_CDI_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    gint num_sessions = 0;
    gint i;
    
    /* First byte seems to be number of sessions */
    num_sessions = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint8);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of sessions: %d\n", __debug__, num_sessions);
    
    /* Load sessions (note that the equal sign in for loop is there to account
       for the last, empty session) */
    _priv->cur_ptr += 1; /* Set pointer at start of first session descriptor */
    for (i = 0; i <= num_sessions; i++) {
        /* Load session */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: *** Loading session %i ***\n", __debug__, i);
        if (!__mirage_parser_cdi_load_session(self, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to load session!\n", __debug__);
            succeeded = FALSE;
            goto end;
        }
    }
    
    /* Disc descriptor */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing disc block\n", __debug__);
    
    gint disc_length = 0;
    gint volume_id_length = 0;
    gchar *volume_id = NULL;
    gint mcn_valid = 0;
    gchar *mcn = NULL;
    gint cdtext_length = 0;
    guint8 *cdtext_data = NULL;
    
    /* Header */
    if (!__mirage_parser_cdi_parse_header(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failes to parse header!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }
    
    /* First 4 bytes seem to be overall size of the disc */
    disc_length = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc length: 0x%X\n", __debug__, disc_length);
    
    /* One byte that follows is length of volume identifier... this is ISO9660
       volume identifier, found on data discs */
    volume_id_length = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint8);
    _priv->cur_ptr += sizeof(guint8);
    
    volume_id = MIRAGE_CAST_PTR(_priv->cur_ptr, 0, gchar *);
    _priv->cur_ptr += volume_id_length;
    
    if (volume_id_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: volume ID: %.*s\n", __debug__, volume_id_length, volume_id);
    }
    
    
    /* 14 undeciphered bytes */
#ifdef WHINE_ON_UNEXPECTED
    {
    ExpectedField fields[] = {
        { 0, 0x00 },
        { 1, 0x01 },
        { 2, 0x00 },
        { 3, 0x00 },
        { 4, 0x00 },
        { 5, 0x01 },
        { 6, 0x00 },
        { 7, 0x00 },
        { 8, 0x00 },
    };
    __mirage_parser_cdi_whine_on_unexpected(self, _priv->cur_ptr, fields, G_N_ELEMENTS(fields), (gchar *)__debug__, "9 bytes after volume ID");
    }
#endif
    _priv->cur_ptr += 9;
    
    /* MCN */
    mcn = MIRAGE_CAST_PTR(_priv->cur_ptr, 0, gchar *);
    _priv->cur_ptr += 13;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MCN: %.13s\n", __debug__, mcn);

    mcn_valid = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MCN valid: %i\n", __debug__, mcn_valid);
    
    /* CD-TEXT */
    cdtext_length = MIRAGE_CAST_DATA(_priv->cur_ptr, 0, guint32);
    _priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: raw CD-TEXT data length: %i\n", __debug__, cdtext_length);
    cdtext_data = MIRAGE_CAST_PTR(_priv->cur_ptr, 0, guint8 *);
    
    if (cdtext_length) {
        /* FIXME: CD-TEXT data is for the first session only, I think... */
        GObject *first_session = NULL;
        mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), 0, &first_session, NULL);
        if (!mirage_session_set_cdtext_data(MIRAGE_SESSION(first_session), cdtext_data, cdtext_length, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load CD-TEXT!\n");
        }
        g_object_unref(first_session);
        
        _priv->cur_ptr += cdtext_length;
    }
    
    
    /* Last 12 bytes are undeciphered as well */
    #ifdef WHINE_ON_UNEXPECTED
    {
    ExpectedField fields[] = {
        {  0, 0x00 },
        {  1, 0x00 },
        {  2, 0x00 },
        {  3, 0x00 },
        {  4, 0x00 },
        {  5, 0x00 },
        {  6, 0x00 },
        {  7, 0x00 },
        {  8, 0x06 },
        {  9, 0x00 },
        { 10, 0x00 },
        { 11, 0x80 },
    };
    __mirage_parser_cdi_whine_on_unexpected(self, _priv->cur_ptr, fields, G_N_ELEMENTS(fields), (gchar *)__debug__, "Last 12 bytes");
    }
#endif
    _priv->cur_ptr += 12;
    
end:    
    return succeeded;    
}

/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                     *
\******************************************************************************/
static gboolean __mirage_parser_cdi_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_CDIPrivate *_priv = MIRAGE_PARSER_CDI_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    FILE *file;
    guint64 offset;
    gint32 descriptor_length;
    
    
    /* Check if we can load the file; we check the suffix */
    if (!mirage_helper_has_suffix(filenames[0], ".cdi")) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }

    /* Open file */
    file = g_fopen(filenames[0], "r");
    if (!file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file '%s'!\n", __debug__, filenames[0]);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);
    
    mirage_disc_set_filename(MIRAGE_DISC(_priv->disc), filenames[0], NULL);
    _priv->cdi_filename = g_strdup(filenames[0]);

    /* The descriptor is stored at the end of CDI image; I'm quite positive that
       last four bytes represent length of descriptor data */
    offset = -(guint64)(sizeof(descriptor_length));
    fseeko(file, offset, SEEK_END);
    if (fread(&descriptor_length, sizeof(descriptor_length), 1, file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read descriptor lenght!\n", __debug__);
        mirage_error(MIRAGE_E_READFAILED, error);
        succeeded = FALSE;
        goto end;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CDI descriptor length: 0x%X\n", __debug__, descriptor_length);
    
    /* Allocate descriptor data and read it */
    _priv->cur_ptr = _priv->cdi_data = g_malloc0(descriptor_length);
    offset = -(guint64)(descriptor_length);
    fseeko(file, offset, SEEK_END);
    if (fread(_priv->cdi_data, descriptor_length, 1, file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read descriptor!\n", __debug__);
        mirage_error(MIRAGE_E_READFAILED, error);
        succeeded = FALSE;
        goto end;
    }
        
    /* Parse the descriptor */
    succeeded = __mirage_parser_cdi_load_disc(self, error);
    
    /* Dirty test: check if size of parsed descriptor equals to declared size
       (minus 4 bytes which make up declared size...) */
    if (_priv->cur_ptr - _priv->cdi_data != descriptor_length - 4) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: size of parsed descriptor mismatch, Dave. Expect trouble... (%d != %d)\n", __debug__, _priv->cur_ptr - _priv->cdi_data, descriptor_length);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parser seems to have been loaded successfully\n", __debug__);
    }
    
    /* Make parser start at -150... it seems both CD and DVD images start at -150
       in CDI (regardless of medium, there's 150 sectors pregap at the beginning) */
    mirage_disc_layout_set_start_sector(MIRAGE_DISC(_priv->disc), -150, NULL);

end:
    /* Free descriptor data */
    g_free(_priv->cdi_data);
        
    /* Close file */
    fclose(file);

    /* Return disc */
    mirage_object_detach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);
    if (succeeded) {
        *disc = _priv->disc;
    } else {
        g_object_unref(_priv->disc);
        *disc = NULL;
    }
        
    return succeeded;
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ParserClass *parent_class = NULL;

static void __mirage_parser_cdi_instance_init (GTypeInstance *instance, gpointer g_class) {
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-CDI",
        "CDI Image Parser",
        "CDI (DiscJuggler) images",
        "application/libmirage-cdi"
    );
    
    return;
}


static void __mirage_parser_cdi_finalize (GObject *obj) {
    MIRAGE_Parser_CDI *self = MIRAGE_PARSER_CDI(obj);
    MIRAGE_Parser_CDIPrivate *_priv = MIRAGE_PARSER_CDI_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);
    
    g_free(_priv->cdi_filename);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_parser_cdi_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_CDIClass *klass = MIRAGE_PARSER_CDI_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
            
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_CDIPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_cdi_finalize;
    
    /* Initialize MIRAGE_Parser methods */
    class_parser->load_image = __mirage_parser_cdi_load_image;
        
    return;
}

GType mirage_parser_cdi_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_CDIClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_cdi_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_CDI),
            0,      /* n_preallocs */
            __mirage_parser_cdi_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_CDI", &info, 0);
    }
    
    return type;
}
