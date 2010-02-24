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


/* Regex engine */
typedef gboolean (*MIRAGE_RegexCallback) (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error);

typedef struct {
    GRegex *regex;
    MIRAGE_RegexCallback callback_func;
} MIRAGE_RegexRule;


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_XCDROAST_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_XCDROAST, MIRAGE_Parser_XCDROASTPrivate))

typedef struct {  
    GObject *disc;

    gchar *toc_filename;
    
    /* Regex engine */
    GList *regex_rules_toc;
    GList *regex_rules_xinf;

    GRegex *regex_comment_ptr; /* Pointer, do not free! */
} MIRAGE_Parser_XCDROASTPrivate;

/******************************************************************************\
 *                         Parser private functions                           *
\******************************************************************************/


/******************************************************************************\
 *                           Regex parsing engine                             *
\******************************************************************************/
static gboolean __mirage_parser_xcdroast_callback_toc_comment (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gchar *comment = g_match_info_fetch_named(match_info, "comment");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed COMMENT: %s\n", __debug__, comment);
    
    g_free(comment);
    
    return TRUE;
}

#define APPEND_REGEX_RULE(list,rule,callback) {                         \
    MIRAGE_RegexRule *new_rule = g_new(MIRAGE_RegexRule, 1);            \
    new_rule->regex = g_regex_new(rule, G_REGEX_OPTIMIZE, 0, NULL);     \
    new_rule->callback_func = callback;                                 \
                                                                        \
    list = g_list_append(list, new_rule);                               \
}

static void __mirage_parser_xcdroast_init_regex_parser (MIRAGE_Parser *self) {
    MIRAGE_Parser_XCDROASTPrivate *_priv = MIRAGE_PARSER_XCDROAST_GET_PRIVATE(self);

    /* *** TOC parser *** */
    /* Ignore empty lines */
    APPEND_REGEX_RULE(_priv->regex_rules_toc, "^[\\s]*$", NULL);
    
    /* Comment */
    APPEND_REGEX_RULE(_priv->regex_rules_toc, "^#\\s+(?<comment>.+)$", __mirage_parser_xcdroast_callback_toc_comment);

    /* Store pointer to comment's regex rule */
    GList *elem_comment = g_list_last(_priv->regex_rules_toc);
    MIRAGE_RegexRule *rule_comment = elem_comment->data;
    _priv->regex_comment_ptr = rule_comment->regex;

    /* *** XINF parser ***/
    
    return;
}

static void __mirage_parser_xcdroast_cleanup_regex_parser (MIRAGE_Parser *self) {
    MIRAGE_Parser_XCDROASTPrivate *_priv = MIRAGE_PARSER_XCDROAST_GET_PRIVATE(self);
    GList *entry;
    
    G_LIST_FOR_EACH(entry, _priv->regex_rules_toc) {
        MIRAGE_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }
    
    g_list_free(_priv->regex_rules_toc);

    G_LIST_FOR_EACH(entry, _priv->regex_rules_xinf) {
        MIRAGE_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }
    
    g_list_free(_priv->regex_rules_xinf);
}


static gboolean __mirage_parser_xcdroast_parse_toc_file (MIRAGE_Parser *self, gchar *filename, GError **error) {
    MIRAGE_Parser_XCDROASTPrivate *_priv = MIRAGE_PARSER_XCDROAST_GET_PRIVATE(self);
    GError *io_error = NULL;
    GIOChannel *io_channel;
    gboolean succeeded = TRUE;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: opening file: %s\n", __debug__, filename);
    
    /* Create IO channel for file */
    io_channel = g_io_channel_new_file(filename, "r", &io_error);
    if (!io_channel) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create IO channel: %s\n", __debug__, io_error->message);
        g_error_free(io_error);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    return succeeded;
}

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

    /* *** Additional check ***
       Because X-CD Roast also uses .toc for its images, we need to make
       sure this one was created by cdrdao... for that, we check for presence
       of CD_DA/CD_ROM_XA/CD_ROM/CD_I directive. */
    GIOChannel *io_channel;
        
    /* Create IO channel for file */
    io_channel = g_io_channel_new_file(filename, "r", NULL);
    if (!io_channel) {
        return FALSE;
    }
        
    /* Read file line-by-line */
    gint line_nr;
    for (line_nr = 1; ; line_nr++) {
        GIOStatus status;
        gchar *line_str;
        gsize line_len;

        GMatchInfo *match_info = NULL;

        status = g_io_channel_read_line(io_channel, &line_str, &line_len, NULL, NULL);
        
        /* Handle EOF */
        if (status == G_IO_STATUS_EOF) {
            break;
        }
        
        /* Handle abnormal status */
        if (status != G_IO_STATUS_NORMAL) {
            break;
        }
        
        /* Try to match the rule */
        if (g_regex_match(_priv->regex_comment_ptr, line_str, 0, &match_info)) {
            gchar *comment = g_match_info_fetch_named(match_info, "comment");

            /* Search for "X-CD-Roast" in the comment... */
            if (g_strrstr(comment, "X-CD-Roast")) {
                succeeded = TRUE;
            }
            
            g_free(comment);

            /* Free match info */
            g_match_info_free(match_info);
        }
        
        g_free(line_str);
                
        /* If we found the header, break the loop */
        if (succeeded) {
            break;
        }
    }
    
    g_io_channel_unref(io_channel);
        
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

    mirage_disc_set_filename(MIRAGE_DISC(_priv->disc), filenames[0], NULL);
    _priv->toc_filename = g_strdup(filenames[0]);

    /* Parse the CUE */
    if (!__mirage_parser_xcdroast_parse_toc_file(self, _priv->toc_filename, error)) {
        succeeded = FALSE;
        goto end;
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

static void __mirage_parser_xcdroast_instance_init (GTypeInstance *instance, gpointer g_class) {
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-XCDROAST",
        "X-CD-Roast Image Parser",
        "X-CD-Roast TOC files",
        "application/libmirage-xcdroast"
    );
    
    __mirage_parser_xcdroast_init_regex_parser(MIRAGE_PARSER(instance));
    
    return;
}

static void __mirage_parser_xcdroast_finalize (GObject *obj) {
    MIRAGE_Parser_XCDROAST *self = MIRAGE_PARSER_XCDROAST(obj);
    MIRAGE_Parser_XCDROASTPrivate *_priv = MIRAGE_PARSER_XCDROAST_GET_PRIVATE(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);

    g_free(_priv->toc_filename);
    
    /* Cleanup regex parser engine */
    __mirage_parser_xcdroast_cleanup_regex_parser(MIRAGE_PARSER(self));
    
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
