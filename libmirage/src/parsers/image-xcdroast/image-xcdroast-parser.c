/*
 *  libMirage: X-CD-Roast image parser: Parser object
 *  Copyright (C) 2009-2012 Rok Mandeljc
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


/**********************************************************************\
 *                           Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_XCDROAST_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_XCDROAST, MIRAGE_Parser_XCDROASTPrivate))

struct _MIRAGE_Parser_XCDROASTPrivate
{
    GObject *disc;

    gchar *toc_filename;

    GObject *cur_session;

    DISC_Info disc_info;
    TOC_Track toc_track;
    XINF_Track xinf_track;

    /* Regex engine */
    GList *regex_rules_toc;
    GList *regex_rules_xinf;

    GRegex *regex_comment_ptr; /* Pointer, do not free! */

    gint set_pregap;
};


/**********************************************************************\
 *                     Parser private functions                       *
\**********************************************************************/
static gboolean mirage_parser_xcdroast_parse_xinf_file (MIRAGE_Parser_XCDROAST *self, gchar *filename, GError **error);

static gchar *create_xinf_filename (gchar *track_filename)
{
    gchar *xinf_filename;
    int baselen = strlen(track_filename);
    int suffixlen = strlen(mirage_helper_get_suffix(track_filename));

    baselen -= suffixlen;

    xinf_filename = g_new0(gchar, baselen + 6); /* baselen + . + xinf + \0 */
    strncpy(xinf_filename, track_filename, baselen); /* Copy basename */
    sprintf(xinf_filename+baselen, ".xinf");

    return xinf_filename;
}

