/*
 *  libMirage: DAA image parser: Parser object
 *  Copyright (C) 2008-2009 Rok Mandeljc
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

#define __debug__ "DAA-Parser"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_DAA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_DAA, MIRAGE_Parser_DAAPrivate))

typedef struct {
    GObject *disc;
} MIRAGE_Parser_DAAPrivate;


/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                     *
\******************************************************************************/
static gboolean __mirage_parser_daa_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {   
    MIRAGE_Parser_DAAPrivate *_priv = MIRAGE_PARSER_DAA_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    FILE *file;
    gchar signature[16] = "";
    
    /* Open file */
    file = g_fopen(filenames[0], "r");
    if (!file) {
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
        
    /* Read signature */
    fseeko(file, 0, SEEK_SET);
    if (fread(signature, 16, 1, file) < 1) {
        fclose(file);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }
    fclose(file);
        
    /* Check signature (we're comparing -all- 16 bytes!) */
    if (memcmp(signature, daa_main_signature, sizeof(daa_main_signature))) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }
    
    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);

    mirage_disc_set_filename(MIRAGE_DISC(_priv->disc), filenames[0], NULL);
    
    /* Add session */
    GObject *session = NULL;
    
    if (!mirage_disc_add_session_by_number(MIRAGE_DISC(_priv->disc), 1, &session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }
    
    mirage_session_set_session_type(MIRAGE_SESSION(session), MIRAGE_SESSION_CD_ROM, NULL);
    
    /* Add track */
    GObject *track = NULL;
    succeeded = mirage_session_add_track_by_index(MIRAGE_SESSION(session), -1, &track, error);
    g_object_unref(session);
    if (!succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }

    mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE1, NULL);

    /* Try to get password from parser parameters */
    gchar *password = NULL;
    mirage_parser_get_param_string(self, "password", (const gchar **)&password, NULL);
    
    /* Fragment(s); we use private, DAA fragments for this */
    GObject *data_fragment = g_object_new(MIRAGE_TYPE_FRAGMENT_DAA, NULL);
    GError *local_error = NULL;
    
    if (!mirage_track_add_fragment(MIRAGE_TRACK(track), -1, &data_fragment, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add fragment!\n", __debug__);
        g_object_unref(data_fragment);
        g_object_unref(track);
        succeeded = FALSE;
        goto end;
    }
    
    if (!mirage_fragment_daa_set_file(MIRAGE_FRAGMENT(data_fragment), filenames[0], password, &local_error)) {
        /* Don't make buzz for password failures */
        if (local_error->code != MIRAGE_E_NEEDPASSWORD 
            && local_error->code != MIRAGE_E_WRONGPASSWORD) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set file to fragment!\n", __debug__);
        }
        g_propagate_error(error, local_error);
        g_object_unref(data_fragment);
        g_object_unref(track);
        succeeded = FALSE;
        goto end;
    }
    
    g_object_unref(data_fragment);
    g_object_unref(track);

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

static void __mirage_parser_daa_instance_init (GTypeInstance *instance, gpointer g_class) {
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-DAA",
        "DAA Image Parser",
        "PowerISO direct access archives",
        "application/libmirage-daa"
    );
    
    return;
}

static void __mirage_parser_daa_finalize (GObject *obj) {
    MIRAGE_Parser_DAA *self = MIRAGE_PARSER_DAA(obj);
    /*MIRAGE_Parser_DAAPrivate *_priv = MIRAGE_PARSER_DAA_GET_PRIVATE(self);*/
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}


static void __mirage_parser_daa_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_DAAClass *klass = MIRAGE_PARSER_DAA_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_DAAPrivate));
    
    /* Initialize GObject members */
    class_gobject->finalize = __mirage_parser_daa_finalize;
    
    /* Initialize MIRAGE_Parser members */
    class_parser->load_image = __mirage_parser_daa_load_image;
        
    return;
}

GType mirage_parser_daa_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_DAAClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_daa_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_DAA),
            0,      /* n_preallocs */
            __mirage_parser_daa_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_DAA", &info, 0);
    }
    
    return type;
}
