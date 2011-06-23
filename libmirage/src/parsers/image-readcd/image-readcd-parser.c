/*
 *  libMirage: readcd image parser: Parser object
 *  Copyright (C) 2011 Rok Mandeljc
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

#include "image-readcd.h"

#define __debug__ "READCD-Parser"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_READCD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_READCD, MIRAGE_Parser_READCDPrivate))

typedef struct {
    GObject *disc;

    gint cur_lba;
    gint leadout_lba;

    gchar *data_filename;

    GObject *cur_session;
    GObject *cur_track;
} MIRAGE_Parser_READCDPrivate;


/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                     *
\******************************************************************************/
static gboolean __mirage_parser_readcd_is_file_valid (MIRAGE_Parser *self, gchar *filename, GError **error) {
    /*MIRAGE_Parser_READCDPrivate *_priv = MIRAGE_PARSER_READCD_GET_PRIVATE(self);*/
    gboolean succeeded;
    struct stat st;
    FILE *file;

    guint16 toc_len = 0;

    /* File must have .toc suffix */
    if (!mirage_helper_has_suffix(filename, ".toc")) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        succeeded = FALSE;
    }

    if (g_stat(filename, &st) < 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to stat file!\n", __debug__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    file = g_fopen(filename, "r");
    if (!file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file!\n", __debug__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    /* First 4 bytes of TOC are its header; and first 2 bytes of that indicate
       the length */
    if (fread(&toc_len, 2, 1, file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 2-byte TOC length!\n", __debug__);
        mirage_error(MIRAGE_E_READFAILED, error);
        succeeded = FALSE;
        goto end;
    }
    toc_len = GUINT16_FROM_BE(toc_len);

    /* Does TOC length match? (the TOC file actually contains TOC plus two bytes
       that indicate sector types) */
    if (st.st_size - 2 == toc_len + 2) {
        succeeded = TRUE;
    } else {
        /* Nope, can't load the file */
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        succeeded = FALSE;
    }

end:
    fclose(file);
    return succeeded;
}

static gboolean __mirage_parser_readcd_determine_track_mode (MIRAGE_Parser *self, GObject *track, GError **error) {
    GObject *data_fragment = NULL;
    guint64 offset = 0;
    FILE *file = NULL;
    size_t blocks_read;
    guint8 sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    guint8 buf[24] = {0};

    /* Get last fragment */
    if (!mirage_track_get_fragment_by_index(MIRAGE_TRACK(track), -1, &data_fragment, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get fragment\n", __debug__);
        return FALSE;
    }

    /* 2352-byte sectors are assumed */
    mirage_finterface_binary_track_file_get_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), &file, NULL);
    mirage_finterface_binary_track_file_get_position(MIRAGE_FINTERFACE_BINARY(data_fragment), 0, &offset, NULL);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: checking for track type in data file at location 0x%llX\n", __debug__, offset);
    fseeko(file, offset, SEEK_SET);
    blocks_read = fread(buf, 24, 1, file);
    if (blocks_read < 1) return FALSE;
    if (!memcmp(buf, sync, 12)) {
        switch (buf[15]) {
            case 0x01: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 1 track\n", __debug__);
                mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE1, NULL);
                break;
            }
            case 0x02: {
                /* Mode 2; let's say we're Mode 2 Mixed and let the sector
                   code do the rest for us */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 2 track; setting to mixed...\n", __debug__);
                mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE2_MIXED, NULL);
                break;
            }
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Audio track\n", __debug__);
        mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_AUDIO, NULL);
    }

    g_object_unref(data_fragment);

    return TRUE;
}

