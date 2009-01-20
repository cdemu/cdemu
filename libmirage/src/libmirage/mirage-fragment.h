/*
 *  libMirage: Fragment object and interfaces
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
 
#ifndef __MIRAGE_FRAGMENT_H__
#define __MIRAGE_FRAGMENT_H__


G_BEGIN_DECLS

/**
 * MIRAGE_FragmentInfo:
 * @id: fragment ID
 * @name: fragment name
 * @version: fragment version
 * @author: author name
 * @interface: interface fragment implements
 *
 * <para>
 * A structure containing fragment information. It can be obtained with call to
 * mirage_fragment_get_fragment_info().
 * </para>
 * 
 * <para>
 * @interface is a string contraining name of interface the fragment implements;
 * it is provided for debug and informational purposes.
 * </para>
 **/
typedef struct {
    gchar *id;
    gchar *name;
    gchar *version;
    gchar *author;
    
    gchar *interface;
} MIRAGE_FragmentInfo;

/******************************************************************************\
 *                             Base fragment class                            *
\******************************************************************************/
#define MIRAGE_TYPE_FRAGMENT            (mirage_fragment_get_type())
#define MIRAGE_FRAGMENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT, MIRAGE_Fragment))
#define MIRAGE_FRAGMENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FRAGMENT, MIRAGE_FragmentClass))
#define MIRAGE_IS_FRAGMENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT))
#define MIRAGE_IS_FRAGMENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FRAGMENT))
#define MIRAGE_FRAGMENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FRAGMENT, MIRAGE_FragmentClass))

/**
 * MIRAGE_Fragment:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
typedef struct {
    MIRAGE_Object parent;
} MIRAGE_Fragment;

typedef struct {
    MIRAGE_ObjectClass parent;
        
    /* Class members */    
    gboolean (*can_handle_data_format) (MIRAGE_Fragment *self, gchar *filename, GError **error);
    
    gboolean (*use_the_rest_of_file) (MIRAGE_Fragment *self, GError **error);
    
    gboolean (*read_main_data) (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error);
    gboolean (*read_subchannel_data) (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error);
} MIRAGE_FragmentClass;

/* Used by MIRAGE_TYPE_FRAGMENT */
GType mirage_fragment_get_type (void);


/* Public API */
void mirage_fragment_generate_fragment_info (MIRAGE_Fragment *self, gchar *id, gchar *name, gchar *version, gchar *author, gchar *interface);
gboolean mirage_fragment_get_fragment_info (MIRAGE_Fragment *self, MIRAGE_FragmentInfo **fragment_info, GError **error);

gboolean mirage_fragment_can_handle_data_format (MIRAGE_Fragment *self, gchar *filename, GError **error);

gboolean mirage_fragment_set_address (MIRAGE_Fragment *self, gint address, GError **error);
gboolean mirage_fragment_get_address (MIRAGE_Fragment *self, gint *address, GError **error);

gboolean mirage_fragment_set_length (MIRAGE_Fragment *self, gint length, GError **error);
gboolean mirage_fragment_get_length (MIRAGE_Fragment *self, gint *length, GError **error);

gboolean mirage_fragment_use_the_rest_of_file (MIRAGE_Fragment *self, GError **error);

gboolean mirage_fragment_read_main_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error);
gboolean mirage_fragment_read_subchannel_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error);


/******************************************************************************\
 *                           NULL fragment interface                          *
\******************************************************************************/
#define MIRAGE_TYPE_FINTERFACE_NULL            (mirage_finterface_null_get_type())
#define MIRAGE_FINTERFACE_NULL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FINTERFACE_NULL, MIRAGE_FInterface_NULL))
#define MIRAGE_IS_FINTERFACE_NULL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FINTERFACE_NULL))
#define MIRAGE_FINTERFACE_NULL_GET_CLASS(inst) (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_FINTERFACE_NULL, MIRAGE_FInterface_NULLClass))

/**
 * MIRAGE_FInterface_NULL:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MIRAGE_FInterface_NULL MIRAGE_FInterface_NULL;

typedef struct {
    GTypeInterface parent;
} MIRAGE_FInterface_NULLClass;

/* Used by MIRAGE_TYPE_FINTERFACE_NULL */
GType mirage_finterface_null_get_type (void);


