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
 * MirageFragmentInfo:
 * @id: fragment ID
 * @name: fragment name
 *
 * <para>
 * A structure containing fragment information. It can be obtained with call to
 * mirage_fragment_get_fragment_info().
 * </para>
 **/
typedef struct _MirageFragmentInfo MirageFragmentInfo;
struct _MirageFragmentInfo
{
    gchar *id;
    gchar *name;
};


/**********************************************************************\
 *                         Base fragment class                        *
\**********************************************************************/
#define MIRAGE_TYPE_FRAGMENT            (mirage_fragment_get_type())
#define MIRAGE_FRAGMENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT, MirageFragment))
#define MIRAGE_FRAGMENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FRAGMENT, MirageFragmentClass))
#define MIRAGE_IS_FRAGMENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT))
#define MIRAGE_IS_FRAGMENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FRAGMENT))
#define MIRAGE_FRAGMENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FRAGMENT, MirageFragmentClass))

typedef struct _MirageFragment         MirageFragment;
typedef struct _MirageFragmentClass    MirageFragmentClass;
typedef struct _MirageFragmentPrivate  MirageFragmentPrivate;

/**
 * MirageFragment:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MirageFragment
{
    MirageObject parent_instance;

    /*< private >*/
    MirageFragmentPrivate *priv;
};

struct _MirageFragmentClass
{
    MirageObjectClass parent_class;

    /* Class members */
    gboolean (*can_handle_data_format) (MirageFragment *self, GObject *stream, GError **error);

    gboolean (*use_the_rest_of_file) (MirageFragment *self, GError **error);

    gboolean (*read_main_data) (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error);
    gboolean (*read_subchannel_data) (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error);
};

/* Used by MIRAGE_TYPE_FRAGMENT */
GType mirage_fragment_get_type (void);


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
void mirage_fragment_generate_fragment_info (MirageFragment *self, const gchar *id, const gchar *name);
const MirageFragmentInfo *mirage_fragment_get_fragment_info (MirageFragment *self);

gboolean mirage_fragment_can_handle_data_format (MirageFragment *self, GObject *stream, GError **error);

void mirage_fragment_set_address (MirageFragment *self, gint address);
gint mirage_fragment_get_address (MirageFragment *self);

void mirage_fragment_set_length (MirageFragment *self, gint length);
gint mirage_fragment_get_length (MirageFragment *self);

gboolean mirage_fragment_use_the_rest_of_file (MirageFragment *self, GError **error);

gboolean mirage_fragment_read_main_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error);
gboolean mirage_fragment_read_subchannel_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error);


/**********************************************************************\
 *                       Null fragment interface                      *
\**********************************************************************/
#define MIRAGE_TYPE_FRAGMENT_IFACE_NULL                (mirage_fragment_iface_null_get_type())
#define MIRAGE_FRAGMENT_IFACE_NULL(obj)                (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT_IFACE_NULL, MIRAGE_FragmentIface_Null))
#define MIRAGE_IS_FRAGMENT_IFACE_NULL(obj)             (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT_IFACE_NULL))
#define MIRAGE_FRAGMENT_IFACE_NULL_GET_INTERFACE(inst) (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_FRAGMENT_IFACE_NULL, MIRAGE_FragmentIface_NullInterface))

/**
 * MirageFragmentIfaceNull:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MirageFragmentIfaceNull          MirageFragmentIfaceNull;
typedef struct _MirageFragmentIfaceNullInterface MirageFragmentIfaceNullInterface;

struct _MirageFragmentIfaceNullInterface
{
    GTypeInterface parent_iface;
};

/* Used by MIRAGE_TYPE_FRAGMENT_IFACE_NULL */
GType mirage_fragment_iface_null_get_type (void);


/**********************************************************************\
 *                     Binary fragment interface                      *
\**********************************************************************/
/**
 * MirageMainDataFormat:
 * @MIRAGE_MAIN_DATA: binary data
 * @MIRAGE_MAIN_AUDIO: audio data
 * @MIRAGE_MAIN_AUDIO_SWAP: audio data that needs to be swapped
 *
 * <para>
 * Track file data formats.
 * </para>
 **/