static gboolean __mirage_parser_readcd_finish_previous_track (MIRAGE_Parser *self, gint next_address) {
    MIRAGE_Parser_READCDPrivate *_priv = MIRAGE_PARSER_READCD_GET_PRIVATE(self);

    if (_priv->cur_lba != -1) {
        gint length = next_address - _priv->cur_lba;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous track's length: %d (0x%X)\n", __debug__, length, length);

        GObject *fragment;

        /* Get last fragment */
        if (!mirage_track_get_fragment_by_index(MIRAGE_TRACK(_priv->cur_track), -1, &fragment, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get fragment\n", __debug__);
            return FALSE;
        }

        /* Set length */
        mirage_fragment_set_length(MIRAGE_FRAGMENT(fragment), length, NULL);

        g_object_unref(fragment);

    }

    return TRUE;
}


static gboolean __mirage_parser_readcd_parse_toc_entry (MIRAGE_Parser *self, guint8 *entry, GError **error)
{
    MIRAGE_Parser_READCDPrivate *_priv = MIRAGE_PARSER_READCD_GET_PRIVATE(self);
    gboolean succeeded = TRUE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", __debug__, entry[0], entry[1], entry[2], entry[3], entry[4], entry[5], entry[6], entry[7], entry[8], entry[9], entry[10]);

    if (entry[3] == 0xA0) {
        /* First track number */
        guint8 session = entry[0];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating session #%d\n", __debug__, session);

        _priv->cur_session = NULL;
        succeeded = mirage_disc_add_session_by_number(MIRAGE_DISC(_priv->disc), session, &_priv->cur_session, error);
        if (!succeeded) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
            goto end;
        }

        g_object_unref(_priv->cur_session); /* Keep only pointer, without reference */

        /* Finish previous track (i.e. last track in previous session) */
        if (_priv->cur_track && _priv->leadout_lba != -1) {
            __mirage_parser_readcd_finish_previous_track(self, _priv->leadout_lba);
        }

        /* Reset */
        _priv->cur_lba = -1;
        _priv->cur_track = NULL;
    } else if (entry[3] == 0xA1) {
        /* Last track number */
    } else if (entry[3] == 0xA2) {
        /* Lead-put position */
        gint leadout_lba = mirage_helper_msf2lba(entry[8], entry[9], entry[10], TRUE);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: leadout LBA: %d (0x%X)\n", __debug__, leadout_lba, leadout_lba);

        _priv->leadout_lba = leadout_lba;
    } else if (entry[3] > 0 && entry[3] < 99) {
        /* Track */
        guint8 track = entry[3];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating track #%d\n", __debug__, track);

        gint track_lba = mirage_helper_msf2lba(entry[8], entry[9], entry[10], TRUE);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track LBA: %d (0x%X)\n", __debug__, track_lba, track_lba);

        /* Calculate previous track's length */
        __mirage_parser_readcd_finish_previous_track(self, track_lba);
        _priv->cur_lba = track_lba;

        /* Add track */
        _priv->cur_track = NULL;
        succeeded = mirage_session_add_track_by_number(MIRAGE_SESSION(_priv->cur_session), track, &_priv->cur_track, error);
        if (!succeeded) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
            goto end;
        }

        mirage_track_set_ctl(MIRAGE_TRACK(_priv->cur_track), entry[1], NULL);

        g_object_unref(_priv->cur_track); /* Keep only pointer, without reference */

        /* Data fragment */
        GObject *data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, _priv->data_filename, error);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data fragment!\n", __debug__);
            succeeded = FALSE;
            goto end;
        }

        FILE *tfile_file = g_fopen(_priv->data_filename, "r");
        gint tfile_sectsize = 2352; /* Always */
        guint64 tfile_offset = track_lba * 2448; /* Guess this one's always true, too */
        gint tfile_format = FR_BIN_TFILE_DATA; /* Assume data, but change later if it's audio */

        gint sfile_sectsize = 96; /* Always */
        gint sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT;

        mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_file, NULL);
        mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
        mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
        mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);

        mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
        mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_format, NULL);

        mirage_track_add_fragment(MIRAGE_TRACK(_priv->cur_track), -1, &data_fragment, NULL);
        g_object_unref(data_fragment);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: determining track mode\n", __debug__);
        __mirage_parser_readcd_determine_track_mode(self, _priv->cur_track, NULL);
    }

end:
    return succeeded;
}

