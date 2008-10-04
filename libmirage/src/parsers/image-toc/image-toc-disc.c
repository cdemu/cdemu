/*
 *  libMirage: TOC image parser: Disc object
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

#include "image-toc.h"


/* Some prototypes from flex/bison */
int yylex_init (void *scanner);
void yyset_in  (FILE *in_str, void *yyscanner);
int yylex_destroy (void *yyscanner);
int yyparse (void *scanner, MIRAGE_Session *self, GError **error);


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_DISC_TOC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DISC_TOC, MIRAGE_Disc_TOCPrivate))

typedef struct {  
    /* Parser info */
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_Disc_TOCPrivate;


/******************************************************************************\
 *                     MIRAGE_Disc methods implementation                     *
\******************************************************************************/
static gboolean __mirage_disc_toc_get_parser_info (MIRAGE_Disc *self, MIRAGE_ParserInfo **parser_info, GError **error) {
    MIRAGE_Disc_TOCPrivate *_priv = MIRAGE_DISC_TOC_GET_PRIVATE(self);
    *parser_info = _priv->parser_info;
    return TRUE;
}

static gboolean __mirage_disc_toc_can_load_file (MIRAGE_Disc *self, gchar *filename, GError **error) {
    MIRAGE_Disc_TOCPrivate *_priv = MIRAGE_DISC_TOC_GET_PRIVATE(self);

    /* Does file exist? */
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        return FALSE;
    }
    
    /* Check the extension (.toc) */
    if (!mirage_helper_match_suffixes(filename, _priv->parser_info->suffixes)) {
        return FALSE;
    }
    
    return TRUE;
}

static gboolean __mirage_disc_toc_load_image (MIRAGE_Disc *self, gchar **filenames, GError **error) {
    gint length = 0;
    gint i;
        
    mirage_disc_set_filenames(self, filenames, NULL);
    
    /* Each TOC/BIN is one session, so we load all given filenames */
    for (i = 0; i < g_strv_length(filenames); i++) {
        void *scanner = NULL;
        FILE *file = NULL;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading session #%i: '%s'!\n", __func__, i, filenames[i]);
        
        /* There's slight problem with multi-session TOC images, namely that each
           TOC can be used independently... in order words, there's no way to determine
           the length of leadouts for sessions (since all sessions start at sector 0).
           So we use what multisession FAQ from cdrecord docs tells us... */
        if (i > 0) {
            GObject *prev_session = NULL;
            gint leadout_length = 0;

            if (!mirage_disc_get_session_by_index(self, -1, &prev_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get previous session!\n", __func__);
                return FALSE;
            }
            
            /* Second session has index 1... */
            if (i == 1) {
                leadout_length = 11250; /* Actually, it should be 6750 previous leadout, 4500 current leadin */
            } else {
                leadout_length = 6750; /* Actually, it should be 2250 previous leadout, 4500 current leadin */                
            }
            
            mirage_session_set_leadout_length(MIRAGE_SESSION(prev_session), leadout_length, NULL);
            
            g_object_unref(prev_session);
        }
        
        /* Open file */
        file = g_fopen(filenames[i], "r");
        if (!file) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to open file '%s'!\n", __func__, filenames[i]);
            mirage_error(MIRAGE_E_IMAGEFILE, error);
            return FALSE;
        }
        
        /* Create TOC session */
        GObject *session = g_object_new(MIRAGE_TYPE_SESSION_TOC, NULL);
        __mirage_session_toc_set_toc_filename(MIRAGE_SESSION(session), filenames[i], NULL);
        if (!mirage_disc_add_session_by_index(self, -1, &session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to add session!\n", __func__);
            return FALSE;
        }
        
        /* Prepare scanner */
        yylex_init(&scanner);
        yyset_in(file, scanner);
        
        /* Load */
        if (yyparse(scanner, MIRAGE_SESSION(session), error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to parse TOC file!\n", __func__);
            fclose(file);
            return FALSE;
        }
        
        /* Destroy scanner */
        yylex_destroy(scanner);        
        fclose(file);
        
        g_object_unref(session);
    }
    
    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_helper_guess_medium_type(self);
    mirage_disc_set_medium_type(self, medium_type, NULL);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc length implies CD-ROM image; setting Red Book pregaps\n", __func__);
        mirage_helper_add_redbook_pregap(self);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc length implies non CD-ROM image\n", __func__);
    }
   
    return TRUE;
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_DiscClass *parent_class = NULL;

static void __mirage_disc_toc_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Disc_TOC *self = MIRAGE_DISC_TOC(instance);
    MIRAGE_Disc_TOCPrivate *_priv = MIRAGE_DISC_TOC_GET_PRIVATE(self);
    
    /* Create parser info */
    _priv->parser_info = mirage_helper_create_parser_info(
        "PARSER-TOC",
        "TOC Image Parser",
        "1.0.0",
        "Rok Mandeljc",
        TRUE,
        "TOC files",
        2, ".toc", NULL
    );
    
    return;
}

static void __mirage_disc_toc_finalize (GObject *obj) {
    MIRAGE_Disc_TOC *self = MIRAGE_DISC_TOC(obj);
    MIRAGE_Disc_TOCPrivate *_priv = MIRAGE_DISC_TOC_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);
    
    /* Free parser info */
    mirage_helper_destroy_parser_info(_priv->parser_info);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}


static void __mirage_disc_toc_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_DiscClass *class_disc = MIRAGE_DISC_CLASS(g_class);
    MIRAGE_Disc_TOCClass *klass = MIRAGE_DISC_TOC_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Disc_TOCPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_disc_toc_finalize;
    
    /* Initialize MIRAGE_Disc methods */
    class_disc->get_parser_info = __mirage_disc_toc_get_parser_info;
    class_disc->can_load_file = __mirage_disc_toc_can_load_file;
    class_disc->load_image = __mirage_disc_toc_load_image;
        
    return;
}

GType mirage_disc_toc_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Disc_TOCClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_disc_toc_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Disc_TOC),
            0,      /* n_preallocs */
            __mirage_disc_toc_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_DISC, "MIRAGE_Disc_TOC", &info, 0);
    }
    
    return type;
}
