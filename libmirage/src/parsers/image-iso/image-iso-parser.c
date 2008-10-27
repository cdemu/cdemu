/*
 *  libMirage: ISO image parser: Parser object
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

#include "image-iso.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_ISO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_ISO, MIRAGE_Parser_ISOPrivate))

typedef struct {
    gint track_mode;
    gint track_sectsize;
    
    GObject *disc;
} MIRAGE_Parser_ISOPrivate;


static guint8 iso_pattern[] = {0x01, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00};
static guint8 sync_pattern[] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                     *
\******************************************************************************/
static gboolean __mirage_parser_iso_is_file_valid (MIRAGE_Parser *self, gchar *filename, GError **error) {
    MIRAGE_Parser_ISOPrivate *_priv = MIRAGE_PARSER_ISO_GET_PRIVATE(self);
    gboolean succeeded;
    struct stat st;
    FILE *file;
    
    if (g_stat(filename, &st) < 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to stat file!\n", __func__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    file = g_fopen(filename, "r");
    if (!file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file!\n", __func__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    /* 2048-byte standard ISO image check */
    if (st.st_size % 2048 == 0) {
        guint8 buf[8] = {};
        
        fseeko(file, 16*2048, SEEK_SET);
        
        if (fread(buf, 8, 1, file) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read ISO pattern!\n", __func__);
            mirage_error(MIRAGE_E_READFAILED, error);
            succeeded = FALSE;
            goto end;
        }
        
        if (!memcmp(buf, iso_pattern, sizeof(iso_pattern))) {
            _priv->track_sectsize = 2048;
            _priv->track_mode = MIRAGE_MODE_MODE1;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: standard 2048-byte ISO9660 track, Mode 1 assumed\n", __func__);
            
            succeeded = TRUE;
            goto end;
        }
    }
    
    /* 2352-byte image check */
    if (st.st_size % 2352 == 0) {
        guint8 buf[12] = {};
        
        fseeko(file, 16*2352, SEEK_SET);
        
        if (fread(buf, 12, 1, file) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read sync pattern!\n", __func__);
            mirage_error(MIRAGE_E_READFAILED, error);
            succeeded = FALSE;
            goto end;
        }
        
        if (!memcmp(buf, sync_pattern, sizeof(sync_pattern))) {
            guint8 mode_byte = 0;
            
            
            /* Read mode byte from header */
            fseeko(file, 3, SEEK_CUR); /* We're at the end of sync, we just need to skip MSF */
            
            if (fread(&mode_byte, 1, 1, file) < 1) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read mode byte!\n", __func__);
                mirage_error(MIRAGE_E_READFAILED, error);
                succeeded = FALSE;
                goto end;
            }
            
            switch (mode_byte) {
                case 0: {
                    _priv->track_sectsize = 2352;
                    _priv->track_mode = MIRAGE_MODE_MODE0;
                    
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte track, Mode 0\n", __func__);
                    
                    succeeded = TRUE;
                    goto end;
                }
                case 1: {
                    _priv->track_sectsize = 2352;
                    _priv->track_mode = MIRAGE_MODE_MODE1;
                    
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte track, Mode 1\n", __func__);
                    
                    succeeded = TRUE;
                    goto end;
                }
                case 2: {
                    _priv->track_sectsize = 2352;
                    _priv->track_mode = MIRAGE_MODE_MODE2_MIXED;
                    
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte track, Mode 2 Mixed\n", __func__);
                    
                    succeeded = TRUE;
                    goto end;
                }
            }
        } else {
            _priv->track_sectsize = 2352;
            _priv->track_mode = MIRAGE_MODE_AUDIO;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte track w/o sync pattern, Audio assumed\n", __func__);
            
            succeeded = TRUE;
            goto end;
        }
    }
    
    /* 2332/2336-byte image check */
    if (st.st_size % 2332 == 0) {
        _priv->track_sectsize = 2332;
        _priv->track_mode = MIRAGE_MODE_MODE2_MIXED;
            
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2332-byte track, Mode 2 Mixed assumed (unreliable!)\n", __func__);
            
        succeeded = TRUE;
        goto end;
    }
    if (st.st_size % 2336 == 0) {
        _priv->track_sectsize = 2336;
        _priv->track_mode = MIRAGE_MODE_MODE2_MIXED;
            
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2336-byte track, Mode 2 Mixed assumed (unreliable!)\n", __func__);
            
        succeeded = TRUE;
        goto end;
    }
    
    /* Nope, can't load the file */
    mirage_error(MIRAGE_E_CANTHANDLE, error);
    succeeded = FALSE;
    
end:
    fclose(file);
    return succeeded;
}

static gboolean __mirage_parser_iso_load_track (MIRAGE_Parser *self, gchar *filename, GError **error) {
    MIRAGE_Parser_ISOPrivate *_priv = MIRAGE_PARSER_ISO_GET_PRIVATE(self);

    gboolean succeeded = TRUE;
    GObject *session = NULL;
    GObject *track = NULL;
    GObject *data_fragment = NULL;

    /* Create data fragment */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __func__);
    data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, filename, error);
    if (!data_fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __func__);
        return FALSE;
    }
        
    /* Set track file */
    mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), g_fopen(filename, "r"), NULL);
    mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), _priv->track_sectsize, NULL);
    mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), FR_BIN_TFILE_DATA, NULL);
    
    /* Use whole file */
    if (!mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(data_fragment), error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to use the rest of file!\n", __func__);
        g_object_unref(data_fragment);
        return FALSE;
    }
    
    /* Add track */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track\n", __func__);

    mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), -1, &session, NULL);
    succeeded = mirage_session_add_track_by_index(MIRAGE_SESSION(session), -1, &track, error);
    g_object_unref(session);
    if (!succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
        g_object_unref(data_fragment);
        return succeeded;
    }
    
    /* Set track mode */
    mirage_track_set_mode(MIRAGE_TRACK(track), _priv->track_mode, NULL);
        
    /* Add fragment to track */
    if (!mirage_track_add_fragment(MIRAGE_TRACK(track), -1, &data_fragment, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add fragment!\n", __func__);
        g_object_unref(data_fragment);
        g_object_unref(track);
        return FALSE;
    }
        
    g_object_unref(data_fragment);
    g_object_unref(track);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished loading track\n", __func__);
    
    return TRUE;
}

