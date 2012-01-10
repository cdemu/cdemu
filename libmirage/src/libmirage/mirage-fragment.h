/*
 *  libMirage: Fragment object and interfaces
 *  Copyright (C) 2006-2012 Rok Mandeljc
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
 
#ifndef __MIRAGE_FRAGMENT_H__
#define __MIRAGE_FRAGMENT_H__


G_BEGIN_DECLS

/**
 * MIRAGE_FragmentInfo:
 * @id: fragment ID
 * @name: fragment name
 *
 * <para>
 * A structure containing fragment information. It can be obtained with call to
 * mirage_fragment_get_fragment_info().
 * </para>
 **/
typedef struct _MIRAGE_FragmentInfo MIRAGE_FragmentInfo;
struct _MIRAGE_FragmentInfo
{
    gchar *id;
    gchar *name;
};


/**********************************************************************\
 *                         Base fragment class                        *
\**********************************************************************/
#define MIRAGE_TYPE_FRAGMENT            (mirage_fragment_get_type())
#define MIRAGE_FRAGMENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT, MIRAGE_Fragment))
#define MIRAGE_FRAGMENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FRAGMENT, MIRAGE_FragmentClass))
#define MIRAGE_IS_FRAGMENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT))
#define MIRAGE_IS_FRAGMENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FRAGMENT))
#define MIRAGE_FRAGMENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FRAGMENT, MIRAGE_FragmentClass))

typedef struct _MIRAGE_Fragment         MIRAGE_Fragment;
typedef struct _MIRAGE_FragmentClass    MIRAGE_FragmentClass;
typedef struct _MIRAGE_FragmentPrivate  MIRAGE_FragmentPrivate;

/**
 * MIRAGE_Fragment:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MIRAGE_Fragment
{
    MIRAGE_Object parent_instance;

    /*< private >*/
    MIRAGE_FragmentPrivate *priv;
};

struct _MIRAGE_FragmentClass
{
    MIRAGE_ObjectClass parent_class;
        
    /* Class members */    
    gboolean (*can_handle_data_format) (MIRAGE_Fragment *self, const gchar *filename, GError **error);
    
    gboolean (*use_the_rest_of_file) (MIRAGE_Fragment *self, GError **error);
    
    gboolean (*read_main_data) (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error);
    gboolean (*read_subchannel_data) (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error);
};

/* Used by MIRAGE_TYPE_FRAGMENT */
GType mirage_fragment_get_type (void);


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
void mirage_fragment_generate_fragment_info (MIRAGE_Fragment *self, const gchar *id, const gchar *name);
gboolean mirage_fragment_get_fragment_info (MIRAGE_Fragment *self, const MIRAGE_FragmentInfo **fragment_info, GError **error);

gboolean mirage_fragment_can_handle_data_format (MIRAGE_Fragment *self, const gchar *filename, GError **error);

gboolean mirage_fragment_set_address (MIRAGE_Fragment *self, gint address, GError **error);
gboolean mirage_fragment_get_address (MIRAGE_Fragment *self, gint *address, GError **error);

gboolean mirage_fragment_set_length (MIRAGE_Fragment *self, gint length, GError **error);
gboolean mirage_fragment_get_length (MIRAGE_Fragment *self, gint *length, GError **error);

gboolean mirage_fragment_use_the_rest_of_file (MIRAGE_Fragment *self, GError **error);

gboolean mirage_fragment_read_main_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error);
gboolean mirage_fragment_read_subchannel_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error);


/**********************************************************************\
 *                       Null fragment interface                      *
\**********************************************************************/
#define MIRAGE_TYPE_FRAG_IFACE_NULL                (mirage_frag_iface_null_get_type())
#define MIRAGE_FRAG_IFACE_NULL(obj)                (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAG_IFACE_NULL, MIRAGE_FragIface_Null))
#define MIRAGE_IS_FRAG_IFACE_NULL(obj)             (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAG_IFACE_NULL))
#define MIRAGE_FRAG_IFACE_NULL_GET_INTERFACE(inst) (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_FRAG_IFACE_NULL, MIRAGE_FragIface_NullInterface))

/**
 * MIRAGE_FragIface_Null:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MIRAGE_FragIface_Null          MIRAGE_FragIface_Null;
typedef struct _MIRAGE_FragIface_NullInterface MIRAGE_FragIface_NullInterface;

struct _MIRAGE_FragIface_NullInterface
{
    GTypeInterface parent_iface;
};

/* Used by MIRAGE_TYPE_FRAG_IFACE_NULL */
GType mirage_frag_iface_null_get_type (void);


/**********************************************************************\
 *                     Binary fragment interface                      *
\**********************************************************************/
/**
 * MIRAGE_BINARY_TrackFile_Format:
 * @FR_BIN_TFILE_DATA: binary data
 * @FR_BIN_TFILE_AUDIO: audio data
 * @FR_BIN_TFILE_AUDIO_SWAP: audio data that needs to be swapped
 *
 * <para>
 * Track file data formats.
 * </para>
 **/
