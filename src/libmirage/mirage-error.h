/*
 *  libMirage: Error handling
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
 
#ifndef __MIRAGE_ERROR_H__
#define __MIRAGE_ERROR_H__


G_BEGIN_DECLS

/**
 * MIRAGE_ERROR:
 *
 * <para>
 * Used to get the #GError quark for libMirage errors.
 * </para>
 **/
#define MIRAGE_ERROR mirage_error_quark()

GQuark mirage_error_quark (void);


/**
 * MIRAGE_ErrorCodes:
 * @MIRAGE_E_INVALIDARG: invalid argument
 * @MIRAGE_E_NOTIMPL: not implemented
 * @MIRAGE_E_ITERCANCELLED: iteration cancelled
 * @MIRAGE_E_INVALIDOBJTYPE: invalid object type
 * @MIRAGE_E_DATANOTSET: requested data is not set
 * @MIRAGE_E_DATAFIXED: data is fixed and cannot be altered
 * @MIRAGE_E_NOTINLAYOUT: object is not part of the layout
 * @MIRAGE_E_INDEXOUTOFRANGE: requested index is out of range
 * @MIRAGE_E_SECTOROUTOFRANGE: requested sector is out of range
 * @MIRAGE_E_NOTINIT: libMirage not initialized
 * @MIRAGE_E_PLUGINDIR: failed to open plugin directory
 * @MIRAGE_E_NOPARSERFOUND: no parser can handle given image
 * @MIRAGE_E_NOFRAGMENTFOUND: no parser can handle given data
 * @MIRAGE_E_IMAGEFILE: image file cannot be opened or read
 * @MIRAGE_E_PARSER: parser error
 * @MIRAGE_E_CANTHANDLE: parser cannot handle given image file(s)
 * @MIRAGE_E_NEEDPASSWORD: image is encrypted and requires password
 * @MIRAGE_E_WRONGPASSWORD: wrong password
 * @MIRAGE_E_NODEBUGCONTEXT: debug context is not set
 * @MIRAGE_E_NOPARENT: parent object is not set
 * @MIRAGE_E_SESSIONNOTFOUND: session not found
 * @MIRAGE_E_SESSIONEXISTS: session already exists
 * @MIRAGE_E_INVALIDMEDIUM: invalid medium type
 * @MIRAGE_E_TRACKNOTFOUND: track not found
 * @MIRAGE_E_LANGNOTFOUND: language not found
 * @MIRAGE_E_TRACKEXISTS: track already exists
 * @MIRAGE_E_LANGEXISTS: language already exists
 * @MIRAGE_E_INDEXNOTFOUND: index not found
 * @MIRAGE_E_FRAGMENTNOTFOUND: fragment not found
 * @MIRAGE_E_FILENOTSET: file is not set
 * @MIRAGE_E_READFAILED: read has failed
 * @MIRAGE_E_DATAFILE: data file cannot be opened or read
 * @MIRAGE_E_SECTORTYPE: invalid sector type
 * @MIRAGE_E_INVALIDPACKTYPE: invalid pack type
 * @MIRAGE_E_PACKNOTSET: pack of requested type is not set
 * @MIRAGE_E_GENERIC: generic libMirage error
 *
 * Error codes for libMirage library.
 **/
typedef enum {
    /* General */
    MIRAGE_E_INVALIDARG  = 0xDEAD000,
    MIRAGE_E_NOTIMPL,
    
    MIRAGE_E_ITERCANCELLED,
    
    MIRAGE_E_INVALIDOBJTYPE,

    MIRAGE_E_DATANOTSET,
    MIRAGE_E_DATAFIXED,

    MIRAGE_E_NOTINLAYOUT,

    MIRAGE_E_INDEXOUTOFRANGE,
    MIRAGE_E_SECTOROUTOFRANGE,
    
    /* Mirage */
    MIRAGE_E_NOTINIT,
    MIRAGE_E_PLUGINDIR,
    MIRAGE_E_NOPARSERFOUND,
    MIRAGE_E_NOFRAGMENTFOUND,
    MIRAGE_E_IMAGEFILE,
    
    /* Plugins */
    MIRAGE_E_PARSER,
    MIRAGE_E_CANTHANDLE,
    MIRAGE_E_NEEDPASSWORD,
    MIRAGE_E_WRONGPASSWORD,
    
    /* Object */
    MIRAGE_E_NODEBUGCONTEXT,
    MIRAGE_E_NOPARENT,

    /* Disc */    
    MIRAGE_E_SESSIONNOTFOUND,
    MIRAGE_E_SESSIONEXISTS,
    MIRAGE_E_INVALIDMEDIUM,
    
    /* Session */
    MIRAGE_E_TRACKNOTFOUND,
    MIRAGE_E_LANGNOTFOUND,
    MIRAGE_E_TRACKEXISTS,
    MIRAGE_E_LANGEXISTS,
    
    /* Track */
    MIRAGE_E_INDEXNOTFOUND,
    MIRAGE_E_FRAGMENTNOTFOUND,
    
    /* Fragment */
    MIRAGE_E_FILENOTSET,
    MIRAGE_E_READFAILED,
    MIRAGE_E_DATAFILE,
    
    /* Sector */
    MIRAGE_E_SECTORTYPE,
    
    /* Language */
    MIRAGE_E_INVALIDPACKTYPE,
    MIRAGE_E_PACKNOTSET,
    
    MIRAGE_E_GENERIC = 0xDEADFFF,
} MIRAGE_ErrorCodes;

/* Error function */
void mirage_error (gint errcode, GError **error);

G_END_DECLS

#endif /* __MIRAGE_ERROR_H__ */