static gboolean mirage_parser_xcdroast_add_track (MIRAGE_Parser_XCDROAST *self, TOC_Track *track_info, GError **error)
{
    GObject *track = NULL;

    /* Add track */
    if (!mirage_session_add_track_by_number(MIRAGE_SESSION(self->priv->cur_session), track_info->number, &track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
        return FALSE;
    }

    /* Add pregap, if needed */
    if (self->priv->set_pregap) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding %d sector pregap\n", __debug__, self->priv->set_pregap);

        /* Creation of NULL fragment should never fail */
        GObject *null_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_NULL, "NULL", G_OBJECT(self), error);

        mirage_fragment_set_length(MIRAGE_FRAGMENT(null_fragment), self->priv->set_pregap);

        mirage_track_add_fragment(MIRAGE_TRACK(track), -1, null_fragment);
        mirage_track_set_track_start(MIRAGE_TRACK(track), self->priv->set_pregap);

        g_object_unref(null_fragment);

        self->priv->set_pregap = 0;
    }

    /* Determine data/audio file's full path */
    gchar *data_file = mirage_helper_find_data_file(track_info->file, self->priv->toc_filename);
    if (!data_file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: file '%s' not found!\n", __debug__, track_info->file);
        g_object_unref(track);
        return FALSE;
    }

    /* Setup basic track info, add fragment */
    switch (track_info->type) {
        case TRACK_TYPE_DATA: {
            /* Data (Mode 1) track */
            mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE1);

            /* Create and add binary fragment, using whole file */
            GObject *data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, data_file, G_OBJECT(self), error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment for file: %s\n", __debug__, data_file);
                g_free(data_file);
                g_object_unref(track);
                return FALSE;
            }

            if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(data_fragment), data_file, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
                g_free(data_file);
                g_object_unref(track);
                g_object_unref(data_fragment);
                return FALSE;
            }
            mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), 2048);
            mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(data_fragment), 0);
            mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), FR_BIN_TFILE_DATA);

            mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(data_fragment), NULL);

            mirage_track_add_fragment(MIRAGE_TRACK(track), -1, data_fragment);

            /* Verify fragment's length vs. declared track length */
            gint fragment_length = mirage_fragment_get_length(MIRAGE_FRAGMENT(data_fragment));
            if (fragment_length != track_info->size) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: data track size mismatch! Declared %d sectors, actual fragment size: %d\n", __debug__, track_info->size, fragment_length);

                /* Data track size mismatch is usually found on mixed-mode
                   CDs... it seems to be around 152 sectors. 150 come from
                   the pregap between data and following audio tracks. No
                   idea where the 2 come from, though (we'll put it all into
                   next track's pregap) */
                self->priv->set_pregap = track_info->size - fragment_length;
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: compensating data track size difference with %d sector pregap to next track\n", __debug__, self->priv->set_pregap);
            }

            g_object_unref(data_fragment);

            break;
        }
        case TRACK_TYPE_AUDIO: {
            /* Audio track */
            mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_AUDIO);

            /* Create and add audio fragment, using whole file */
            GObject *data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_AUDIO, data_file, G_OBJECT(self), error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create AUDIO fragment for file: %s\n", __debug__, data_file);
                g_free(data_file);
                g_object_unref(track);
                return FALSE;
            }
            if (!mirage_frag_iface_audio_set_file(MIRAGE_FRAG_IFACE_AUDIO(data_fragment), data_file, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
                g_free(data_file);
                g_object_unref(track);
                g_object_unref(data_fragment);
                return FALSE;
            }

            mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(data_fragment), NULL);

            mirage_track_add_fragment(MIRAGE_TRACK(track), -1, data_fragment);

            /* Verify fragment's length vs. declared track length */
            gint fragment_length = mirage_fragment_get_length(MIRAGE_FRAGMENT(data_fragment));
            if (fragment_length != track_info->size) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: audio track size mismatch! Declared %d sectors, actual fragment size: %d\n", __debug__, track_info->size, fragment_length);
            }

            g_object_unref(data_fragment);

            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled track type %d!\n", __debug__, track_info->type);
            break;
        }
    }
    g_free(data_file);

    /* Try to get a XINF file and read additional info from it */
    gchar *xinf_filename = create_xinf_filename(track_info->file);
    gchar *real_xinf_filename = mirage_helper_find_data_file(xinf_filename, self->priv->toc_filename);

    if (mirage_parser_xcdroast_parse_xinf_file(self, real_xinf_filename, NULL)) {
        /* Track flags */
        gint flags = 0;
        if (self->priv->xinf_track.copyperm) flags |= MIRAGE_TRACKF_COPYPERMITTED;
        if (self->priv->xinf_track.preemp) flags |= MIRAGE_TRACKF_PREEMPHASIS;

        /* This is valid only for audio track (because data track in non-stereo
           by default */
        if (track_info->type == TRACK_TYPE_AUDIO && !self->priv->xinf_track.stereo) flags |= MIRAGE_TRACKF_FOURCHANNEL;

        mirage_track_set_flags(MIRAGE_TRACK(track), flags);
    }
    g_free(real_xinf_filename);
    g_free(xinf_filename);

    g_object_unref(track);

    return TRUE;
};


/**********************************************************************\
 *                       Regex parsing engine                         *
\**********************************************************************/
typedef gboolean (*XCDROAST_RegexCallback) (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error);

typedef struct
{
    GRegex *regex;
    XCDROAST_RegexCallback callback_func;
} XCDROAST_RegexRule;


static gboolean mirage_parser_xcdroast_callback_toc_comment (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *comment = g_match_info_fetch_named(match_info, "comment");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed COMMENT: %s\n", __debug__, comment);

    g_free(comment);

    return TRUE;
}


static gboolean mirage_parser_xcdroast_callback_toc_cdtitle (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->disc_info.cdtitle);
    self->priv->disc_info.cdtitle = g_match_info_fetch_named(match_info, "cdtitle");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD Title: %s\n", __debug__, self->priv->disc_info.cdtitle);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_cdsize (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *cdsize = g_match_info_fetch_named(match_info, "cdsize");

    self->priv->disc_info.cdsize = g_strtod(cdsize, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD Size: %d\n", __debug__, self->priv->disc_info.cdsize);

    g_free(cdsize);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_discid (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->disc_info.discid);
    self->priv->disc_info.discid = g_match_info_fetch_named(match_info, "discid");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Disc ID: %s\n", __debug__, self->priv->disc_info.discid);

    return TRUE;
}