typedef enum
{
    MIRAGE_MAIN_DATA  = 0x01,
    MIRAGE_MAIN_AUDIO = 0x02,
    MIRAGE_MAIN_AUDIO_SWAP = 0x04,
} MirageMainDataFormat;

/**
 * MirageSubchannelDataFormat:
 * @MIRAGE_SUBCHANNEL_INT: internal subchannel (i.e. included in track file)
 * @MIRAGE_SUBCHANNEL_EXT: external subchannel (i.e. provided by separate file)
 * @MIRAGE_SUBCHANNEL_PW96_INT: P-W subchannel, 96 bytes, interleaved
 * @MIRAGE_SUBCHANNEL_PW96_LIN: P-W subchannel, 96 bytes, linear
 * @MIRAGE_SUBCHANNEL_RW96: R-W subchannel, 96 bytes, deinterleaved
 * @MIRAGE_SUBCHANNEL_PQ16: PQ subchannel, 16 bytes
 *
 * <para>
 * Subchannel file data formats.
 * </para>
 **/
typedef enum
{
    MIRAGE_SUBCHANNEL_INT = 0x01,
    MIRAGE_SUBCHANNEL_EXT = 0x02,

    MIRAGE_SUBCHANNEL_PW96_INT = 0x10,
    MIRAGE_SUBCHANNEL_PW96_LIN = 0x20,
    MIRAGE_SUBCHANNEL_RW96     = 0x40,
    MIRAGE_SUBCHANNEL_PQ16     = 0x80,
} MirageSubchannelDataFormat;

#define MIRAGE_TYPE_FRAGMENT_IFACE_BINARY                 (mirage_fragment_iface_binary_get_type())
#define MIRAGE_FRAGMENT_IFACE_BINARY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT_IFACE_BINARY, MirageFragmentIfaceBinary))
#define MIRAGE_IS_FRAGMENT_IFACE_BINARY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT_IFACE_BINARY))
#define MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_FRAGMENT_IFACE_BINARY, MirageFragmentIfaceBinaryInterface))

/**
 * MirageFragmentIfaceBinary:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MirageFragmentIfaceBinary             MirageFragmentIfaceBinary;
typedef struct _MirageFragmentIfaceBinaryInterface    MirageFragmentIfaceBinaryInterface;

struct _MirageFragmentIfaceBinaryInterface
{
    GTypeInterface parent_iface;

    /* Interface methods */
    gboolean (*main_data_set_stream) (MirageFragmentIfaceBinary *self, GObject *stream, GError **error);
    const gchar *(*main_data_get_filename) (MirageFragmentIfaceBinary *self);
    void (*main_data_set_offset) (MirageFragmentIfaceBinary *self, guint64 offset);
    guint64 (*main_data_get_offset) (MirageFragmentIfaceBinary *self);
    void (*main_data_set_size) (MirageFragmentIfaceBinary *self, gint sectsize);
    gint (*main_data_get_size) (MirageFragmentIfaceBinary *self);
    void (*main_data_set_format) (MirageFragmentIfaceBinary *self, gint format);
    gint (*main_data_get_format) (MirageFragmentIfaceBinary *self);

    guint64 (*main_data_get_position) (MirageFragmentIfaceBinary *self, gint address);

    gboolean (*subchannel_data_set_stream) (MirageFragmentIfaceBinary *self, GObject *stream, GError **error);
    const gchar *(*subchannel_data_get_filename) (MirageFragmentIfaceBinary *self);
    void (*subchannel_data_set_offset) (MirageFragmentIfaceBinary *self, guint64 offset);
    guint64 (*subchannel_data_get_offset) (MirageFragmentIfaceBinary *self);
    void (*subchannel_data_set_size) (MirageFragmentIfaceBinary *self, gint sectsize);
    gint (*subchannel_data_get_size) (MirageFragmentIfaceBinary *self);
    void (*subchannel_data_set_format) (MirageFragmentIfaceBinary *self, gint format);
    gint (*subchannel_data_get_format) (MirageFragmentIfaceBinary *self);

    guint64 (*subchannel_data_get_position) (MirageFragmentIfaceBinary *self, gint address);

};