/******************************************************************************\
 *                         BINARY fragment interface                          *
\******************************************************************************/
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
typedef enum {
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
typedef enum {
    FR_BIN_SFILE_INT = 0x01,
    FR_BIN_SFILE_EXT = 0x02,
    
    FR_BIN_SFILE_PW96_INT = 0x10,
    FR_BIN_SFILE_PW96_LIN = 0x20,
    FR_BIN_SFILE_RW96     = 0x40,
    FR_BIN_SFILE_PQ16     = 0x80,
} MIRAGE_BINARY_SubchannelFile_Format;

#define MIRAGE_TYPE_FINTERFACE_BINARY             (mirage_finterface_binary_get_type())
#define MIRAGE_FINTERFACE_BINARY(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FINTERFACE_BINARY, MIRAGE_FInterface_BINARY))
#define MIRAGE_IS_FINTERFACE_BINARY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FINTERFACE_BINARY))
#define MIRAGE_FINTERFACE_BINARY_GET_CLASS(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_FINTERFACE_BINARY, MIRAGE_FInterface_BINARYClass))

/**
 * MIRAGE_FInterface_BINARY:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MIRAGE_FInterface_BINARY MIRAGE_FInterface_BINARY;

typedef struct {
    GTypeInterface parent;
    
    /* Interface methods */
    gboolean (*track_file_set_handle) (MIRAGE_FInterface_BINARY *self, FILE *file, GError **error);
    gboolean (*track_file_get_handle) (MIRAGE_FInterface_BINARY *self, FILE **file, GError **error);
    gboolean (*track_file_set_offset) (MIRAGE_FInterface_BINARY *self, guint64 offset, GError **error);
    gboolean (*track_file_get_offset) (MIRAGE_FInterface_BINARY *self, guint64 *offset, GError **error);
    gboolean (*track_file_set_sectsize) (MIRAGE_FInterface_BINARY *self, gint sectsize, GError **error);
    gboolean (*track_file_get_sectsize) (MIRAGE_FInterface_BINARY *self, gint *sectsize, GError **error);
    gboolean (*track_file_set_format) (MIRAGE_FInterface_BINARY *self, gint format, GError **error);
    gboolean (*track_file_get_format) (MIRAGE_FInterface_BINARY *self, gint *format, GError **error);

    gboolean (*track_file_get_position) (MIRAGE_FInterface_BINARY *self, gint address, guint64 *position, GError **error);

    gboolean (*subchannel_file_set_handle) (MIRAGE_FInterface_BINARY *self, FILE *file, GError **error);
    gboolean (*subchannel_file_get_handle) (MIRAGE_FInterface_BINARY *self, FILE **file, GError **error);
    gboolean (*subchannel_file_set_offset) (MIRAGE_FInterface_BINARY *self, guint64 offset, GError **error);
    gboolean (*subchannel_file_get_offset) (MIRAGE_FInterface_BINARY *self, guint64 *offset, GError **error);
    gboolean (*subchannel_file_set_sectsize) (MIRAGE_FInterface_BINARY *self, gint sectsize, GError **error);
    gboolean (*subchannel_file_get_sectsize) (MIRAGE_FInterface_BINARY *self, gint *sectsize, GError **error);
    gboolean (*subchannel_file_set_format) (MIRAGE_FInterface_BINARY *self, gint format, GError **error);
    gboolean (*subchannel_file_get_format) (MIRAGE_FInterface_BINARY *self, gint *format, GError **error);

    gboolean (*subchannel_file_get_position) (MIRAGE_FInterface_BINARY *self, gint address, guint64 *position, GError **error);

} MIRAGE_FInterface_BINARYClass;

/* Used by MIRAGE_TYPE_FINTERFACE_BINARY */
GType mirage_finterface_binary_get_type (void);