static gboolean __mirage_parser_readcd_parse_toc (MIRAGE_Parser *self, gchar *filename, GError **error) {
    MIRAGE_Parser_READCDPrivate *_priv = MIRAGE_PARSER_READCD_GET_PRIVATE(self);
    GError *local_error = NULL;
    gboolean succeeded = TRUE;

    /* NOTE: the __mirage_parser_readcd_is_file_valid() check guarantees that the
       image filename has a valid suffix... */
    _priv->data_filename = g_strdup(filename);
    *mirage_helper_get_suffix(_priv->data_filename) = 0; /* Skip the suffix */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TOC filename: %s\n", __debug__, filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data filename: %s\n", __debug__, _priv->data_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:\n", __debug__);

    /* Make sure data file exists */
     if (!g_file_test(_priv->data_filename, G_FILE_TEST_IS_REGULAR)) {
         mirage_error(MIRAGE_E_DATAFILE, error);
         return FALSE;
     }

    /* Map the TOC file */
    GMappedFile *toc_mapped = g_mapped_file_new(filename, FALSE, &local_error);;
    if (!toc_mapped) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to map file '%s': %s!\n", __debug__, filename, local_error->message);
        g_error_free(local_error);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        succeeded = FALSE;
        goto end;
    }

    guint8 *data = (guint8 *)g_mapped_file_get_contents(toc_mapped);
    guint8 cur_off = 0;

    guint16 toc_len;
    guint8 first_session, last_session;

    toc_len = MIRAGE_CAST_DATA(data, cur_off, guint16);
    cur_off += sizeof(guint16);
    toc_len = GUINT16_FROM_BE(toc_len);

    first_session = MIRAGE_CAST_DATA(data, cur_off, guint8);
    cur_off += sizeof(guint8);

    last_session = MIRAGE_CAST_DATA(data, cur_off, guint8);
    cur_off += sizeof(guint8);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TOC:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  TOC length: %d (0x%X)\n", __debug__, toc_len, toc_len);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  first session: %d\n", __debug__, first_session);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  last session: %d\n", __debug__, last_session);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:\n", __debug__);

    toc_len -= 2; /* Session numbers */

    /* Reset */
    _priv->leadout_lba = -1;
    _priv->cur_lba = -1;
    _priv->cur_session = NULL;
    _priv->cur_track = NULL;

    /* Go over all TOC entries */
    for (int i = 0; i < toc_len/11; i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TOC entry #%d\n", __debug__, i);

        /* Parse TOC entry */
        succeeded = __mirage_parser_readcd_parse_toc_entry(self, data+cur_off, error);
        if (!succeeded) {
            goto end;
        }

        cur_off += 11;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:\n", __debug__);
    }

    /* Finish previous track (i.e. last track in previous session) */
    if (_priv->cur_track && _priv->leadout_lba != -1) {
        __mirage_parser_readcd_finish_previous_track(self, _priv->leadout_lba);
    }

    guint8 sector0 = MIRAGE_CAST_DATA(data, cur_off, guint8);
    cur_off += sizeof(guint8);

    guint8 sectorE = MIRAGE_CAST_DATA(data, cur_off, guint8);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: start sector type: %Xh\n", __debug__, sector0);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: end sector type: %Xh\n", __debug__, sectorE);

end:
    g_free(_priv->data_filename);

    g_mapped_file_free(toc_mapped);

    return succeeded;
}


static gboolean __mirage_parser_readcd_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_READCDPrivate *_priv = MIRAGE_PARSER_READCD_GET_PRIVATE(self);
    gboolean succeeded = TRUE;

    /* Check if file can be loaded */
    if (!__mirage_parser_readcd_is_file_valid(self, filenames[0], error)) {
        return FALSE;
    }

    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);

    /* Set filenames */
    mirage_disc_set_filename(MIRAGE_DISC(_priv->disc), filenames[0], NULL);

    /* Read and parse TOC */
    if (!__mirage_parser_readcd_parse_toc(self, filenames[0], error)) {
        succeeded = FALSE;
        goto end;
    }

    /* If it's a multisession disc, fix up the lead-in/lead-out lengths
       (NOTE: last session is left out for readibility; but it's irrelevant) */
    gint i, num_sessions;
    mirage_disc_get_number_of_sessions(MIRAGE_DISC(_priv->disc), &num_sessions, NULL);
    for (i = 0; i < num_sessions - 1; i++) {
        GObject *session;
        mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), i, &session, NULL);

        if (i == 0) {
            /* Actually, it should be 6750 previous leadout, 4500 current leadin */
            mirage_session_set_leadout_length(MIRAGE_SESSION(session), 11250, NULL);
        } else {
            /* Actually, it should be 2250 previous leadout, 4500 current leadin */
            mirage_session_set_leadout_length(MIRAGE_SESSION(session), 6750, NULL);
        }

        g_object_unref(session);
    }

    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_parser_guess_medium_type(self, _priv->disc);
    mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), medium_type, NULL);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(self, _priv->disc, NULL);
    }

end:
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

static void __mirage_parser_readcd_instance_init (GTypeInstance *instance, gpointer g_class) {
    /* Create parser info */
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-READCD",
        "READCD Image Parser",
        "READCD images",
        "application/x-cd-image"
    );

    return;
}

static void __mirage_parser_readcd_finalize (GObject *obj) {
    MIRAGE_Parser_READCD *self = MIRAGE_PARSER_READCD(obj);
    /*MIRAGE_Parser_READCDPrivate *_priv = MIRAGE_PARSER_READCD_GET_PRIVATE(self);*/

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);

    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}


static void __mirage_parser_readcd_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_READCDClass *klass = MIRAGE_PARSER_READCD_CLASS(g_class);

    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_READCDPrivate));

    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_readcd_finalize;

    /* Initialize MIRAGE_Parser methods */
    class_parser->load_image = __mirage_parser_readcd_load_image;

    return;
}

GType mirage_parser_readcd_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_READCDClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_readcd_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_READCD),
            0,      /* n_preallocs */
            __mirage_parser_readcd_instance_init    /* instance_init */
        };

        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_READCD", &info, 0);
    }

    return type;
}
