/** @file
 * Guest property service:
 * Common header for host service and guest clients.
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ___VBox_HostService_GuestPropertyService_h
#define ___VBox_HostService_GuestPropertyService_h

#include <VBox/types.h>
#include <VBox/VBoxGuest.h>
#include <VBox/hgcmsvc.h>

/** Everything defined in this file lives in this namespace. */
namespace guestProp {

/*
 * The service functions which are callable by host.
 */
enum eHostFn
{
    /** Pass the address of the cfgm node used by the service as a database. */
    SET_CFGM_NODE = 1,
    /**
     * Get the value attached to a guest property
     * The parameter format matches that of GET_PROP.
     */
    GET_PROP_HOST = 2,
    /**
     * Set the value attached to a guest property
     * The parameter format matches that of SET_PROP.
     */
    SET_PROP_HOST = 3,
    /**
     * Set the value attached to a guest property
     * The parameter format matches that of SET_PROP_VALUE.
     */
    SET_PROP_VALUE_HOST = 4,
    /**
     * Remove a guest property.
     * The parameter format matches that of DEL_PROP.
     */
    DEL_PROP_HOST = 5,
    /**
     * Enumerate guest properties.
     * The parameter format matches that of ENUM_PROPS.
     */
    ENUM_PROPS_HOST = 6,
    /**
     * Register a callback with the service which will be called when a
     * property is modified.  The callback is a function returning void and
     * taking a pointer to a HOSTCALLBACKDATA structure.
     */
    REGISTER_CALLBACK = 7
};

typedef struct _HOSTCALLBACKDATA
{
    /** Callback structure header */
    VBOXHGCMCALLBACKHDR hdr;
    /** The name of the property that was changed */
    const char *pcszName;
    /** The new property value, or NULL if the property was deleted */
    const char *pcszValue;
    /** The timestamp of the modification */
    uint64_t u64Timestamp;
    /** The flags field of the modified property */
    const char *pcszFlags;
} HOSTCALLBACKDATA, *PHOSTCALLBACKDATA;

/**
 * The service functions which are called by guest.  The numbers may not change,
 * so we hardcode them.
 */
enum eGuestFn
{
    /** Get a guest property */
    GET_PROP = 1,
    /** Set a guest property */
    SET_PROP = 2,
    /** Set just the value of a guest property */
    SET_PROP_VALUE = 3,
    /** Delete a guest property */
    DEL_PROP = 4,
    /** Enumerate guest properties */
    ENUM_PROPS = 5
};

/** Maximum length for property names */
enum { MAX_NAME_LEN = 64 };
/** Maximum length for property values */
enum { MAX_VALUE_LEN = 128 };
/** Maximum length for the property flags field */
enum { MAX_FLAGS_LEN = 128 };
/** Maximum number of properties per guest */
enum { MAX_PROPS = 256 };

/**
 * HGCM parameter structures.  Packing is explicitly defined as this is a wire format.
 */
#pragma pack (1)
/** The guest is requesting the value of a property */
typedef struct _GetProperty
{
    VBoxGuestHGCMCallInfo hdr;

    /**
     * The property name (IN pointer)
     * This must fit to a number of criteria, namely
     *  - Only Utf8 strings are allowed
     *  - Less than or equal to MAX_NAME_LEN bytes in length
     *  - Zero terminated
     */
    HGCMFunctionParameter name;

    /**
     * The returned string data will be placed here.  (OUT pointer)
     * This call returns two null-terminated strings which will be placed one
     * after another: value and flags.
     */
    HGCMFunctionParameter buffer;

    /**
     * The property timestamp.  (OUT uint64_t)
     */

    HGCMFunctionParameter timestamp;

    /**
     * If the buffer provided was large enough this will contain the size of
     * the returned data.  Otherwise it will contain the size of the buffer
     * needed to hold the data and VERR_BUFFER_OVERFLOW will be returned.
     * (OUT uint32_t)
     */
    HGCMFunctionParameter size;
} GetProperty;

/** The guest is requesting to change a property */
typedef struct _SetProperty
{
    VBoxGuestHGCMCallInfo hdr;

    /**
     * The property name.  (IN pointer)
     * This must fit to a number of criteria, namely
     *  - Only Utf8 strings are allowed
     *  - Less than or equal to MAX_NAME_LEN bytes in length
     *  - Zero terminated
     */
    HGCMFunctionParameter name;

    /**
     * The value of the property (IN pointer)
     * Criteria as for the name parameter, but with length less than or equal to
     * MAX_VALUE_LEN.  
     */
    HGCMFunctionParameter value;

    /**
     * The property flags (IN pointer)
     * This is a comma-separated list of the format flag=value
     * The length must be less than or equal to MAX_FLAGS_LEN and only
     * known flag names and values will be accepted.
     */
    HGCMFunctionParameter flags;
} SetProperty;

/** The guest is requesting to change the value of a property */
typedef struct _SetPropertyValue
{
    VBoxGuestHGCMCallInfo hdr;

    /**
     * The property name.  (IN pointer)
     * This must fit to a number of criteria, namely
     *  - Only Utf8 strings are allowed
     *  - Less than or equal to MAX_NAME_LEN bytes in length
     *  - Zero terminated
     */
    HGCMFunctionParameter name;

    /**
     * The value of the property (IN pointer)
     * Criteria as for the name parameter, but with length less than or equal to
     * MAX_VALUE_LEN.  
     */
    HGCMFunctionParameter value;
} SetPropertyValue;

/** The guest is requesting to remove a property */
typedef struct _DelProperty
{
    VBoxGuestHGCMCallInfo hdr;

    /**
     * The property name.  This must fit to a number of criteria, namely
     *  - Only Utf8 strings are allowed
     *  - Less than or equal to MAX_NAME_LEN bytes in length
     *  - Zero terminated
     */
    HGCMFunctionParameter name;
} DelProperty;

/** The guest is requesting to enumerate properties */
typedef struct _EnumProperties
{
    VBoxGuestHGCMCallInfo hdr;

    /**
     * Null-separated array of patterns to match the properties against.
     * (IN pointer)
     * If no patterns are given then return all.  The list is terminated by an
     * empty string.
     */
    HGCMFunctionParameter patterns;
    /**
     * On success, null-separated array of strings in which the properties are
     * returned.  (OUT pointer)
     * The number of strings in the array is always a multiple of four,
     * and in sequences of name, value, timestamp (hexadecimal string) and the
     * flags as a comma-separated list in the format "name=value".  The list
     * is terminated by an empty string after a "flags" entry (or at the
     * start).
     */
    HGCMFunctionParameter strings;
    /**
     * On success, the size of the returned data.  If the buffer provided is
     * too small, the size of buffer needed.  (OUT uint32_t)
     */
    HGCMFunctionParameter size;
} EnumProperties;
#pragma pack ()

} /* namespace guestProp */

#endif  /* ___VBox_HostService_GuestPropertySvc_h defined */