/* Track file */
gboolean mirage_finterface_binary_track_file_set_handle (MIRAGE_FInterface_BINARY *self, FILE *file, GError **error);
gboolean mirage_finterface_binary_track_file_get_handle (MIRAGE_FInterface_BINARY *self, FILE **file, GError **error);
gboolean mirage_finterface_binary_track_file_set_offset (MIRAGE_FInterface_BINARY *self, guint64 offset, GError **error);
gboolean mirage_finterface_binary_track_file_get_offset (MIRAGE_FInterface_BINARY *self, guint64 *offset, GError **error);
gboolean mirage_finterface_binary_track_file_set_sectsize (MIRAGE_FInterface_BINARY *self, gint sectsize, GError **error);
gboolean mirage_finterface_binary_track_file_get_sectsize (MIRAGE_FInterface_BINARY *self, gint *sectsize, GError **error);
gboolean mirage_finterface_binary_track_file_set_format (MIRAGE_FInterface_BINARY *self, gint format, GError **error);
gboolean mirage_finterface_binary_track_file_get_format (MIRAGE_FInterface_BINARY *self, gint *format, GError **error);

gboolean mirage_finterface_binary_track_file_get_position (MIRAGE_FInterface_BINARY *self, gint address, guint64 *position, GError **error);

/* Subchannel file */
gboolean mirage_finterface_binary_subchannel_file_set_handle (MIRAGE_FInterface_BINARY *self, FILE *file, GError **error);
gboolean mirage_finterface_binary_subchannel_file_get_handle (MIRAGE_FInterface_BINARY *self, FILE **file, GError **error);
gboolean mirage_finterface_binary_subchannel_file_set_offset (MIRAGE_FInterface_BINARY *self, guint64 offset, GError **error);
gboolean mirage_finterface_binary_subchannel_file_get_offset (MIRAGE_FInterface_BINARY *self, guint64 *offset, GError **error);
gboolean mirage_finterface_binary_subchannel_file_set_sectsize (MIRAGE_FInterface_BINARY *self, gint sectsize, GError **error);
gboolean mirage_finterface_binary_subchannel_file_get_sectsize (MIRAGE_FInterface_BINARY *self, gint *sectsize, GError **error);
gboolean mirage_finterface_binary_subchannel_file_set_format (MIRAGE_FInterface_BINARY *self, gint format, GError **error);
gboolean mirage_finterface_binary_subchannel_file_get_format (MIRAGE_FInterface_BINARY *self, gint *format, GError **error);

gboolean mirage_finterface_binary_subchannel_file_get_position (MIRAGE_FInterface_BINARY *self, gint address, guint64 *position, GError **error);


/******************************************************************************\
 *                           AUDIO fragment interface                         *
\******************************************************************************/
#define MIRAGE_TYPE_FINTERFACE_AUDIO             (mirage_finterface_audio_get_type())
#define MIRAGE_FINTERFACE_AUDIO(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FINTERFACE_AUDIO, MIRAGE_FInterface_AUDIO))
#define MIRAGE_IS_FINTERFACE_AUDIO(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FINTERFACE_AUDIO))
#define MIRAGE_FINTERFACE_AUDIO_GET_CLASS(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_FINTERFACE_AUDIO, MIRAGE_FInterface_AUDIOClass))

/**
 * MIRAGE_FInterface_AUDIO:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MIRAGE_FInterface_AUDIO MIRAGE_FInterface_AUDIO;

typedef struct {
    GTypeInterface parent;
    
    /* Interface methods */
    gboolean (*set_file) (MIRAGE_FInterface_AUDIO *self, gchar *filename, GError **error);
    gboolean (*get_file) (MIRAGE_FInterface_AUDIO *self, gchar **filename, GError **error);
    gboolean (*set_offset) (MIRAGE_FInterface_AUDIO *self, gint offset, GError **error);
    gboolean (*get_offset) (MIRAGE_FInterface_AUDIO *self, gint *offset, GError **error);
} MIRAGE_FInterface_AUDIOClass;

/* Used by MIRAGE_TYPE_FINTERFACE_AUDIO */
GType mirage_finterface_audio_get_type (void);

gboolean mirage_finterface_audio_set_file (MIRAGE_FInterface_AUDIO *self, gchar *filename, GError **error);
gboolean mirage_finterface_audio_get_file (MIRAGE_FInterface_AUDIO *self, gchar **filename, GError **error);
gboolean mirage_finterface_audio_set_offset (MIRAGE_FInterface_AUDIO *self, gint offset, GError **error);
gboolean mirage_finterface_audio_get_offset (MIRAGE_FInterface_AUDIO *self, gint *offset, GError **error);

G_END_DECLS

#endif /* __MIRAGE_FRAGMENT_H__ */
