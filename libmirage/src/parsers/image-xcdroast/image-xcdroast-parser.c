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
#define MIRAGE_PARSER_XCDROAST_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_XCDROAST, MirageParserXcdroastPrivate))

struct _MirageParserXcdroastPrivate
{
    MirageDisc *disc;

    const gchar *toc_filename;

    MirageSession *cur_session;

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
static gboolean mirage_parser_xcdroast_parse_xinf_file (MirageParserXcdroast *self, GInputStream *stream, GError **error);

static gchar *create_xinf_filename (gchar *track_filename)
{
    gchar *xinf_filename;
    gint baselen = strlen(track_filename);
    gint suffixlen = strlen(mirage_helper_get_suffix(track_filename));

    baselen -= suffixlen;

    xinf_filename = g_new(gchar, baselen + 6); /* baselen + . + xinf + \0 */
    g_strlcpy(xinf_filename, track_filename, baselen); /* Copy basename */
    g_snprintf(xinf_filename+baselen, 6, ".xinf");

    return xinf_filename;
}

static gboolean mirage_parser_xcdroast_add_track (MirageParserXcdroast *self, TOC_Track *track_info, GError **error)
{
    MirageTrack *track;
    MirageFragment *fragment;

    /* Add track */
    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    if (!mirage_session_add_track_by_number(self->priv->cur_session, track_info->number, track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
        g_object_unref(track);
        return FALSE;
    }

    /* Add pregap, if needed */
    if (self->priv->set_pregap) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding %d sector pregap\n", __debug__, self->priv->set_pregap);

        /* Create NULL fragment */
        fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

        mirage_fragment_set_length(fragment, self->priv->set_pregap);

        mirage_track_add_fragment(track, -1, fragment);
        mirage_track_set_track_start(track, self->priv->set_pregap);

        g_object_unref(fragment);

        self->priv->set_pregap = 0;
    }

    /* Determine data/audio file's full path */
    gchar *data_file = mirage_helper_find_data_file(track_info->file, self->priv->toc_filename);
    if (!data_file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: file '%s' not found!\n", __debug__, track_info->file);
        g_object_unref(track);
        return FALSE;
    }

    /* Open stream */
    GInputStream *data_stream = mirage_contextual_create_file_stream(MIRAGE_CONTEXTUAL(self), data_file, error);
    if (!data_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create stream on data file '%s'!\n", __debug__, data_file);
        g_free(data_file);
        return FALSE;
    }

    g_free(data_file);

    /* Setup basic track info, add fragment */
    switch (track_info->type) {
        case DATA: {
            /* Data (Mode 1) track */
            mirage_track_set_mode(track, MIRAGE_MODE_MODE1);

            /* Create and add binary fragment, using whole file */
            fragment = mirage_contextual_create_fragment(MIRAGE_CONTEXTUAL(self), MIRAGE_TYPE_FRAGMENT_IFACE_BINARY, data_stream, error);
            if (!fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __debug__);
                g_object_unref(data_stream);
                g_object_unref(track);
                return FALSE;
            }

            mirage_fragment_iface_binary_main_data_set_stream(MIRAGE_FRAGMENT_IFACE_BINARY(fragment), data_stream);
            mirage_fragment_iface_binary_main_data_set_size(MIRAGE_FRAGMENT_IFACE_BINARY(fragment), 2048);
            mirage_fragment_iface_binary_main_data_set_offset(MIRAGE_FRAGMENT_IFACE_BINARY(fragment), 0);
            mirage_fragment_iface_binary_main_data_set_format(MIRAGE_FRAGMENT_IFACE_BINARY(fragment), MIRAGE_MAIN_DATA);

            mirage_fragment_use_the_rest_of_file(fragment, NULL);

            mirage_track_add_fragment(track, -1, fragment);

            /* Verify fragment's length vs. declared track length */
            gint fragment_length = mirage_fragment_get_length(fragment);
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

            g_object_unref(fragment);

            break;
        }
        case AUDIO: {
            /* Audio track */
            mirage_track_set_mode(track, MIRAGE_MODE_AUDIO);

            /* Create and add audio fragment, using whole file */
            fragment = mirage_contextual_create_fragment(MIRAGE_CONTEXTUAL(self), MIRAGE_TYPE_FRAGMENT_IFACE_AUDIO, data_stream, error);
            if (!fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create AUDIO fragment!\n", __debug__);
                g_object_unref(data_stream);
                g_object_unref(track);
                return FALSE;
            }

            mirage_fragment_iface_audio_set_stream(MIRAGE_FRAGMENT_IFACE_AUDIO(fragment), data_stream);

            mirage_fragment_use_the_rest_of_file(fragment, NULL);

            mirage_track_add_fragment(track, -1, fragment);

            /* Verify fragment's length vs. declared track length */
            gint fragment_length = mirage_fragment_get_length(fragment);
            if (fragment_length != track_info->size) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: audio track size mismatch! Declared %d sectors, actual fragment size: %d\n", __debug__, track_info->size, fragment_length);
            }

            g_object_unref(fragment);

            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled track type %d!\n", __debug__, track_info->type);
            break;
        }
    }
    g_object_unref(data_stream);


    /* Try to get a XINF file and read additional info from it */
    gchar *xinf_filename = create_xinf_filename(track_info->file);
    gchar *real_xinf_filename = mirage_helper_find_data_file(xinf_filename, self->priv->toc_filename);

    GInputStream *xinf_stream = mirage_contextual_create_file_stream(MIRAGE_CONTEXTUAL(self), real_xinf_filename, error);

    if (xinf_stream) {
        if (mirage_parser_xcdroast_parse_xinf_file(self, xinf_stream, NULL)) {
            /* Track flags */
            gint flags = 0;
            if (self->priv->xinf_track.copyperm) flags |= MIRAGE_TRACK_FLAG_COPYPERMITTED;
            if (self->priv->xinf_track.preemp) flags |= MIRAGE_TRACK_FLAG_PREEMPHASIS;

            /* This is valid only for audio track (because data track in non-stereo
               by default */
            if ((TrackType) track_info->type == AUDIO && !self->priv->xinf_track.stereo) {
                flags |= MIRAGE_TRACK_FLAG_FOURCHANNEL;
            }

            mirage_track_set_flags(track, flags);
        }
        g_object_unref(xinf_stream);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create stream on XINF file '%s'!\n", __debug__, real_xinf_filename);
    }

    g_free(real_xinf_filename);
    g_free(xinf_filename);


    g_object_unref(track);

    return TRUE;
};


