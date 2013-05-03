/*
 *  libMirage: Apple Resource Fork routines.
 *  Copyright (C) 2013 Henrik Stokseth
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

#include <mirage.h>

#include "resource-fork.h"

/* Local typedefs */
typedef struct {
    /* XML parsing state */
    gboolean in_key;
    gboolean in_string;
    gboolean in_data;

    gint  nesting_level;
    gchar *last_key;

    /* Resource Fork */
    rsrc_fork_t *rsrc_fork;
} xml_user_data_t;

/* Helper functions */
static inline void rsrc_raw_fixup_header(rsrc_raw_header_t *rsrc_raw_header)
{
    g_assert(rsrc_raw_header);

    rsrc_raw_header->data_offset = GUINT32_FROM_BE(rsrc_raw_header->data_offset);
    rsrc_raw_header->map_offset  = GUINT32_FROM_BE(rsrc_raw_header->map_offset);
    rsrc_raw_header->data_length = GUINT32_FROM_BE(rsrc_raw_header->data_length);
    rsrc_raw_header->map_length  = GUINT32_FROM_BE(rsrc_raw_header->map_length);
}

static inline void rsrc_raw_fixup_map(rsrc_raw_map_t *rsrc_raw_map)
{
    g_assert(rsrc_raw_map);

    rsrc_raw_fixup_header(&rsrc_raw_map->header_copy);

    rsrc_raw_map->handle_next_map     = GUINT32_FROM_BE(rsrc_raw_map->handle_next_map);
    rsrc_raw_map->file_ref_num        = GUINT16_FROM_BE(rsrc_raw_map->file_ref_num);
    rsrc_raw_map->res_fork_attrs      = GUINT16_FROM_BE(rsrc_raw_map->res_fork_attrs);
    rsrc_raw_map->type_list_offset    = GUINT16_FROM_BE(rsrc_raw_map->type_list_offset);
    rsrc_raw_map->name_list_offset    = GUINT16_FROM_BE(rsrc_raw_map->name_list_offset);
    rsrc_raw_map->num_types_minus_one = GUINT16_FROM_BE(rsrc_raw_map->num_types_minus_one);
}

static inline void rsrc_raw_fixup_type(rsrc_raw_type_t *rsrc_raw_type)
{
    g_assert(rsrc_raw_type);

    rsrc_raw_type->num_refs_minus_one = GUINT16_FROM_BE(rsrc_raw_type->num_refs_minus_one);
    rsrc_raw_type->ref_offset         = GUINT16_FROM_BE(rsrc_raw_type->ref_offset);
}

static inline void rsrc_raw_fixup_ref(rsrc_raw_ref_t *rsrc_raw_ref)
{
    guint8 temp;

    g_assert(rsrc_raw_ref);

    rsrc_raw_ref->id          = GINT16_FROM_BE(rsrc_raw_ref->id);
    rsrc_raw_ref->name_offset = GINT16_FROM_BE(rsrc_raw_ref->name_offset);
    rsrc_raw_ref->handle      = GUINT32_FROM_BE(rsrc_raw_ref->handle);

    temp = rsrc_raw_ref->data_offset[0];
    rsrc_raw_ref->data_offset[0] = rsrc_raw_ref->data_offset[2];
    rsrc_raw_ref->data_offset[2] = temp;
}

static void xml_start_element (GMarkupParseContext *context G_GNUC_UNUSED, const gchar *element_name, const gchar **attribute_names G_GNUC_UNUSED,
                           const gchar **attribute_values G_GNUC_UNUSED, gpointer user_data, GError **error G_GNUC_UNUSED)
{
    xml_user_data_t *xml_user_data = (xml_user_data_t *) user_data;

    xml_user_data->nesting_level++;

    if (!strncmp(element_name, "key", strlen("key"))) {
        xml_user_data->in_key = TRUE;
    } else if (!strncmp(element_name, "string", strlen("string"))) {
        xml_user_data->in_string = TRUE;
    } else if (!strncmp(element_name, "data", strlen("data"))) {
        xml_user_data->in_data = TRUE;
    } else if (!strncmp(element_name, "dict", strlen("dict"))) {
        /* New resource reference */
        if (xml_user_data->nesting_level == 5) {
            rsrc_type_t *rsrc_type = NULL;
            rsrc_ref_t  *rsrc_ref = NULL;

            /* Make room for new ref and point to last one */
            g_assert(xml_user_data->rsrc_fork);
            rsrc_type = &xml_user_data->rsrc_fork->type_list[xml_user_data->rsrc_fork->num_types - 1];
            g_assert(rsrc_type);

            rsrc_type->num_refs++;
            rsrc_type->ref_list = g_renew(rsrc_ref_t, rsrc_type->ref_list,
                                          rsrc_type->num_refs);

            rsrc_ref = &rsrc_type->ref_list[rsrc_type->num_refs - 1];
            memset(rsrc_ref, 0, sizeof(rsrc_ref_t));
        }
    }
}

