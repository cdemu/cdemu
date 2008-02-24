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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

/**
 * mirage_error_quark:
 *
 * <para>
 * Registers an error quark for libMirage if necessary.
 * </para>
 * 
 * Return value: The error quark used for libMirage errors.
 **/
GQuark mirage_error_quark (void) {
    static GQuark q = 0;
    
    if (q == 0) {
        q = g_quark_from_static_string("mirage-error-quark");
    }
    
    return q;
}

/**
 * mirage_error:
 * @errcode: error code
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets error code and error string for error @error code to #GError stored
 * in @error. If @error is already set, it is freed before being set. If @error
 * is %NULL, this function does nothing.
 * </para>
 **/
void mirage_error (gint errcode, GError **error) {
    struct {
        gint errcode;
        gchar *errstring;
    } errors[] = {
        /* General */
        { MIRAGE_E_INVALIDARG, "Invalid argument." },
        { MIRAGE_E_NOTIMPL, "Not implemented." },
        
        { MIRAGE_E_ITERCANCELLED, "Iteration cancelled." },
        
        { MIRAGE_E_INVALIDOBJTYPE, "Invalid object type." },

        { MIRAGE_E_DATANOTSET, "Requested data is not set." },
        { MIRAGE_E_DATAFIXED, "Data is fixed and cannot be altered." },

        { MIRAGE_E_NOTINLAYOUT, "Object is not part of a layout." },

        { MIRAGE_E_INDEXOUTOFRANGE, "Requested index is out of range." },
        { MIRAGE_E_SECTOROUTOFRANGE, "Requested sector is out of range." },
    
        /* Mirage */
        { MIRAGE_E_PLUGINDIR, "Failed to open plugins directory." },
        { MIRAGE_E_PLUGINNOTLOADED, "Plugin not loaded." },
        { MIRAGE_E_NOPARSERFOUND, "No parser can handle given image." },
        { MIRAGE_E_NOFRAGMENTFOUND, "No fragment can handle given data." },
        { MIRAGE_E_IMAGEFILE, "Image file cannot be opened or read." },
        
        /* Plugins */
        { MIRAGE_E_PARSER, "Parser error." },
        { MIRAGE_E_SINGLEFILE, "Parser supports only single-file images." },
        
        /* Object */
        { MIRAGE_E_NODEBUGCONTEXT, "Debug context is not set." },
        { MIRAGE_E_NOPARENT, "Parent object is not set." },
        { MIRAGE_E_NOMIRAGE, "Mirage object is not set." },
    
        /* Disc */    
        { MIRAGE_E_SESSIONNOTFOUND, "Session not found." },
        { MIRAGE_E_SESSIONEXISTS, "Session already exists." },
        { MIRAGE_E_INVALIDMEDIUM, "Invalid medium type." },
        
        /* Session */
        { MIRAGE_E_TRACKNOTFOUND, "Track not found." },
        { MIRAGE_E_LANGNOTFOUND, "Language not found." },
        { MIRAGE_E_TRACKEXISTS, "Track already exists." },
        { MIRAGE_E_LANGEXISTS, "Language already exists." },
        
        /* Track */
        { MIRAGE_E_INDEXNOTFOUND, "Index not found." },
        { MIRAGE_E_FRAGMENTNOTFOUND, "Fragment not found." },
        
        /* Fragment */
        { MIRAGE_E_FILENOTSET, "File is not set." },
        { MIRAGE_E_READFAILED, "Read has failed." },
        { MIRAGE_E_DATAFILE, "Data file cannot be opened or read." },
        
        /* Sector */
        { MIRAGE_E_SECTORTYPE, "Invalid sector type." },
        
        /* Language */
        { MIRAGE_E_INVALIDPACKTYPE, "Invalid pack type." },
        { MIRAGE_E_PACKNOTSET, "Pack of requested type is not set." },

        
        { MIRAGE_E_GENERIC, "Generic error." },
    };
    
    if (!error) {
        return;
    }
    
    if (*error) {
        g_error_free(*error);
        *error = NULL;
    }
    
    gint i;
    for (i = 0; i < G_N_ELEMENTS(errors); i++) {
        if (errors[i].errcode == errcode) {
            g_set_error(error, MIRAGE_ERROR, errors[i].errcode, errors[i].errstring);
            return;
        }
    }
    
    /* Generic error */
    g_set_error(error, MIRAGE_ERROR, errors[i-1].errcode, errors[i-1].errstring);
    
    return;
}
