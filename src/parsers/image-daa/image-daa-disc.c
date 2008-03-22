/*
 *  libMirage: DAA image parser: Disc object
 *  Copyright (C) 2008 Rok Mandeljc
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

#include "image-daa.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_DISC_DAA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DISC_DAA, MIRAGE_Disc_DAAPrivate))

typedef struct {
    /* Parser info */
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_Disc_DAAPrivate;


/******************************************************************************\
 *                     MIRAGE_Disc methods implementation                     *
\******************************************************************************/
static gboolean __mirage_disc_daa_get_parser_info (MIRAGE_Disc *self, MIRAGE_ParserInfo **parser_info, GError **error) {
    MIRAGE_Disc_DAAPrivate *_priv = MIRAGE_DISC_DAA_GET_PRIVATE(self);
    *parser_info = _priv->parser_info;
    return TRUE;
}

static gboolean __mirage_disc_daa_can_load_file (MIRAGE_Disc *self, gchar *filename, GError **error) {
    MIRAGE_Disc_DAAPrivate *_priv = MIRAGE_DISC_DAA_GET_PRIVATE(self);

    /* Does file exist? */
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        return FALSE;
    }
    
    /* Check supported suffixes */
    if (!mirage_helper_match_suffixes(filename, _priv->parser_info->suffixes)) {
        return FALSE;
    }
    
    /* Now the real test... check signature */
    gchar signature[16] = "";
    
    FILE *file = g_fopen(filename, "r");
    if (!file) {
        return FALSE;
    }
    
    /* Read signature */
    fseeko(file, 0, SEEK_SET);
    fread(signature, 16, 1, file);
    
    fclose(file);
    
    gchar daa_signature[16] = "DAA";
    
    if (memcmp(signature, daa_signature, sizeof(daa_signature))) {
        return FALSE;
    }
    
    return TRUE;
}

static gboolean __mirage_disc_daa_load_image (MIRAGE_Disc *self, gchar **filenames, GError **error) {   
    /*MIRAGE_Disc_DAAPrivate *_priv = MIRAGE_DISC_DAA_GET_PRIVATE(self);*/
    gboolean succeeded = TRUE;
    
    /* For now, DAA parser supports only one-file images */
    if (g_strv_length(filenames) > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: only single-file images supported!\n", __func__);
        mirage_error(MIRAGE_E_SINGLEFILE, error);
        return FALSE;
    }
    
    /* Open file */
    FILE *file = g_fopen(filenames[0], "r");
    if (!file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file '%s'!\n", __func__, filenames[0]);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    /* Set filename */
    mirage_disc_set_filenames(self, filenames, NULL);
    
    /* Read signature */
    gchar signature[16] = "";
    gchar daa_signature[16] = "DAA";
    
    /* Read signature */
    fseeko(file, 0, SEEK_SET);
    fread(signature, 16, 1, file);
    
    fclose(file);
    
    if (memcmp(signature, daa_signature, sizeof(daa_signature))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: invalid signature: %.16s\n", __func__, signature);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    /* Add session */
    GObject *session = NULL;
    
    if (!mirage_disc_add_session_by_number(self, 1, &session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);
        return FALSE;
    }
    
    mirage_session_set_session_type(MIRAGE_SESSION(session), MIRAGE_SESSION_CD_ROM, NULL);
    
    /* Add track */
    GObject *track = NULL;
    succeeded = mirage_session_add_track_by_index(MIRAGE_SESSION(session), -1, &track, error);
    g_object_unref(session);
    if (!succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
        return succeeded;
    }

    mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE1, NULL);

    /* Fragment(s); we use private, DAA fragments for this */
    GObject *data_fragment = NULL;
    
    data_fragment = g_object_new(MIRAGE_TYPE_FRAGMENT_DAA, NULL);
    
    
    if (!mirage_track_add_fragment(MIRAGE_TRACK(track), -1, &data_fragment, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add fragment!\n", __func__);
        g_object_unref(data_fragment);
        g_object_unref(track);
        return FALSE;
    }
    
    if (!mirage_fragment_daa_set_file(MIRAGE_FRAGMENT(data_fragment), filenames[0], error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set file to fragment!\n", __func__);
        g_object_unref(data_fragment);
        g_object_unref(track);
        return FALSE;
    }
    
    g_object_unref(data_fragment);
    
    
    /* Now get length and if it surpasses length of 90min CD, assume we
       have a DVD */
    gint length = 0;
    mirage_disc_layout_get_length(self, &length, NULL);
    if (length > 90*60*75) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-ROM image\n", __func__);
        mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_DVD, NULL);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __func__);
        mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_CD, NULL);

        /* If we got CD-ROM, we assume Red Book for now... which means disc starts
           at -150 and first track has 150 sector pregap */
        mirage_disc_layout_set_start_sector(self, -150, NULL);
        
        /* Add pregap fragment (empty) */
        GObject *mirage = NULL;
        if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get Mirage object!\n", __func__);
            g_object_unref(track);
            return FALSE;
        }
        GObject *pregap_fragment = NULL;
        mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_NULL, "NULL", &pregap_fragment, error);
        g_object_unref(mirage);
        if (!pregap_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create pregap fragment\n", __func__);
            g_object_unref(track);
            return FALSE;
        }
        mirage_track_add_fragment(MIRAGE_TRACK(track), 0, &pregap_fragment, NULL);
        mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), 150, NULL);
        g_object_unref(pregap_fragment);
        
        /* Track starts at 150 */
        mirage_track_set_track_start(MIRAGE_TRACK(track), 150, NULL);
    }
    
    
    g_object_unref(track);
    
    return succeeded;
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_DiscClass *parent_class = NULL;

static void __mirage_disc_daa_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Disc_DAA *self = MIRAGE_DISC_DAA(instance);
    MIRAGE_Disc_DAAPrivate *_priv = MIRAGE_DISC_DAA_GET_PRIVATE(self);
    
    /* Create parser info */
    _priv->parser_info = mirage_helper_create_parser_info(
        "PARSER-DAA",
        "DAA Image Parser",
        "1.0.0",
        "Rok Mandeljc",
        FALSE,
        "PowerISO direct access archives",
        2, ".daa", NULL
    );
    
    return;
}

static void __mirage_disc_daa_finalize (GObject *obj) {
    MIRAGE_Disc_DAA *self = MIRAGE_DISC_DAA(obj);
    MIRAGE_Disc_DAAPrivate *_priv = MIRAGE_DISC_DAA_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    /* Free parser info */
    mirage_helper_destroy_parser_info(_priv->parser_info);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}


static void __mirage_disc_daa_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_DiscClass *class_disc = MIRAGE_DISC_CLASS(g_class);
    MIRAGE_Disc_DAAClass *klass = MIRAGE_DISC_DAA_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Disc_DAAPrivate));
    
    /* Initialize GObject members */
    class_gobject->finalize = __mirage_disc_daa_finalize;
    
    /* Initialize MIRAGE_Disc members */
    class_disc->get_parser_info = __mirage_disc_daa_get_parser_info;
    class_disc->can_load_file = __mirage_disc_daa_can_load_file;
    class_disc->load_image = __mirage_disc_daa_load_image;
        
    return;
}

GType mirage_disc_daa_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Disc_DAAClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_disc_daa_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Disc_DAA),
            0,      /* n_preallocs */
            __mirage_disc_daa_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_DISC, "MIRAGE_Disc_DAA", &info, 0);
    }
    
    return type;
}