/* Used by MIRAGE_TYPE_FRAGMENT_IFACE_BINARY */
GType mirage_fragment_iface_binary_get_type (void);

/* Track file */
gboolean mirage_fragment_iface_binary_main_data_set_stream (MirageFragmentIfaceBinary *self, GObject *stream, GError **error);
const gchar *mirage_fragment_iface_binary_main_data_get_filename (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_main_data_set_offset (MirageFragmentIfaceBinary *self, guint64 offset);
guint64 mirage_fragment_iface_binary_main_data_get_offset (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_main_data_set_size (MirageFragmentIfaceBinary *self, gint sectsize);
gint mirage_fragment_iface_binary_main_data_get_size (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_main_data_set_format (MirageFragmentIfaceBinary *self, gint format);
gint mirage_fragment_iface_binary_main_data_get_format (MirageFragmentIfaceBinary *self);

guint64 mirage_fragment_iface_binary_main_data_get_position (MirageFragmentIfaceBinary *self, gint address);

/* Subchannel file */
gboolean mirage_fragment_iface_binary_subchannel_data_set_stream (MirageFragmentIfaceBinary *self, GObject *stream, GError **error);
const gchar *mirage_fragment_iface_binary_subchannel_data_get_filename (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_subchannel_data_set_offset (MirageFragmentIfaceBinary *self, guint64 offset);
guint64 mirage_fragment_iface_binary_subchannel_data_get_offset (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_subchannel_data_set_size (MirageFragmentIfaceBinary *self, gint sectsize);
gint mirage_fragment_iface_binary_subchannel_data_get_size (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_subchannel_data_set_format (MirageFragmentIfaceBinary *self, gint format);
gint mirage_fragment_iface_binary_subchannel_data_get_format (MirageFragmentIfaceBinary *self);

guint64 mirage_fragment_iface_binary_subchannel_data_get_position (MirageFragmentIfaceBinary *self, gint address);


/**********************************************************************\
 *                       Audio fragment interface                     *
\**********************************************************************/
#define MIRAGE_TYPE_FRAGMENT_IFACE_AUDIO             (mirage_fragment_iface_audio_get_type())
#define MIRAGE_FRAGMENT_IFACE_AUDIO(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT_IFACE_AUDIO, MirageFragmentIfaceAudio))
#define MIRAGE_IS_FRAGMENT_IFACE_AUDIO(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT_IFACE_AUDIO))
#define MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_FRAGMENT_IFACE_AUDIO, MirageFragmentIfaceAudioInterface))

/**
 * MirageFragmentIfaceAudio:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MirageFragmentIfaceAudio          MirageFragmentIfaceAudio;
typedef struct _MirageFragmentIfaceAudioInterface MirageFragmentIfaceAudioInterface;

struct _MirageFragmentIfaceAudioInterface
{
    GTypeInterface parent_iface;

    /* Interface methods */
    gboolean (*set_stream) (MirageFragmentIfaceAudio *self, GObject *stream, GError **error);
    const gchar *(*get_filename) (MirageFragmentIfaceAudio *self);
    void (*set_offset) (MirageFragmentIfaceAudio *self, gint offset);
    gint (*get_offset) (MirageFragmentIfaceAudio *self);
};

/* Used by MIRAGE_TYPE_FRAGMENT_IFACE_AUDIO */
GType mirage_fragment_iface_audio_get_type (void);

gboolean mirage_fragment_iface_audio_set_stream (MirageFragmentIfaceAudio *self, GObject *stream, GError **error);
const gchar *mirage_fragment_iface_audio_get_filename (MirageFragmentIfaceAudio *self);
void mirage_fragment_iface_audio_set_offset (MirageFragmentIfaceAudio *self, gint offset);
gint mirage_fragment_iface_audio_get_offset (MirageFragmentIfaceAudio *self);

G_END_DECLS

#endif /* __MIRAGE_FRAGMENT_H__ */