static gboolean mirage_parser_xcdroast_callback_toc_track (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *track = g_match_info_fetch_named(match_info, "track");

    self->priv->toc_track.number = g_strtod(track, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Track: %d\n", __debug__, self->priv->toc_track.number);

    g_free(track);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_type (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *type = g_match_info_fetch_named(match_info, "type");

    self->priv->toc_track.type = g_strtod(type, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Type: %d\n", __debug__, self->priv->toc_track.type);

    g_free(type);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_size (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *size = g_match_info_fetch_named(match_info, "size");

    self->priv->toc_track.size  = g_strtod(size, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Size: %d\n", __debug__, self->priv->toc_track.size);

    g_free(size);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_startsec (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *startsec = g_match_info_fetch_named(match_info, "startsec");

    self->priv->toc_track.startsec  = g_strtod(startsec, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Start sector: %d\n", __debug__, self->priv->toc_track.startsec);

    g_free(startsec);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_file (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->toc_track.file);
    self->priv->toc_track.file = g_match_info_fetch_named(match_info, "file");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: File: %s\n", __debug__, self->priv->toc_track.file);

    /* Add track */
    return mirage_parser_xcdroast_add_track(self, &self->priv->toc_track, error);
}


static gboolean mirage_parser_xcdroast_callback_xinf_comment (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *comment = g_match_info_fetch_named(match_info, "comment");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed COMMENT: %s\n", __debug__, comment);

    g_free(comment);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_file (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.file);
    self->priv->xinf_track.file = g_match_info_fetch_named(match_info, "file");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: File: %s\n", __debug__, self->priv->xinf_track.file);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_track (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *track = g_match_info_fetch_named(match_info, "track");
    gchar *num_tracks = g_match_info_fetch_named(match_info, "num_tracks");

    self->priv->xinf_track.track = g_strtod(track, NULL);
    self->priv->xinf_track.num_tracks = g_strtod(num_tracks, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Track: %d / %d\n", __debug__, self->priv->xinf_track.track, self->priv->xinf_track.num_tracks);

    g_free(track);
    g_free(num_tracks);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_title (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.title);
    self->priv->xinf_track.title = g_match_info_fetch_named(match_info, "title");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Title: %s\n", __debug__, self->priv->xinf_track.title);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_artist (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.artist);
    self->priv->xinf_track.artist = g_match_info_fetch_named(match_info, "artist");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Artist: %s\n", __debug__, self->priv->xinf_track.artist);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_size (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *size = g_match_info_fetch_named(match_info, "size");

    self->priv->xinf_track.size  = g_strtod(size, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Size: %d\n", __debug__, self->priv->xinf_track.size);

    g_free(size);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_type (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *type = g_match_info_fetch_named(match_info, "type");

    self->priv->xinf_track.type  = g_strtod(type, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Type: %d\n", __debug__, self->priv->xinf_track.type);

    g_free(type);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_rec_type (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *rec_type = g_match_info_fetch_named(match_info, "rec_type");

    self->priv->xinf_track.rec_type  = g_strtod(rec_type, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Rec type: %d\n", __debug__, self->priv->xinf_track.rec_type);

    g_free(rec_type);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_preemp (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *preemp = g_match_info_fetch_named(match_info, "preemp");

    self->priv->xinf_track.preemp  = g_strtod(preemp, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Preemp: %d\n", __debug__, self->priv->xinf_track.preemp);

    g_free(preemp);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_copyperm (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *copyperm = g_match_info_fetch_named(match_info, "copyperm");

    self->priv->xinf_track.copyperm  = g_strtod(copyperm, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Copy perm: %d\n", __debug__, self->priv->xinf_track.copyperm);

    g_free(copyperm);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_stereo (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *stereo = g_match_info_fetch_named(match_info, "stereo");

    self->priv->xinf_track.stereo  = g_strtod(stereo, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Stereo: %d\n", __debug__, self->priv->xinf_track.stereo);

    g_free(stereo);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_cd_title (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.cd_title);
    self->priv->xinf_track.cd_title = g_match_info_fetch_named(match_info, "cd_title");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD Title: %s\n", __debug__, self->priv->xinf_track.cd_title);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_cd_artist (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.cd_artist);
    self->priv->xinf_track.cd_artist = g_match_info_fetch_named(match_info, "cd_artist");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD Artist: %s\n", __debug__, self->priv->xinf_track.cd_artist);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_cd_discid (MIRAGE_Parser_XCDROAST *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.cd_discid);
    self->priv->xinf_track.cd_discid = g_match_info_fetch_named(match_info, "cd_discid");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD Disc ID: %s\n", __debug__, self->priv->xinf_track.cd_discid);

    return TRUE;
}

#define APPEND_REGEX_RULE(list,rule,callback) { \
    XCDROAST_RegexRule *new_rule = g_new(XCDROAST_RegexRule, 1); \
    new_rule->regex = g_regex_new(rule, G_REGEX_OPTIMIZE, 0, NULL); \
    new_rule->callback_func = callback; \
    /* Append to the list */ \
    list = g_list_append(list, new_rule); \
}

static void mirage_parser_xcdroast_init_regex_parser (MIRAGE_Parser_XCDROAST *self)
{
    /* *** TOC parser *** */
    /* Ignore empty lines */
    APPEND_REGEX_RULE(self->priv->regex_rules_toc, "^[\\s]*$", NULL);

    /* Comment */
    APPEND_REGEX_RULE(self->priv->regex_rules_toc, "^#(?<comment>.*)$", mirage_parser_xcdroast_callback_toc_comment);
    /* Store pointer to comment's regex rule */
    GList *elem_comment = g_list_last(self->priv->regex_rules_toc);
    XCDROAST_RegexRule *rule_comment = elem_comment->data;
    self->priv->regex_comment_ptr = rule_comment->regex;

    APPEND_REGEX_RULE(self->priv->regex_rules_toc, "^\\s*cdtitle\\s*=\\s*\"(?<cdtitle>.*)\"\\s*$", mirage_parser_xcdroast_callback_toc_cdtitle);
    APPEND_REGEX_RULE(self->priv->regex_rules_toc, "^\\s*cdsize\\s*=\\s*(?<cdsize>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_toc_cdsize);
    APPEND_REGEX_RULE(self->priv->regex_rules_toc, "^\\s*discid\\s*=\\s*\"(?<discid>[\\w]+)\"\\s*$", mirage_parser_xcdroast_callback_toc_discid);

    APPEND_REGEX_RULE(self->priv->regex_rules_toc, "^\\s*track\\s*=\\s*(?<track>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_toc_track);
    APPEND_REGEX_RULE(self->priv->regex_rules_toc, "^\\s*type\\s*=\\s*(?<type>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_toc_type);
    APPEND_REGEX_RULE(self->priv->regex_rules_toc, "^\\s*size\\s*=\\s*(?<size>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_toc_size);
    APPEND_REGEX_RULE(self->priv->regex_rules_toc, "^\\s*startsec\\s*=\\s*(?<startsec>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_toc_startsec);
    APPEND_REGEX_RULE(self->priv->regex_rules_toc, "^\\s*file\\s*=\\s*\"(?<file>.+)\"\\s*$", mirage_parser_xcdroast_callback_toc_file);

    /* *** XINF parser ***/
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^#(?<comment>.*)$", mirage_parser_xcdroast_callback_xinf_comment);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*file\\s*=\\s*\"(?<file>.+)\"\\s*$", mirage_parser_xcdroast_callback_xinf_file);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*track\\s*=\\s*(?<track>[\\d]+)\\s+of\\s*(?<num_tracks>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_track);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*title\\s*=\\s*\"(?<title>.*)\"\\s*$", mirage_parser_xcdroast_callback_xinf_title);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*artist\\s*=\\s*\"(?<artist>.*)\"\\s*$", mirage_parser_xcdroast_callback_xinf_artist);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*size\\s*=\\s*(?<size>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_size);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*type\\s*=\\s*(?<type>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_type);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*rec_type\\s*=\\s*(?<rec_type>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_rec_type);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*preemp\\s*=\\s*(?<preemp>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_preemp);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*copyperm\\s*=\\s*(?<copyperm>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_copyperm);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*stereo\\s*=\\s*(?<stereo>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_stereo);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*cd_title\\s*=\\s*\"(?<cd_title>.*)\"\\s*$", mirage_parser_xcdroast_callback_xinf_cd_title);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*cd_artist\\s*=\\s*\"(?<cd_artist>.*)\"\\s*$", mirage_parser_xcdroast_callback_xinf_cd_artist);
    APPEND_REGEX_RULE(self->priv->regex_rules_xinf, "^\\s*cd_discid\\s*=\\s*\"(?<cd_discid>.*)\"\\s*$", mirage_parser_xcdroast_callback_xinf_cd_discid);

    return;
}

static void mirage_parser_xcdroast_cleanup_regex_parser (MIRAGE_Parser_XCDROAST *self)
{
    GList *entry;

    G_LIST_FOR_EACH(entry, self->priv->regex_rules_toc) {
        XCDROAST_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }

    g_list_free(self->priv->regex_rules_toc);

    G_LIST_FOR_EACH(entry, self->priv->regex_rules_xinf) {
        XCDROAST_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }

    g_list_free(self->priv->regex_rules_xinf);
}


static gboolean mirage_parser_xcdroast_parse_xinf_file (MIRAGE_Parser_XCDROAST *self, gchar *filename, GError **error)
{
    GError *io_error = NULL;
    GIOChannel *io_channel;
    gboolean succeeded = TRUE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: opening XINF file: %s\n", __debug__, filename);

    /* Create IO channel for file */
    io_channel = g_io_channel_new_file(filename, "r", &io_error);
    if (!io_channel) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create IO channel: %s\n", __debug__, io_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to create I/O channel on file '%s': %s", filename, io_error->message);
        g_error_free(io_error);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing XINF\n", __debug__);

    /* Read file line-by-line */
    gint line_nr;
    for (line_nr = 1; ; line_nr++) {
        GIOStatus status;
        gchar *line_str;
        gsize line_len;

        status = g_io_channel_read_line(io_channel, &line_str, &line_len, NULL, &io_error);

        /* Handle EOF */
        if (status == G_IO_STATUS_EOF) {
            break;
        }

        /* Handle abnormal status */
        if (status != G_IO_STATUS_NORMAL) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: status %d while reading line #%d from IO channel: %s\n", __debug__, status, line_nr, io_error ? io_error->message : "no error message");
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Status %d while reading line #%d from IO channel: %s", status, line_nr, io_error ? io_error->message : "no error message");
            g_error_free(io_error);
            succeeded = FALSE;
            break;
        }

        /* GRegex matching engine */
        GMatchInfo *match_info = NULL;
        gboolean matched = FALSE;
        GList *entry;

        /* Go over all matching rules */
        G_LIST_FOR_EACH(entry, self->priv->regex_rules_xinf) {
            XCDROAST_RegexRule *regex_rule = entry->data;

            /* Try to match the given rule */
            if (g_regex_match(regex_rule->regex, line_str, 0, &match_info)) {
                if (regex_rule->callback_func) {
                    succeeded = regex_rule->callback_func(self, match_info, error);
                }
                matched = TRUE;
            }

            /* Must be freed in any case */
            g_match_info_free(match_info);

            /* Break if we had a match */
            if (matched) {
                break;
            }
        }

        /* Complain if we failed to match the line (should it be fatal?) */
        if (!matched) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to match line #%d: %s\n", __debug__, line_nr, line_str);
            /* succeeded = FALSE */
        }

        g_free(line_str);

        /* In case callback didn't succeed... */
        if (!succeeded) {
            break;
        }
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: done parsing XINF\n\n", __debug__);

    g_io_channel_unref(io_channel);

    return succeeded;
}

static gboolean mirage_parser_xcdroast_parse_toc_file (MIRAGE_Parser_XCDROAST *self, gchar *filename, GError **error)
{
    GError *io_error = NULL;
    GIOChannel *io_channel;
    gboolean succeeded = TRUE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: opening TOC file: %s\n", __debug__, filename);

    /* Create IO channel for file */
    io_channel = g_io_channel_new_file(filename, "r", &io_error);
    if (!io_channel) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create IO channel: %s\n", __debug__, io_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to create I/O channel on file '%s': %s", filename, io_error->message);
        g_error_free(io_error);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing TOC\n", __debug__);

    /* Read file line-by-line */
    gint line_nr;
    for (line_nr = 1; ; line_nr++) {
        GIOStatus status;
        gchar *line_str;
        gsize line_len;

        status = g_io_channel_read_line(io_channel, &line_str, &line_len, NULL, &io_error);

        /* Handle EOF */
        if (status == G_IO_STATUS_EOF) {
            break;
        }

        /* Handle abnormal status */
        if (status != G_IO_STATUS_NORMAL) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: status %d while reading line #%d from IO channel: %s\n", __debug__, status, line_nr, io_error ? io_error->message : "no error message");
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Status %d while reading line #%d from IO channel: %s", status, line_nr, io_error ? io_error->message : "no error message");
            g_error_free(io_error);
            succeeded = FALSE;
            break;
        }

        /* GRegex matching engine */
        GMatchInfo *match_info = NULL;
        gboolean matched = FALSE;
        GList *entry;

        /* Go over all matching rules */
        G_LIST_FOR_EACH(entry, self->priv->regex_rules_toc) {
            XCDROAST_RegexRule *regex_rule = entry->data;

            /* Try to match the given rule */
            if (g_regex_match(regex_rule->regex, line_str, 0, &match_info)) {
                if (regex_rule->callback_func) {
                    succeeded = regex_rule->callback_func(self, match_info, error);
                }
                matched = TRUE;
            }

            /* Must be freed in any case */
            g_match_info_free(match_info);

            /* Break if we had a match */
            if (matched) {
                break;
            }
        }

        /* Complain if we failed to match the line (should it be fatal?) */
        if (!matched) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to match line #%d: %s\n", __debug__, line_nr, line_str);
            /* succeeded = FALSE */
        }

        g_free(line_str);

        /* In case callback didn't succeed... */
        if (!succeeded) {
            break;
        }
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing TOC\n", __debug__);

    g_io_channel_unref(io_channel);

    return succeeded;
}


static gboolean mirage_parser_xcdroast_check_toc_file (MIRAGE_Parser_XCDROAST *self, const gchar *filename)
{
    gboolean succeeded = FALSE;

    /* Check suffix - must be .toc */
    if (!mirage_helper_has_suffix(filename, ".toc")) {
        return FALSE;
    }

    /* *** Additional check ***
       Because cdrdao also uses .toc for its images, we need to make
       sure this one was created by X-CD-Roast... for that, we check for
       of comment containing "X-CD-Roast" */
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
        if (g_regex_match(self->priv->regex_comment_ptr, line_str, 0, &match_info)) {
            gchar *comment = g_match_info_fetch_named(match_info, "comment");

            /* Search for "X-CD-Roast" in the comment... */
            if (g_strrstr(comment, "X-CD-Roast")) {
                succeeded = TRUE;
            }

            g_free(comment);
        }
        /* Free match info */
        g_match_info_free(match_info);

        g_free(line_str);

        /* If we found the header, break the loop */
        if (succeeded) {
            break;
        }
    }

    g_io_channel_unref(io_channel);

    return succeeded;
}


/**********************************************************************\
 *                MIRAGE_Parser methods implementation                *
\**********************************************************************/
static GObject *mirage_parser_xcdroast_load_image (MIRAGE_Parser *_self, gchar **filenames, GError **error)
{
    MIRAGE_Parser_XCDROAST *self = MIRAGE_PARSER_XCDROAST(_self);

    gboolean succeeded = TRUE;

    /* Check if we can load file */
    if (!mirage_parser_xcdroast_check_toc_file(self, filenames[0])) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
        return FALSE;
    }

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc);

    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0]);
    self->priv->toc_filename = g_strdup(filenames[0]);

    /* Create session; note that we store only pointer, but release reference */
    mirage_disc_add_session_by_index(MIRAGE_DISC(self->priv->disc), -1, &self->priv->cur_session);
    g_object_unref(self->priv->cur_session);

    /* Parse the TOC */
    if (!mirage_parser_xcdroast_parse_toc_file(self, self->priv->toc_filename, error)) {
        succeeded = FALSE;
        goto end;
    }

    /* Verify layout length (this has to be done before Red Book pregap
       is added... */
    gint layout_length = mirage_disc_layout_get_length(MIRAGE_DISC(self->priv->disc));
    if (layout_length != self->priv->disc_info.cdsize) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: layout size mismatch! Declared %d sectors, actual layout size: %d\n", __debug__, self->priv->disc_info.cdsize, layout_length);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);
    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
    mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), medium_type);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);
    }

end:
    /* Return disc */
    mirage_object_detach_child(MIRAGE_OBJECT(self), self->priv->disc);
    if (succeeded) {
        return self->priv->disc;
    } else {
        g_object_unref(self->priv->disc);
        return NULL;
    }
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MIRAGE_Parser_XCDROAST, mirage_parser_xcdroast, MIRAGE_TYPE_PARSER);

void mirage_parser_xcdroast_type_register (GTypeModule *type_module)
{
    return mirage_parser_xcdroast_register_type(type_module);
}


static void mirage_parser_xcdroast_init (MIRAGE_Parser_XCDROAST *self)
{
    self->priv = MIRAGE_PARSER_XCDROAST_GET_PRIVATE(self);

    mirage_parser_generate_parser_info(MIRAGE_PARSER(self),
        "PARSER-XCDROAST",
        "X-CD-Roast Image Parser",
        "X-CD-Roast TOC files",
        "application/x-xcdroast"
    );

    mirage_parser_xcdroast_init_regex_parser(self);

    self->priv->toc_filename = NULL;

    self->priv->xinf_track.file = NULL;
    self->priv->xinf_track.title = NULL;
    self->priv->xinf_track.artist = NULL;
    self->priv->xinf_track.cd_title = NULL;
    self->priv->xinf_track.cd_artist = NULL;
    self->priv->xinf_track.cd_discid = NULL;
}

static void mirage_parser_xcdroast_finalize (GObject *gobject)
{
    MIRAGE_Parser_XCDROAST *self = MIRAGE_PARSER_XCDROAST(gobject);

    g_free(self->priv->toc_filename);

    /* Cleanup regex parser engine */
    mirage_parser_xcdroast_cleanup_regex_parser(self);

    /* Cleanup the parser structures */
    g_free(self->priv->toc_track.file);

    g_free(self->priv->xinf_track.file);
    g_free(self->priv->xinf_track.title);
    g_free(self->priv->xinf_track.artist);
    g_free(self->priv->xinf_track.cd_title);
    g_free(self->priv->xinf_track.cd_artist);
    g_free(self->priv->xinf_track.cd_discid);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_xcdroast_parent_class)->finalize(gobject);
}

static void mirage_parser_xcdroast_class_init (MIRAGE_Parser_XCDROASTClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MIRAGE_ParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->finalize = mirage_parser_xcdroast_finalize;

    parser_class->load_image = mirage_parser_xcdroast_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_XCDROASTPrivate));
}

static void mirage_parser_xcdroast_class_finalize (MIRAGE_Parser_XCDROASTClass *klass G_GNUC_UNUSED)
{
}