static void xml_end_element (GMarkupParseContext *context G_GNUC_UNUSED, const gchar *element_name, gpointer user_data, GError **error G_GNUC_UNUSED)
{
    xml_user_data_t *xml_user_data = (xml_user_data_t *) user_data;

    if (!strncmp(element_name, "key", strlen("key"))) {
        xml_user_data->in_key = FALSE;
    } else if (!strncmp(element_name, "string", strlen("string"))) {
        xml_user_data->in_string = FALSE;
    } else if (!strncmp(element_name, "data", strlen("data"))) {
        xml_user_data->in_data = FALSE;
    } else if (!strncmp(element_name, "dict", strlen("dict"))) {
        /* End of resource reference */
        if (xml_user_data->nesting_level == 5) {
            rsrc_type_t *rsrc_type = NULL;
            rsrc_ref_t  *rsrc_ref = NULL;

            /* Print out ref information */
            g_assert(xml_user_data->rsrc_fork);
            rsrc_type = &xml_user_data->rsrc_fork->type_list[xml_user_data->rsrc_fork->num_types - 1];
            g_assert(rsrc_type);
            rsrc_ref = &rsrc_type->ref_list[rsrc_type->num_refs - 1];
            g_assert(rsrc_ref);

            //g_message("Resource Type: %.4s ID: %i Name: %s", rsrc_type->type, rsrc_ref->id, rsrc_ref->name->str);
            //g_message(" Attrs: 0x%02x Data length: %u", rsrc_ref->attrs, rsrc_ref->data_length);
        }
    }

    xml_user_data->nesting_level--;
}

static GString *rsrc_data_strip_and_decode_base64(const gchar *text, gsize text_length)
{
    GString *dest_str = g_string_sized_new(text_length);

    if (!text || !dest_str) return NULL;

    /* Strip data string */
    for (gchar *source_pos = (gchar *) text; source_pos < text + text_length; source_pos++) {
        switch (*source_pos) {
            case '\n':
            case '\r':
            case '\t':
            case ' ':
                /* Discard CR, LF, TAB and whitespace */
                break;
            default:
                /* Save everything else */
                g_string_append_c(dest_str, *source_pos);
        }
    }

    /* Decode Base-64 string to resource data */
    g_base64_decode_inplace (dest_str->str, &dest_str->len);

    return dest_str;
}

static void xml_text (GMarkupParseContext *context G_GNUC_UNUSED, const gchar *text, gsize text_len, gpointer user_data, GError **error G_GNUC_UNUSED)
{
    xml_user_data_t *xml_user_data = (xml_user_data_t *) user_data;

    if (xml_user_data->in_key) {
        if (xml_user_data->last_key) g_free(xml_user_data->last_key);
        xml_user_data->last_key = g_strndup(text, text_len);
        g_assert(xml_user_data->last_key);

        /* New resource fork */
        if (xml_user_data->nesting_level == 3) {
            if (!strncmp(text, "resource-fork", strlen("resource-fork"))) {
                xml_user_data->rsrc_fork = g_new0(rsrc_fork_t, 1);
            } else {
                g_assert_not_reached();
            }
        }

        /* New resource type */
        if (xml_user_data->nesting_level == 4) {
            rsrc_type_t *rsrc_type = NULL;

            /* Make room for new type and point to last one */
            g_assert(xml_user_data->rsrc_fork);
            xml_user_data->rsrc_fork->num_types++;
            xml_user_data->rsrc_fork->type_list = g_renew(rsrc_type_t, xml_user_data->rsrc_fork->type_list,
                                                          xml_user_data->rsrc_fork->num_types);
            rsrc_type = &xml_user_data->rsrc_fork->type_list[xml_user_data->rsrc_fork->num_types - 1];
            memset(rsrc_type, 0, sizeof(rsrc_type_t));

            /* Fill in type data */
            memcpy(&rsrc_type->type, text, sizeof(rsrc_type->type));
            rsrc_type->num_refs = 0;
            rsrc_type->ref_list = NULL;
        }
    }

    /* Resource ref data */
    if (xml_user_data->in_string) {
        if (xml_user_data->nesting_level == 6) {
            rsrc_type_t *rsrc_type = &xml_user_data->rsrc_fork->type_list[xml_user_data->rsrc_fork->num_types - 1];
            rsrc_ref_t  *rsrc_ref = &rsrc_type->ref_list[rsrc_type->num_refs - 1];
            gchar       *last_key = xml_user_data->last_key;
            gint        res;

            g_assert(rsrc_type && rsrc_ref);

            if (!strncmp(last_key, "Attributes", strlen("Attributes"))) {
                res = sscanf(text, "%hhx", &rsrc_ref->attrs);
                g_assert(res >= 1);
            } else if (!strncmp(last_key, "ID", strlen("ID"))) {
                res = sscanf(text, "%hi", &rsrc_ref->id);
                g_assert(res >= 1);
            } else if (!strncmp(last_key, "Name", strlen("Name"))) {
                if (!rsrc_ref->name) {
                    rsrc_ref->name = g_string_new_len(text, text_len);
                }
            } else if (!strncmp(last_key, "CFName", strlen("CFName"))) {
                if (!rsrc_ref->name) {
                    rsrc_ref->name = g_string_new_len(text, text_len);
                }
            } else {
                g_assert_not_reached();
            }
        }
    }

    /* More resource ref data */
    if (xml_user_data->in_data) {
        if (xml_user_data->nesting_level == 6) {
            rsrc_type_t *rsrc_type = &xml_user_data->rsrc_fork->type_list[xml_user_data->rsrc_fork->num_types - 1];
            rsrc_ref_t  *rsrc_ref = &rsrc_type->ref_list[rsrc_type->num_refs - 1];
            GString     *dest_str = NULL;

            g_assert(rsrc_type && rsrc_ref);

            dest_str = rsrc_data_strip_and_decode_base64(text, text_len);
            g_assert(dest_str);

            /* Add the data to the ref */
            rsrc_ref->data_length = dest_str->len;
            if (dest_str->len > 0) {
                rsrc_ref->data = g_memdup(dest_str->str, dest_str->len);
                g_assert(rsrc_ref->data);
            } else {
                rsrc_ref->data = NULL;
            }

            g_string_free(dest_str, TRUE);
        }
    }
}