typedef enum
{
    FR_BIN_TFILE_DATA  = 0x01,
    FR_BIN_TFILE_AUDIO = 0x02,
    FR_BIN_TFILE_AUDIO_SWAP = 0x04,
} MIRAGE_BINARY_TrackFile_Format;

/**
 * MIRAGE_BINARY_SubchannelFile_Format:
 * @FR_BIN_SFILE_INT: internal subchannel (i.e. included in track file)
 * @FR_BIN_SFILE_EXT: external subchannel (i.e. provided by separate file)
 * @FR_BIN_SFILE_PW96_INT: P-W subchannel, 96 bytes, interleaved
 * @FR_BIN_SFILE_PW96_LIN: P-W subchannel, 96 bytes, linear
 * @FR_BIN_SFILE_RW96: R-W subchannel, 96 bytes, deinterleaved
 * @FR_BIN_SFILE_PQ16: PQ subchannel, 16 bytes
 *
 * <para>
 * Subchannel file data formats.
 * </para>
 **/
typedef enum
{
    FR_BIN_SFILE_INT = 0x01,
    FR_BIN_SFILE_EXT = 0x02,
    
    FR_BIN_SFILE_PW96_INT = 0x10,
    FR_BIN_SFILE_PW96_LIN = 0x20,
    FR_BIN_SFILE_RW96     = 0x40,
    FR_BIN_SFILE_PQ16     = 0x80,
} MIRAGE_BINARY_SubchannelFile_Format;

#define MIRAGE_TYPE_FRAG_IFACE_BINARY                 (mirage_frag_iface_binary_get_type())
#define MIRAGE_FRAG_IFACE_BINARY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAG_IFACE_BINARY, MIRAGE_FragIface_Binary))
#define MIRAGE_IS_FRAG_IFACE_BINARY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAG_IFACE_BINARY))
#define MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_FRAG_IFACE_BINARY, MIRAGE_FragIface_BinaryInterface))

/**
 * MIRAGE_FragIface_Binary:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MIRAGE_FragIface_Binary             MIRAGE_FragIface_Binary;
typedef struct _MIRAGE_FragIface_BinaryInterface    MIRAGE_FragIface_BinaryInterface;

struct _MIRAGE_FragIface_BinaryInterface
{
    GTypeInterface parent_iface;
    
    /* Interface methods */
    gboolean (*track_file_set_handle) (MIRAGE_FragIface_Binary *self, FILE *file, GError **error);
    gboolean (*track_file_get_handle) (MIRAGE_FragIface_Binary *self, FILE **file, GError **error);
    gboolean (*track_file_set_offset) (MIRAGE_FragIface_Binary *self, guint64 offset, GError **error);
    gboolean (*track_file_get_offset) (MIRAGE_FragIface_Binary *self, guint64 *offset, GError **error);
    gboolean (*track_file_set_sectsize) (MIRAGE_FragIface_Binary *self, gint sectsize, GError **error);
    gboolean (*track_file_get_sectsize) (MIRAGE_FragIface_Binary *self, gint *sectsize, GError **error);
    gboolean (*track_file_set_format) (MIRAGE_FragIface_Binary *self, gint format, GError **error);
    gboolean (*track_file_get_format) (MIRAGE_FragIface_Binary *self, gint *format, GError **error);

    gboolean (*track_file_get_position) (MIRAGE_FragIface_Binary *self, gint address, guint64 *position, GError **error);

    gboolean (*subchannel_file_set_handle) (MIRAGE_FragIface_Binary *self, FILE *file, GError **error);
    gboolean (*subchannel_file_get_handle) (MIRAGE_FragIface_Binary *self, FILE **file, GError **error);
    gboolean (*subchannel_file_set_offset) (MIRAGE_FragIface_Binary *self, guint64 offset, GError **error);
    gboolean (*subchannel_file_get_offset) (MIRAGE_FragIface_Binary *self, guint64 *offset, GError **error);
    gboolean (*subchannel_file_set_sectsize) (MIRAGE_FragIface_Binary *self, gint sectsize, GError **error);
    gboolean (*subchannel_file_get_sectsize) (MIRAGE_FragIface_Binary *self, gint *sectsize, GError **error);
    gboolean (*subchannel_file_set_format) (MIRAGE_FragIface_Binary *self, gint format, GError **error);
    gboolean (*subchannel_file_get_format) (MIRAGE_FragIface_Binary *self, gint *format, GError **error);

    gboolean (*subchannel_file_get_position) (MIRAGE_FragIface_Binary *self, gint address, guint64 *position, GError **error);

} MIRAGE_FragIface_BinaryClass;

/* Used by MIRAGE_TYPE_FRAG_IFACE_BINARY */
GType mirage_frag_iface_binary_get_type (void);

