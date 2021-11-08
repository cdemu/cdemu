/*
 *  libMirage: chd image: parser
 *  Copyright (C) 2011-2014 Rok Mandeljc
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

#include "image-chd.h"

#define __debug__ "CHD-Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
struct _MirageParserChdPrivate {
    MirageDisc *disc;

    const chd_header* header;
    const gchar *chd_filename;

    MirageSession *cur_session;
    MirageTrack *cur_track;
};


static gboolean mirage_parser_chd_is_file_valid (MirageParserChd *self, MirageStream *stream, GError **error)
{
    chd_error err;
    chd_file* file;
    unsigned int totalbytes;
    guint64 file_size;

    /* Set filenames */
    self->priv->chd_filename = mirage_stream_get_filename(stream);
    mirage_disc_set_filename(self->priv->disc, self->priv->chd_filename);

    /* File must have .chd suffix */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: verifying image file's suffix...\n%s\n", __debug__, self->priv->chd_filename);
    if (!mirage_helper_has_suffix(self->priv->chd_filename, ".chd")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: invalid suffix (not a *.chd file!)!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: invalid suffix!"));
        return FALSE;
    }

    /* Opening image file for reading */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: opening image file for reading... CHD_OPEN_READ=%d\n", __debug__, CHD_OPEN_READ);
    
    // Enter the abyss after this
    return FALSE;
    
    err = chd_open(self->priv->chd_filename, CHD_OPEN_READ, NULL, &file);
    if (err) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: chd_open() error: %s\n", __debug__, chd_error_string(err));
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: failed to open image!"));
        return FALSE;
    }

     /* Retrieve chd header */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: reading chd header...\n", __debug__);
    self->priv->header = chd_get_header(file);
    totalbytes = self->priv->header->hunkbytes * self->priv->header->totalhunks;
    mirage_stream_seek(stream, 0, G_SEEK_END, NULL);
    file_size = mirage_stream_tell(stream);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: verifying file length:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s:  expected size (based on header): %d \n", __debug__, totalbytes);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s:  actual data file size: %" G_GINT64_MODIFIER "d\n", __debug__, file_size);

    if (file_size == totalbytes) {
        return TRUE;
    }

    /* Nope, can't load the file */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: invalid data file size!\n", __debug__);
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image!"));
    return FALSE;
}


/**********************************************************************\
 *                MirageParser methods implementation               *
\**********************************************************************/
static MirageDisc *mirage_parser_chd_load_image (MirageParser *_self, MirageStream **streams, GError **error)
{
    MirageParserChd *self = MIRAGE_PARSER_CHD(_self);
    gboolean succeeded = TRUE;
    MirageStream *stream;

    /* Check if file can be loaded */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if parser can handle given image...\n", __debug__);

    stream = g_object_ref(streams[0]);

    if (!mirage_parser_chd_is_file_valid(self, stream, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: invalid CHD file!\n", __debug__);
        g_object_unref(stream);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser can handle given image!\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CHD filename: %s\n", __debug__, self->priv->chd_filename);

    // Lets get to here first then we can get creative.

    /* Return disc */
    if (succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);
        return self->priv->disc;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
        g_object_unref(self->priv->disc);
        return NULL;
    }
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE_EXTENDED(MirageParserChd,
                               mirage_parser_chd,
                               MIRAGE_TYPE_PARSER,
                               0,
                               G_ADD_PRIVATE_DYNAMIC(MirageParserChd))

void mirage_parser_chd_type_register (GTypeModule *type_module)
{
    return mirage_parser_chd_register_type(type_module);
}


static void mirage_parser_chd_init (MirageParserChd *self)
{
    self->priv = mirage_parser_chd_get_instance_private(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-CHD",
        Q_("CHD Image Parser"),
        1,
        Q_("chd images (*.chd)"), "application/x-mame-chd"
    );
}

static void mirage_parser_chd_dispose (GObject *gobject)
{
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_chd_parent_class)->dispose(gobject);
}

static void mirage_parser_chd_finalize (GObject *gobject)
{
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_chd_parent_class)->finalize(gobject);
}

static void mirage_parser_chd_class_init (MirageParserChdClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->dispose = mirage_parser_chd_dispose;
    gobject_class->finalize = mirage_parser_chd_finalize;

    parser_class->load_image = mirage_parser_chd_load_image;
}

static void mirage_parser_chd_class_finalize (MirageParserChdClass *klass G_GNUC_UNUSED)
{
}