/* Public API */
rsrc_fork_t *rsrc_fork_read_xml(const gchar *xml_data, gssize xml_length)
{
    xml_user_data_t *xml_user_data = NULL;
    rsrc_fork_t     *rsrc_fork = NULL;

    const GMarkupParser res_fork_xml_parser = {
        xml_start_element,
        xml_end_element,
        xml_text,
        NULL,
        NULL
    };

    /* Sanity check */
    if (!xml_data || xml_length < 1) {
        return NULL;
    }

    xml_user_data = g_try_new0(xml_user_data_t, 1);
    if (!xml_user_data) return NULL;

    GMarkupParseContext *context = g_markup_parse_context_new (&res_fork_xml_parser, 0, xml_user_data, NULL);
    if (!context) return NULL;

    /* Parse the properties list */
    if (!g_markup_parse_context_parse (context, xml_data, xml_length, NULL)) {
        g_markup_parse_context_free(context);
        g_free(xml_user_data);
        return NULL;
    }

    rsrc_fork = xml_user_data->rsrc_fork;

    g_markup_parse_context_free(context);

    g_free(xml_user_data);

    return rsrc_fork;
}

rsrc_type_t *rsrc_find_type(rsrc_fork_t *rsrc_fork, const gchar *type)
{
    if (!rsrc_fork || !type) return NULL;

    for (guint t = 0; t < rsrc_fork->num_types; t++) {
        rsrc_type_t *rsrc_type = &rsrc_fork->type_list[t];

        if (!memcmp(rsrc_type->type, type, 4)) {
            return rsrc_type;
        }
    }
    
    return NULL;
}

rsrc_ref_t *rsrc_find_ref_by_type_and_id(rsrc_fork_t *rsrc_fork, const gchar *type, gint16 id)
{
    if (!rsrc_fork || !type) return NULL;

    rsrc_type_t *rsrc_type = rsrc_find_type(rsrc_fork, type);
    if (!rsrc_type) return NULL;

    for (guint r = 0; r < rsrc_type->num_refs; r++) {
        rsrc_ref_t *rsrc_ref = &rsrc_type->ref_list[r];

        if (rsrc_ref->id == id) {
            return rsrc_ref;
        }
    }

    return NULL;
}