/* Track file */
gboolean mirage_frag_iface_binary_track_file_set_handle (MIRAGE_FragIface_Binary *self, FILE *file, GError **error);
gboolean mirage_frag_iface_binary_track_file_get_handle (MIRAGE_FragIface_Binary *self, FILE **file, GError **error);
gboolean mirage_frag_iface_binary_track_file_set_offset (MIRAGE_FragIface_Binary *self, guint64 offset, GError **error);
gboolean mirage_frag_iface_binary_track_file_get_offset (MIRAGE_FragIface_Binary *self, guint64 *offset, GError **error);
gboolean mirage_frag_iface_binary_track_file_set_sectsize (MIRAGE_FragIface_Binary *self, gint sectsize, GError **error);
gboolean mirage_frag_iface_binary_track_file_get_sectsize (MIRAGE_FragIface_Binary *self, gint *sectsize, GError **error);
gboolean mirage_frag_iface_binary_track_file_set_format (MIRAGE_FragIface_Binary *self, gint format, GError **error);
gboolean mirage_frag_iface_binary_track_file_get_format (MIRAGE_FragIface_Binary *self, gint *format, GError **error);

gboolean mirage_frag_iface_binary_track_file_get_position (MIRAGE_FragIface_Binary *self, gint address, guint64 *position, GError **error);

/* Subchannel file */
gboolean mirage_frag_iface_binary_subchannel_file_set_handle (MIRAGE_FragIface_Binary *self, FILE *file, GError **error);
gboolean mirage_frag_iface_binary_subchannel_file_get_handle (MIRAGE_FragIface_Binary *self, FILE **file, GError **error);
gboolean mirage_frag_iface_binary_subchannel_file_set_offset (MIRAGE_FragIface_Binary *self, guint64 offset, GError **error);
gboolean mirage_frag_iface_binary_subchannel_file_get_offset (MIRAGE_FragIface_Binary *self, guint64 *offset, GError **error);
gboolean mirage_frag_iface_binary_subchannel_file_set_sectsize (MIRAGE_FragIface_Binary *self, gint sectsize, GError **error);
gboolean mirage_frag_iface_binary_subchannel_file_get_sectsize (MIRAGE_FragIface_Binary *self, gint *sectsize, GError **error);
gboolean mirage_frag_iface_binary_subchannel_file_set_format (MIRAGE_FragIface_Binary *self, gint format, GError **error);
gboolean mirage_frag_iface_binary_subchannel_file_get_format (MIRAGE_FragIface_Binary *self, gint *format, GError **error);

gboolean mirage_frag_iface_binary_subchannel_file_get_position (MIRAGE_FragIface_Binary *self, gint address, guint64 *position, GError **error);


/**********************************************************************\
 *                       Audio fragment interface                     *
\**********************************************************************/
#define MIRAGE_TYPE_FRAG_IFACE_AUDIO             (mirage_frag_iface_audio_get_type())
#define MIRAGE_FRAG_IFACE_AUDIO(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAG_IFACE_AUDIO, MIRAGE_FragIface_Audio))
#define MIRAGE_IS_FRAG_IFACE_AUDIO(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAG_IFACE_AUDIO))
#define MIRAGE_FRAG_IFACE_AUDIO_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_FRAG_IFACE_AUDIO, MIRAGE_FragIface_AudioInterface))

/**
 * MIRAGE_FragIface_Audio:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MIRAGE_FragIface_Audio          MIRAGE_FragIface_Audio;
typedef struct _MIRAGE_FragIface_AudioInterface MIRAGE_FragIface_AudioInterface;

struct _MIRAGE_FragIface_AudioInterface
{
    GTypeInterface parent_iface;
    
    /* Interface methods */
    gboolean (*set_file) (MIRAGE_FragIface_Audio *self, const gchar *filename, GError **error);
    gboolean (*get_file) (MIRAGE_FragIface_Audio *self, const gchar **filename, GError **error);
    gboolean (*set_offset) (MIRAGE_FragIface_Audio *self, gint offset, GError **error);
    gboolean (*get_offset) (MIRAGE_FragIface_Audio *self, gint *offset, GError **error);
};

/* Used by MIRAGE_TYPE_FRAG_IFACE_AUDIO */
GType mirage_frag_iface_audio_get_type (void);

gboolean mirage_frag_iface_audio_set_file (MIRAGE_FragIface_Audio *self, const gchar *filename, GError **error);
gboolean mirage_frag_iface_audio_get_file (MIRAGE_FragIface_Audio *self, const gchar **filename, GError **error);
gboolean mirage_frag_iface_audio_set_offset (MIRAGE_FragIface_Audio *self, gint offset, GError **error);
gboolean mirage_frag_iface_audio_get_offset (MIRAGE_FragIface_Audio *self, gint *offset, GError **error);

G_END_DECLS

#endif /* __MIRAGE_FRAGMENT_H__ */