static gboolean __mirage_parser_iso_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_ISOPrivate *_priv = MIRAGE_PARSER_ISO_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Check if file can be loaded */
    if (!__mirage_parser_iso_is_file_valid(self, filenames[0], error)) {
        return FALSE;
    }
    
    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);

    /* Set filenames */
    mirage_disc_set_filenames(MIRAGE_DISC(_priv->disc), filenames, NULL);
    
    /* Session: one session (with possibly multiple tracks) */
    GObject *session = NULL;
    if (!mirage_disc_add_session_by_number(MIRAGE_DISC(_priv->disc), 1, &session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);
        succeeded = FALSE;
        goto end;
    }
    /* ISO image parser assumes single-track image, so we're dealing with regular CD-ROM session */
    mirage_session_set_session_type(MIRAGE_SESSION(session), MIRAGE_SESSION_CD_ROM, NULL);
    g_object_unref(session);
    
    /* Load track */
    if (!__mirage_parser_iso_load_track(self, filenames[0], error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load track!\n", __func__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_parser_guess_medium_type(self, _priv->disc);
    mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), medium_type, NULL);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(self, _priv->disc, NULL);
    }
    
end:    
    /* Return disc */
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

static void __mirage_parser_iso_instance_init (GTypeInstance *instance, gpointer g_class) {
    /* Create parser info */
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-ISO",
        "ISO Image Parser",
        "1.0.0",
        "Rok Mandeljc",
        FALSE,
        "ISO images",
        4, ".iso", ".udf", ".img", NULL
    );
    
    return;
}

static void __mirage_parser_iso_finalize (GObject *obj) {
    MIRAGE_Parser_ISO *self = MIRAGE_PARSER_ISO(obj);
    /*MIRAGE_Parser_ISOPrivate *_priv = MIRAGE_PARSER_ISO_GET_PRIVATE(self);*/
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}


static void __mirage_parser_iso_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_ISOClass *klass = MIRAGE_PARSER_ISO_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_ISOPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_iso_finalize;
    
    /* Initialize MIRAGE_Parser methods */
    class_parser->load_image = __mirage_parser_iso_load_image;
        
    return;
}

GType mirage_parser_iso_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_ISOClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_iso_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_ISO),
            0,      /* n_preallocs */
            __mirage_parser_iso_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_ISO", &info, 0);
    }
    
    return type;
}
