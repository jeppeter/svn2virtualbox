/** @file
 *
 * Module to dynamically load libhal and libdbus and load all symbols
 * which are needed by VirtualBox.
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */

#ifndef ____H_VBOX_LIBHAL
#define ____H_VBOX_LIBHAL

#include <stdint.h>

#define LIB_DBUS "libdbus-1"
#define LIB_HAL "libhal"

/** Types from the dbus and hal header files which we need.  These are taken more or less
    verbatim from the DBus and Hal public interface header files. */
struct DBusError
{
    const char *name;
    const char *message;
    unsigned int dummy1 : 1; /**< placeholder */
    unsigned int dummy2 : 1; /**< placeholder */
    unsigned int dummy3 : 1; /**< placeholder */
    unsigned int dummy4 : 1; /**< placeholder */
    unsigned int dummy5 : 1; /**< placeholder */
    void *padding1; /**< placeholder */
};
struct DBusConnection;
typedef struct DBusConnection DBusConnection;
typedef uint32_t dbus_bool_t;
typedef enum { DBUS_BUS_SESSON, DBUS_BUS_SYSTEM, DBUS_BUS_STARTER } DBusBusType;
struct LibHalContext_s;
typedef struct LibHalContext_s LibHalContext;

/** The following are the symbols which we need from libdbus and libhal. */
extern void (*DBusErrorInit)(DBusError *);
extern DBusConnection *(*DBusBusGet)(DBusBusType, DBusError *);
extern void (*DBusErrorFree)(DBusError *);
extern void (*DBusConnectionUnref)(DBusConnection *);
extern LibHalContext *(*LibHalCtxNew)(void);
extern dbus_bool_t (*LibHalCtxSetDBusConnection)(LibHalContext *, DBusConnection *);
extern dbus_bool_t (*LibHalCtxInit)(LibHalContext *, DBusError *);
extern char **(*LibHalFindDeviceByCapability)(LibHalContext *, const char *, int *, DBusError *);
extern char *(*LibHalDeviceGetPropertyString)(LibHalContext *, const char *, const char *,
                                              DBusError *);
extern void (*LibHalFreeString)(char *);
extern void (*LibHalFreeStringArray)(char **);
extern dbus_bool_t (*LibHalCtxShutdown)(LibHalContext *, DBusError *);
extern dbus_bool_t (*LibHalCtxFree)(LibHalContext *);

extern bool LibHalCheckPresence(void);

#endif /* ____H_VBOX_LIBHAL not defined */
