/*
 *  libMirage: X-CD-Roast image parser: Parser object
 *  Copyright (C) 2009 Rok Mandeljc
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

#include "image-xcdroast.h"

#define __debug__ "X-CD-Roast-Parser"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_XCDROAST_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_XCDROAST, MIRAGE_Parser_XCDROASTPrivate))

typedef struct {  
    GObject *disc;

    /* Internal stuff */
} MIRAGE_Parser_XCDROASTPrivate;

/******************************************************************************\
 *                         Parser private functions                           *
\******************************************************************************/


/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                     *
\******************************************************************************/
static gboolean __check_toc_file (MIRAGE_Parser *self, const gchar *filename) {
    MIRAGE_Parser_XCDROASTPrivate *_priv = MIRAGE_PARSER_XCDROAST_GET_PRIVATE(self);
    gboolean succeeded = FALSE;

    /* Check suffix - must be .toc */
    if (!mirage_helper_has_suffix(filename, ".toc")) {
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: FIXME!\n", __debug__);
    
    return succeeded;
}

static gboolean __mirage_parser_xcdroast_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_XCDROASTPrivate *_priv = MIRAGE_PARSER_XCDROAST_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Check if we can load file */
    if (!__check_toc_file(self, filenames[0])) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }
    
    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);

    mirage_disc_set_filenames(MIRAGE_DISC(_priv->disc), filenames, NULL);

    // FIXME
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: FIXME!\n", __debug__);
    succeeded = FALSE;
    
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

static void __mirage_parser_xcdroast_instance_init (GTypeInstance *instance, gpointer g_class) {
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-XCDROAST",
        "X-CD-Roast Image Parser",
        "X-CD-Roast TOC files",
        "application/libmirage-xcdroast"
    );
    
    //__mirage_parser_xcdroast_init_regex_parser(MIRAGE_PARSER(instance));
    
    return;
}

static void __mirage_parser_xcdroast_finalize (GObject *obj) {
    MIRAGE_Parser_XCDROAST *self = MIRAGE_PARSER_XCDROAST(obj);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);
    
    /* Cleanup regex parser engine */
    //__mirage_parser_xcdroast_cleanup_regex_parser(MIRAGE_PARSER(self));
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_parser_xcdroast_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_XCDROASTClass *klass = MIRAGE_PARSER_XCDROAST_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_XCDROASTPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_xcdroast_finalize;
    
    /* Initialize MIRAGE_Parser methods */
    class_parser->load_image = __mirage_parser_xcdroast_load_image;
        
    return;
}

GType mirage_parser_xcdroast_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_XCDROASTClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_xcdroast_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_XCDROAST),
            0,      /* n_preallocs */
            __mirage_parser_xcdroast_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_XCDROAST", &info, 0);
    }
    
    return type;
}
