/*
 *  libMirage: Parser object
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Parser"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER, MIRAGE_ParserPrivate))

typedef struct {
    GHashTable *parser_params;
    
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_ParserPrivate;


/******************************************************************************\
 *                              Private functions                             *
\******************************************************************************/
static void __destroy_parser_info (MIRAGE_ParserInfo *info) {
    /* Free info and its content */
    if (info) {
        g_free(info->id);
        g_free(info->name);
        g_free(info->description);
        g_free(info->mime_type);
        
        g_free(info);
    }
    
    return;
}

/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_parser_generate_parser_info:
 * @self: a #MIRAGE_Parser
 * @id: parser ID
 * @name: parser name
 * @description: image file description
 * @mime_type: image file MIME type
 *
 * <para>
 * Generates parser information from the input fields. It is intended as a function
 * for creating parser information in parser implementations.
 * </para>
 **/
void mirage_parser_generate_parser_info (MIRAGE_Parser *self, const gchar *id, const gchar *name, const gchar *description, const gchar *mime_type) {
    MIRAGE_ParserPrivate *_priv = MIRAGE_PARSER_GET_PRIVATE(self);
    
    /* Free old info */
    __destroy_parser_info(_priv->parser_info);
    
    /* Create new info */
    _priv->parser_info = g_new0(MIRAGE_ParserInfo, 1);
    
    _priv->parser_info->id = g_strdup(id);
    _priv->parser_info->name = g_strdup(name);
        
    _priv->parser_info->description = g_strdup(description);
    _priv->parser_info->mime_type = g_strdup(mime_type);
    
    return;
}

/**
 * mirage_parser_get_parser_info:
 * @self: a #MIRAGE_Parser
 * @parser_info: location to store parser info
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves parser information.
 * </para>
 *
 * <para>
 * A pointer to parser information structure is stored in @parser_info; the structure
 * belongs to the object and therefore should not be modified.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_parser_get_parser_info (MIRAGE_Parser *self, const MIRAGE_ParserInfo **parser_info, GError **error) {
    MIRAGE_ParserPrivate *_priv = MIRAGE_PARSER_GET_PRIVATE(self);

    if (!_priv->parser_info) {
        mirage_error(MIRAGE_E_DATANOTSET, error);
        return FALSE;
    }
    
    *parser_info = _priv->parser_info;
    return TRUE;
}


/**
 * mirage_parser_load_image:
 * @self: a #MIRAGE_Parser
 * @filenames: image filename(s)
 * @disc: location to store the resulting #MIRAGE_Disc object
 * @error: location to store error, or %NULL
 *
 * <para>
 * Loads the image stored in @filenames.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_parser_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    /* Provided by implementation */
    if (!MIRAGE_PARSER_GET_CLASS(self)->load_image) {
        mirage_error(MIRAGE_E_NOTIMPL, error);
        return FALSE;
    }
    
    return MIRAGE_PARSER_GET_CLASS(self)->load_image(self, filenames, disc, error);
}


/**
 * mirage_parser_guess_medium_type:
 * @self: a #MIRAGE_Parser
 * @disc: disc object
 *
 * <para>
 * Attempts to guess medium type by looking at the length of the disc layout.
 * Currently, it supports identification of CD-ROM media, which are assumed to
 * have layout length of 90 minutes or less.
 * </para>
 *
 * <para>
 * Note that this function does not set the medium type to disc object; you still
 * need to do it via mirage_disc_set_medium_type(). It is meant to be used in 
 * simple parsers whose image files don't provide medium type information.
 * </para>
 *
 * Returns: a value from #MIRAGE_MediumTypes, according to the guessed medium type.
 **/
gint mirage_parser_guess_medium_type (MIRAGE_Parser *self, GObject *disc) {
    gint length = 0;
    
    mirage_disc_layout_get_length(MIRAGE_DISC(disc), &length, NULL);
    
    /* FIXME: add other media types? */
    if (length <= 90*60*75) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc layout size implies CD-ROM image\n", __debug__);
        return MIRAGE_MEDIUM_CD;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc layout size implies DVD-ROM image\n", __debug__);
        return MIRAGE_MEDIUM_DVD;
    };
}