rsrc_fork_t *rsrc_fork_read_binary(gchar *raw_data)
{
    rsrc_fork_t       *rsrc_fork = NULL;
    rsrc_raw_header_t *rsrc_raw_header = NULL;
    rsrc_raw_map_t    *rsrc_raw_map = NULL;

    /* Sanity check */
    if (!raw_data) {
        return NULL;
    }

    rsrc_fork = g_try_new0(rsrc_fork_t, 1);
    if (!rsrc_fork) return NULL;

    /* Read and fixup header */
    rsrc_raw_header = (rsrc_raw_header_t *) raw_data;
    rsrc_raw_fixup_header(rsrc_raw_header);

    /* Read and fixup resource map */
    rsrc_raw_map = (rsrc_raw_map_t *) (raw_data + rsrc_raw_header->map_offset);
    rsrc_raw_fixup_map(rsrc_raw_map);

    rsrc_fork->file_ref_num = rsrc_raw_map->file_ref_num;
    rsrc_fork->res_fork_attrs = rsrc_raw_map->res_fork_attrs;
    rsrc_fork->num_types = rsrc_raw_map->num_types_minus_one + 1;

    /* Do we have any types at all? */
    if (rsrc_fork->num_types) {
        rsrc_fork->type_list = g_try_new0(rsrc_type_t, rsrc_fork->num_types);
        if (!rsrc_fork->type_list) return NULL;
    } else {
        rsrc_fork->type_list = NULL;
        return rsrc_fork;
    }

    /* Loop through resource types */
    for (gint t = 0; t <= rsrc_raw_map->num_types_minus_one; t++) {
        rsrc_raw_type_t *rsrc_raw_type = (rsrc_raw_type_t *) (raw_data + rsrc_raw_header->map_offset +
                                         rsrc_raw_map->type_list_offset + 2 /* note: needed */ +
                                         sizeof(rsrc_raw_type_t) * t);
        rsrc_type_t     *type_list = rsrc_fork->type_list;

        rsrc_raw_fixup_type(rsrc_raw_type);

        type_list[t].type_as_int = rsrc_raw_type->type_as_int;
        type_list[t].num_refs = rsrc_raw_type->num_refs_minus_one + 1;

        /* Do we have any refs at all? */
        if (type_list[t].num_refs) {
            type_list[t].ref_list = g_try_new0(rsrc_ref_t, type_list[t].num_refs);
            if (!type_list[t].ref_list) return NULL;
        } else {
            type_list[t].ref_list = NULL;
            continue;
        }

        /* Loop through resource references */
        for (gint r = 0; r <= rsrc_raw_type->num_refs_minus_one; r++) {
            rsrc_raw_ref_t *rsrc_raw_ref = (rsrc_raw_ref_t *) (raw_data + rsrc_raw_header->map_offset +
                                           rsrc_raw_map->type_list_offset + rsrc_raw_type->ref_offset +
                                           sizeof(rsrc_raw_ref_t) * r);
            rsrc_ref_t     *ref_list = type_list[t].ref_list;

            rsrc_raw_fixup_ref(rsrc_raw_ref);

            ref_list[r].id = rsrc_raw_ref->id;
            ref_list[r].attrs = rsrc_raw_ref->attrs;

            if (rsrc_raw_ref->name_offset != -1) {
                gchar *rsrc_raw_name = (gchar *) (raw_data + rsrc_raw_header->map_offset +
                                       rsrc_raw_map->name_list_offset + rsrc_raw_ref->name_offset);

                ref_list[r].name = g_string_new_len(rsrc_raw_name + 1, *rsrc_raw_name);
            } else {
                ref_list[r].name = g_string_new("");
            }

            guint32 rsrc_data_offset = (rsrc_raw_ref->data_offset[2] << 16) +
                                       (rsrc_raw_ref->data_offset[1] << 8) +
                                       rsrc_raw_ref->data_offset[0];

            guint32 *rsrc_data_length = (guint32 *) (raw_data + rsrc_raw_header->data_offset + rsrc_data_offset);
            gchar   *rsrc_data_ptr = (gchar *) (rsrc_data_length + 1);

            *rsrc_data_length = GUINT32_FROM_BE(*rsrc_data_length);
            ref_list[r].data_length = *rsrc_data_length;

            //g_message("Resource Type: %.4s ID: %i Name: %s", type_list[t].type, ref_list[r].id, ref_list[r].name->str);
            //g_message(" Attrs: 0x%02x Data length: %u offset: 0x%x", ref_list[r].attrs, *rsrc_data_length, rsrc_data_offset);

            if (*rsrc_data_length > 0) {
                ref_list[r].data = g_memdup(rsrc_data_ptr, *rsrc_data_length);
                if (!ref_list[r].data) return NULL;
            } else {
                ref_list[r].data = NULL;
            }
        }
    }

    return rsrc_fork;
}

gboolean rsrc_fork_free(rsrc_fork_t *rsrc_fork)
{
    if (!rsrc_fork) return FALSE;

    for (guint t = 0; t < rsrc_fork->num_types; t++) {
        for (guint r = 0; r < rsrc_fork->type_list[t].num_refs; r++) {
            if (rsrc_fork->type_list[t].ref_list[r].data) {
                g_free(rsrc_fork->type_list[t].ref_list[r].data);
            }
            g_string_free(rsrc_fork->type_list[t].ref_list[r].name, TRUE);
        }
        if (rsrc_fork->type_list[t].ref_list) {
            g_free(rsrc_fork->type_list[t].ref_list);
        }
    }
    if (rsrc_fork->type_list) {
        g_free(rsrc_fork->type_list);
    }

    g_free(rsrc_fork);

    return TRUE;
}