/**********************************************************************\
 *                       Regex parsing engine                         *
\**********************************************************************/
typedef gboolean (*XCDROAST_RegexCallback) (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error);

typedef struct {
    GRegex *regex;
    XCDROAST_RegexCallback callback_func;
} XCDROAST_RegexRule;


static gboolean mirage_parser_xcdroast_callback_toc_comment (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *comment = g_match_info_fetch_named(match_info, "comment");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed COMMENT: %s\n", __debug__, comment);

    g_free(comment);

    return TRUE;
}


static gboolean mirage_parser_xcdroast_callback_toc_cdtitle (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->disc_info.cdtitle);
    self->priv->disc_info.cdtitle = g_match_info_fetch_named(match_info, "cdtitle");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD Title: %s\n", __debug__, self->priv->disc_info.cdtitle);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_cdsize (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *cdsize = g_match_info_fetch_named(match_info, "cdsize");

    self->priv->disc_info.cdsize = g_strtod(cdsize, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD Size: %d\n", __debug__, self->priv->disc_info.cdsize);

    g_free(cdsize);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_discid (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->disc_info.discid);
    self->priv->disc_info.discid = g_match_info_fetch_named(match_info, "discid");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Disc ID: %s\n", __debug__, self->priv->disc_info.discid);

    return TRUE;
}