/**
 * mirage_parser_add_redbook_pregap:
 * @self: a #MIRAGE_Parser
 * @disc: disc object
 * @error: location to store error, or %NULL
 *
 * <para>
 * A helper function, intended to be used in simpler parsers that don't get proper
 * pregap information from the image file. 
 * </para>
 *
 * <para>
 * First, it sets disc layout start to -150. Then, it adds 150-sector pregap to
 * first track of each session found on the layout; for this, a NULL fragment is
 * used. If track already has a pregap, then the pregaps are stacked.
 * </para>
 *
 * <para>
 * Note that the function works only on discs which have medium type set to 
 * CD-ROM.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_parser_add_redbook_pregap (MIRAGE_Parser *self, GObject *disc, GError **error) {
    gint medium_type = 0;
    gint num_sessions = 0;
    gint i;
    
    mirage_disc_get_medium_type(MIRAGE_DISC(disc), &medium_type, NULL);
    
    /* Red Book pregap is found only on CD-ROMs */
    if (medium_type != MIRAGE_MEDIUM_CD) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Red Book pregap exists only on CD-ROMs!\n", __debug__);
        mirage_error(MIRAGE_E_INVALIDMEDIUM, error);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding Red Book pregaps to the disc...\n", __debug__);
    
    /* CD-ROMs start at -150 as per Red Book... */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting disc layout start at -150\n", __debug__);
    mirage_disc_layout_set_start_sector(MIRAGE_DISC(disc), -150, NULL);
        
    mirage_disc_get_number_of_sessions(MIRAGE_DISC(disc), &num_sessions, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %d session(s)\n", __debug__, num_sessions);

    /* Put 150 sector pregap into every first track of each session */
    for (i = 0; i < num_sessions; i++) {
        GObject *session = NULL;
        GObject *ftrack = NULL;
                
        if (!mirage_disc_get_session_by_index(MIRAGE_DISC(disc), i, &session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get session with index %i!\n", __debug__, i);
            return FALSE;
        }
            
        if (!mirage_session_get_track_by_index(MIRAGE_SESSION(session), 0, &ftrack, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to first track of session with index %i!\n", __debug__, i);
            g_object_unref(session);
            return FALSE;
        }

        /* Add pregap fragment (empty) */
        GObject *pregap_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_NULL, "NULL", NULL);
        if (!pregap_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create pregap fragment!\n", __debug__);
            g_object_unref(session);
            g_object_unref(ftrack);
            mirage_error(MIRAGE_E_PARSER, error);
            return FALSE;
        }
        mirage_track_add_fragment(MIRAGE_TRACK(ftrack), 0, &pregap_fragment, NULL);
        mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), 150, NULL);
        g_object_unref(pregap_fragment);
            
        /* Track starts at 150... well, unless it already has a pregap, in
           which case they should stack */
        gint old_start = 0;
        mirage_track_get_track_start(MIRAGE_TRACK(ftrack), &old_start, NULL);
        old_start += 150;
        mirage_track_set_track_start(MIRAGE_TRACK(ftrack), old_start, NULL);
        
        g_object_unref(ftrack);
        g_object_unref(session);
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: added 150 pregap to first track in session %i\n", __debug__, i);
    }
    
    return TRUE;
}


/**
 * mirage_parser_set_params:
 * @self: a #MIRAGE_Parser
 * @params: a #GHashTable containing parameters
 * @error: location to store error, or %NULL
 *
 * <para>
 * An internal function that sets the parsing parameters to parser 
 * (such as password, encoding, etc.). It is meant to be used by libmirage_create_disc()
 * to pass the parsing parameters to parser before performing the parsing.
 * </para>
 *
 * <para>
 * @params is a #GHashTable that must have strings for its keys and values of 
 * #GValue type.
 * </para>
 *
 * <para>
 * Note that only pointer to @params is stored; therefore, the hash table must
 * still be valid when mirage_parser_load_image() is called. Another thing to
 * note is that whether parameter is used or not is up to the parser implementation.
 * In case of unsupported parameter, the parser implementation should simply ignore it.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_parser_set_params (MIRAGE_Parser *self, GHashTable *params, GError **error) {
    MIRAGE_ParserPrivate *_priv = MIRAGE_PARSER_GET_PRIVATE(self);
    _priv->parser_params = params; /* Just store pointer */
    return TRUE;
}

/**
 * mirage_parser_get_param_string:
 * @self: a #MIRAGE_Parser
 * @name: parameter name (key)
 * @value: location to store the string value, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * An internal function that retrieves a string parameter named @name. It is meant
 * to be used by parser implementation to retrieve the parameter value during the
 * parsing.
 * </para>
 *
 * <para>
 * Note that pointer to string is returned; the string belongs to whoever owns
 * the parameters hash table that was passed to the parser, and as such should
 * not be freed after no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_parser_get_param_string (MIRAGE_Parser *self, const gchar *name, const gchar **ret_value, GError **error) {
    MIRAGE_ParserPrivate *_priv = MIRAGE_PARSER_GET_PRIVATE(self);
    GValue *value;
    
    if (!_priv->parser_params) {
        mirage_error(MIRAGE_E_GENERIC, error);
        return FALSE;
    }
    
    value = g_hash_table_lookup(_priv->parser_params, name);
        
    if (!value) {
        mirage_error(MIRAGE_E_GENERIC, error);
        return FALSE;
    }
    
    if (!G_VALUE_HOLDS_STRING(value)) {
        mirage_error(MIRAGE_E_GENERIC, error);
        return FALSE;
    }
    
    if (ret_value) {
        *ret_value = g_value_get_string(value);
    }
    
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __mirage_parser_finalize (GObject *obj) {
    MIRAGE_Parser *self = MIRAGE_PARSER(obj);
    MIRAGE_ParserPrivate *_priv = MIRAGE_PARSER_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);
    
    /* Free parser info */
    __destroy_parser_info(_priv->parser_info);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_parser_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *klass = MIRAGE_PARSER_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_ParserPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_finalize;
    
    /* Initialize MIRAGE_Parser methods */
    klass->load_image = NULL;
    
    return;
}

GType mirage_parser_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_ParserClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser),
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "MIRAGE_Parser", &info, 0);
    }
    
    return type;
}
