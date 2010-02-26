/*
 *  CDEmuD: error handling
 *  Copyright (C) 2006-2010 Rok Mandeljc
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

#ifndef __CDEMUD_ERROR_H__
#define __CDEMUD_ERROR_H__

#define CDEMUD_ERROR cdemud_error_quark()

/* Error codes */
enum {
    CDEMUD_E_OBJNOTINIT = 0xDEAD000,
    
    CDEMUD_E_INVALIDARG,
    
    CDEMUD_E_NODRIVER,
    CDEMUD_E_NODEVICES,
    
    CDEMUD_E_DEVICEINITFAILED,
    
    CDEMUD_E_DBUSCONNECT,
    CDEMUD_E_DBUSNAMEREQUEST,
    
    CDEMUD_E_INVALIDDEVICE,
    
    CDEMUD_E_AUDIOBACKEND,
    CDEMUD_E_AUDIOINVALIDSTATE,
    
    CDEMUD_E_CTLDEVICE,
    CDEMUD_E_BUFFER,
    CDEMUD_E_ALREADYLOADED,
    CDEMUD_E_DEVLOCKED,    
    
    CDEMUD_E_GENERIC = 0xDEADFFF,
};

#include <glib.h>

GQuark  cdemud_error_quark  (void);
void    cdemud_error        (gint errcode, GError **error);

#endif /* __CDEMUD_ERROR_H__ */