static gboolean mirage_parser_xcdroast_callback_toc_track (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *track = g_match_info_fetch_named(match_info, "track");

    self->priv->toc_track.number = g_strtod(track, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Track: %d\n", __debug__, self->priv->toc_track.number);

    g_free(track);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_type (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *type = g_match_info_fetch_named(match_info, "type");

    self->priv->toc_track.type = g_strtod(type, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Type: %d\n", __debug__, self->priv->toc_track.type);

    g_free(type);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_size (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *size = g_match_info_fetch_named(match_info, "size");

    self->priv->toc_track.size  = g_strtod(size, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Size: %d\n", __debug__, self->priv->toc_track.size);

    g_free(size);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_startsec (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *startsec = g_match_info_fetch_named(match_info, "startsec");

    self->priv->toc_track.startsec  = g_strtod(startsec, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Start sector: %d\n", __debug__, self->priv->toc_track.startsec);

    g_free(startsec);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_toc_file (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->toc_track.file);
    self->priv->toc_track.file = g_match_info_fetch_named(match_info, "file");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: File: %s\n", __debug__, self->priv->toc_track.file);

    /* Add track */
    return mirage_parser_xcdroast_add_track(self, &self->priv->toc_track, error);
}


static gboolean mirage_parser_xcdroast_callback_xinf_comment (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *comment = g_match_info_fetch_named(match_info, "comment");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed COMMENT: %s\n", __debug__, comment);

    g_free(comment);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_file (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.file);
    self->priv->xinf_track.file = g_match_info_fetch_named(match_info, "file");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: File: %s\n", __debug__, self->priv->xinf_track.file);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_track (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
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

static gboolean mirage_parser_xcdroast_callback_xinf_title (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.title);
    self->priv->xinf_track.title = g_match_info_fetch_named(match_info, "title");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Title: %s\n", __debug__, self->priv->xinf_track.title);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_artist (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.artist);
    self->priv->xinf_track.artist = g_match_info_fetch_named(match_info, "artist");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Artist: %s\n", __debug__, self->priv->xinf_track.artist);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_size (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *size = g_match_info_fetch_named(match_info, "size");

    self->priv->xinf_track.size  = g_strtod(size, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Size: %d\n", __debug__, self->priv->xinf_track.size);

    g_free(size);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_type (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *type = g_match_info_fetch_named(match_info, "type");

    self->priv->xinf_track.type  = g_strtod(type, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Type: %d\n", __debug__, self->priv->xinf_track.type);

    g_free(type);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_rec_type (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *rec_type = g_match_info_fetch_named(match_info, "rec_type");

    self->priv->xinf_track.rec_type  = g_strtod(rec_type, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Rec type: %d\n", __debug__, self->priv->xinf_track.rec_type);

    g_free(rec_type);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_preemp (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *preemp = g_match_info_fetch_named(match_info, "preemp");

    self->priv->xinf_track.preemp  = g_strtod(preemp, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Preemp: %d\n", __debug__, self->priv->xinf_track.preemp);

    g_free(preemp);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_copyperm (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *copyperm = g_match_info_fetch_named(match_info, "copyperm");

    self->priv->xinf_track.copyperm  = g_strtod(copyperm, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Copy perm: %d\n", __debug__, self->priv->xinf_track.copyperm);

    g_free(copyperm);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_stereo (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *stereo = g_match_info_fetch_named(match_info, "stereo");

    self->priv->xinf_track.stereo  = g_strtod(stereo, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Stereo: %d\n", __debug__, self->priv->xinf_track.stereo);

    g_free(stereo);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_cd_title (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.cd_title);
    self->priv->xinf_track.cd_title = g_match_info_fetch_named(match_info, "cd_title");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD Title: %s\n", __debug__, self->priv->xinf_track.cd_title);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_cd_artist (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.cd_artist);
    self->priv->xinf_track.cd_artist = g_match_info_fetch_named(match_info, "cd_artist");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD Artist: %s\n", __debug__, self->priv->xinf_track.cd_artist);

    return TRUE;
}

static gboolean mirage_parser_xcdroast_callback_xinf_cd_discid (MirageParserXcdroast *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    g_free(self->priv->xinf_track.cd_discid);
    self->priv->xinf_track.cd_discid = g_match_info_fetch_named(match_info, "cd_discid");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD Disc ID: %s\n", __debug__, self->priv->xinf_track.cd_discid);

    return TRUE;
}


static inline void append_regex_rule (GList **list_ptr, const gchar *rule, XCDROAST_RegexCallback callback)
{
    GList *list = *list_ptr;

    XCDROAST_RegexRule *new_rule = g_new(XCDROAST_RegexRule, 1);
    new_rule->regex = g_regex_new(rule, G_REGEX_OPTIMIZE, 0, NULL);
    new_rule->callback_func = callback;
    /* Append to the list */
    list = g_list_append(list, new_rule);

    *list_ptr = list;
}

static void mirage_parser_xcdroast_init_regex_parser (MirageParserXcdroast *self)
{
    /* *** TOC parser *** */
    /* Ignore empty lines */
    append_regex_rule(&self->priv->regex_rules_toc, "^[\\s]*$", NULL);

    /* Comment */
    append_regex_rule(&self->priv->regex_rules_toc, "^#(?<comment>.*)$", mirage_parser_xcdroast_callback_toc_comment);
    /* Store pointer to comment's regex rule */
    GList *elem_comment = g_list_last(self->priv->regex_rules_toc);
    XCDROAST_RegexRule *rule_comment = elem_comment->data;
    self->priv->regex_comment_ptr = rule_comment->regex;

    append_regex_rule(&self->priv->regex_rules_toc, "^\\s*cdtitle\\s*=\\s*\"(?<cdtitle>.*)\"\\s*$", mirage_parser_xcdroast_callback_toc_cdtitle);
    append_regex_rule(&self->priv->regex_rules_toc, "^\\s*cdsize\\s*=\\s*(?<cdsize>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_toc_cdsize);
    append_regex_rule(&self->priv->regex_rules_toc, "^\\s*discid\\s*=\\s*\"(?<discid>[\\w]+)\"\\s*$", mirage_parser_xcdroast_callback_toc_discid);

    append_regex_rule(&self->priv->regex_rules_toc, "^\\s*track\\s*=\\s*(?<track>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_toc_track);
    append_regex_rule(&self->priv->regex_rules_toc, "^\\s*type\\s*=\\s*(?<type>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_toc_type);
    append_regex_rule(&self->priv->regex_rules_toc, "^\\s*size\\s*=\\s*(?<size>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_toc_size);
    append_regex_rule(&self->priv->regex_rules_toc, "^\\s*startsec\\s*=\\s*(?<startsec>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_toc_startsec);
    append_regex_rule(&self->priv->regex_rules_toc, "^\\s*file\\s*=\\s*\"(?<file>.+)\"\\s*$", mirage_parser_xcdroast_callback_toc_file);

    /* *** XINF parser ***/
    append_regex_rule(&self->priv->regex_rules_xinf, "^#(?<comment>.*)$", mirage_parser_xcdroast_callback_xinf_comment);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*file\\s*=\\s*\"(?<file>.+)\"\\s*$", mirage_parser_xcdroast_callback_xinf_file);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*track\\s*=\\s*(?<track>[\\d]+)\\s+of\\s*(?<num_tracks>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_track);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*title\\s*=\\s*\"(?<title>.*)\"\\s*$", mirage_parser_xcdroast_callback_xinf_title);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*artist\\s*=\\s*\"(?<artist>.*)\"\\s*$", mirage_parser_xcdroast_callback_xinf_artist);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*size\\s*=\\s*(?<size>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_size);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*type\\s*=\\s*(?<type>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_type);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*rec_type\\s*=\\s*(?<rec_type>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_rec_type);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*preemp\\s*=\\s*(?<preemp>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_preemp);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*copyperm\\s*=\\s*(?<copyperm>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_copyperm);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*stereo\\s*=\\s*(?<stereo>[\\d]+)\\s*$", mirage_parser_xcdroast_callback_xinf_stereo);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*cd_title\\s*=\\s*\"(?<cd_title>.*)\"\\s*$", mirage_parser_xcdroast_callback_xinf_cd_title);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*cd_artist\\s*=\\s*\"(?<cd_artist>.*)\"\\s*$", mirage_parser_xcdroast_callback_xinf_cd_artist);
    append_regex_rule(&self->priv->regex_rules_xinf, "^\\s*cd_discid\\s*=\\s*\"(?<cd_discid>.*)\"\\s*$", mirage_parser_xcdroast_callback_xinf_cd_discid);

    return;
}

static void mirage_parser_xcdroast_cleanup_regex_parser (MirageParserXcdroast *self)
{
    for (GList *entry = self->priv->regex_rules_toc; entry; entry = entry->next) {
        XCDROAST_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }

    g_list_free(self->priv->regex_rules_toc);

    for (GList *entry = self->priv->regex_rules_xinf; entry; entry = entry->next) {
        XCDROAST_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }

    g_list_free(self->priv->regex_rules_xinf);
}


static gboolean mirage_parser_xcdroast_parse_xinf_file (MirageParserXcdroast *self, GInputStream *stream, GError **error)
{
    GDataInputStream *data_stream;
    gboolean succeeded = TRUE;

    /* Create GDataInputStream */
    data_stream = mirage_parser_create_text_stream(MIRAGE_PARSER(self), stream, error);
    if (!data_stream) {
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing XINF: %s\n", __debug__, mirage_contextual_get_file_stream_filename(MIRAGE_CONTEXTUAL(self), stream));

    /* Read file line-by-line */
    for (gint line_number = 1; ; line_number++) {
        GError *local_error = NULL;
        gchar *line_string;
        gsize line_length;

        /* Read line */
        line_string = g_data_input_stream_read_line_utf8(data_stream, &line_length, NULL, &local_error);

        /* Handle error */
        if (!line_string) {
            if (!local_error) {
                /* EOF */
                break;
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read line #%d: %s\n", __debug__, line_number, local_error->message);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read line #%d: %s!", line_number, local_error->message);
                g_error_free(local_error);
                succeeded = FALSE;
                break;
            }
        }

        /* GRegex matching engine */
        GMatchInfo *match_info = NULL;
        gboolean matched = FALSE;

        /* Go over all matching rules */
        for (GList *entry = self->priv->regex_rules_xinf; entry; entry = entry->next) {
            XCDROAST_RegexRule *regex_rule = entry->data;

            /* Try to match the given rule */
            if (g_regex_match(regex_rule->regex, line_string, 0, &match_info)) {
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
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to match line #%d: %s\n", __debug__, line_number, line_string);
            /* succeeded = FALSE */
        }

        g_free(line_string);

        /* In case callback didn't succeed... */
        if (!succeeded) {
            break;
        }
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: done parsing XINF\n\n", __debug__);

    g_object_unref(data_stream);

    return succeeded;
}

static gboolean mirage_parser_xcdroast_parse_toc_file (MirageParserXcdroast *self, GInputStream *stream, GError **error)
{
    GDataInputStream *data_stream;
    gboolean succeeded = TRUE;

    /* Create GDataInputStream */
    data_stream = mirage_parser_create_text_stream(MIRAGE_PARSER(self), stream, error);
    if (!data_stream) {
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing TOC: %s\n", __debug__, mirage_contextual_get_file_stream_filename(MIRAGE_CONTEXTUAL(self), stream));

    /* Read file line-by-line */
    for (gint line_number = 1; ; line_number++) {
        GError *local_error = NULL;
        gchar *line_string;
        gsize line_length;

        /* Read line */
        line_string = g_data_input_stream_read_line_utf8(data_stream, &line_length, NULL, &local_error);

        /* Handle error */
        if (!line_string) {
            if (!local_error) {
                /* EOF */
                break;
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read line #%d: %s\n", __debug__, line_number, local_error->message);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read line #%d: %s!", line_number, local_error->message);
                g_error_free(local_error);
                succeeded = FALSE;
                break;
            }
        }

        /* GRegex matching engine */
        GMatchInfo *match_info = NULL;
        gboolean matched = FALSE;

        /* Go over all matching rules */
        for (GList *entry = self->priv->regex_rules_toc; entry; entry = entry->next) {
            XCDROAST_RegexRule *regex_rule = entry->data;

            /* Try to match the given rule */
            if (g_regex_match(regex_rule->regex, line_string, 0, &match_info)) {
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
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to match line #%d: %s\n", __debug__, line_number, line_string);
            /* succeeded = FALSE */
        }

        g_free(line_string);

        /* In case callback didn't succeed... */
        if (!succeeded) {
            break;
        }
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing TOC\n", __debug__);

    g_object_unref(data_stream);

    return succeeded;
}


static gboolean mirage_parser_xcdroast_check_toc_file (MirageParserXcdroast *self, GInputStream *stream)
{
    gboolean succeeded = FALSE;
    GDataInputStream *data_stream;

    /* Check suffix - must be .toc */
    if (!mirage_helper_has_suffix(mirage_contextual_get_file_stream_filename(MIRAGE_CONTEXTUAL(self), stream), ".toc")) {
        return FALSE;
    }

    /* *** Additional check ***
       Because cdrdao also uses .toc for its images, we need to make
       sure this one was created by X-CD-Roast... for that, we check for
       of comment containing "X-CD-Roast" */
    data_stream = mirage_parser_create_text_stream(MIRAGE_PARSER(self), stream, NULL);
    if (!data_stream) {
        return FALSE;
    }

    /* Read file line-by-line */
    for (gint line_nr = 1; ; line_nr++) {
        GError *local_error = NULL;
        gchar *line_string;
        gsize line_length;

        GMatchInfo *match_info = NULL;

        /* Read line */
        line_string = g_data_input_stream_read_line_utf8(data_stream, &line_length, NULL, &local_error);

        /* Handle error */
        if (!line_string) {
            if (!local_error) {
                /* EOF */
                break;
            } else {
                break;
            }
        }

        /* Try to match the rule */
        if (g_regex_match(self->priv->regex_comment_ptr, line_string, 0, &match_info)) {
            gchar *comment = g_match_info_fetch_named(match_info, "comment");

            /* Search for "X-CD-Roast" in the comment... */
            if (g_strrstr(comment, "X-CD-Roast")) {
                succeeded = TRUE;
            }

            g_free(comment);
        }
        /* Free match info */
        g_match_info_free(match_info);

        g_free(line_string);

        /* If we found the header, break the loop */
        if (succeeded) {
            break;
        }
    }

    g_object_unref(data_stream);

    return succeeded;
}


/**********************************************************************\
 *                MirageParser methods implementation                *
\**********************************************************************/
static MirageDisc *mirage_parser_xcdroast_load_image (MirageParser *_self, GInputStream **streams, GError **error)
{
    MirageParserXcdroast *self = MIRAGE_PARSER_XCDROAST(_self);

    gboolean succeeded = TRUE;

    /* Check if we can load file */
    if (!mirage_parser_xcdroast_check_toc_file(self, streams[0])) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    self->priv->toc_filename = mirage_contextual_get_file_stream_filename(MIRAGE_CONTEXTUAL(self), streams[0]);
    mirage_disc_set_filename(self->priv->disc, self->priv->toc_filename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TOC filename: %s\n", __debug__, self->priv->toc_filename);

    /* Create session; note that we store only pointer, but release reference */
    self->priv->cur_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(self->priv->disc, -1, self->priv->cur_session);
    g_object_unref(self->priv->cur_session);

    /* Parse the TOC */
    succeeded = mirage_parser_xcdroast_parse_toc_file(self, streams[0], error);
    if (succeeded) {
        /* Verify layout length (this has to be done before Red Book pregap
           is added... */
        gint layout_length = mirage_disc_layout_get_length(self->priv->disc);
        if (layout_length != self->priv->disc_info.cdsize) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: layout size mismatch! Declared %d sectors, actual layout size: %d\n", __debug__, self->priv->disc_info.cdsize, layout_length);
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);
        /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
        gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
        mirage_disc_set_medium_type(self->priv->disc, medium_type);
        if (medium_type == MIRAGE_MEDIUM_CD) {
            mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);
        }
    }

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
G_DEFINE_DYNAMIC_TYPE(MirageParserXcdroast, mirage_parser_xcdroast, MIRAGE_TYPE_PARSER);

void mirage_parser_xcdroast_type_register (GTypeModule *type_module)
{
    return mirage_parser_xcdroast_register_type(type_module);
}


static void mirage_parser_xcdroast_init (MirageParserXcdroast *self)
{
    self->priv = MIRAGE_PARSER_XCDROAST_GET_PRIVATE(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-XCDROAST",
        "X-CD-Roast Image Parser",
        "X-CD-Roast TOC files",
        "application/x-xcdroast"
    );

    mirage_parser_xcdroast_init_regex_parser(self);

    self->priv->xinf_track.file = NULL;
    self->priv->xinf_track.title = NULL;
    self->priv->xinf_track.artist = NULL;
    self->priv->xinf_track.cd_title = NULL;
    self->priv->xinf_track.cd_artist = NULL;
    self->priv->xinf_track.cd_discid = NULL;
}

static void mirage_parser_xcdroast_finalize (GObject *gobject)
{
    MirageParserXcdroast *self = MIRAGE_PARSER_XCDROAST(gobject);

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

static void mirage_parser_xcdroast_class_init (MirageParserXcdroastClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->finalize = mirage_parser_xcdroast_finalize;

    parser_class->load_image = mirage_parser_xcdroast_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageParserXcdroastPrivate));
}

static void mirage_parser_xcdroast_class_finalize (MirageParserXcdroastClass *klass G_GNUC_UNUSED)
{
}
