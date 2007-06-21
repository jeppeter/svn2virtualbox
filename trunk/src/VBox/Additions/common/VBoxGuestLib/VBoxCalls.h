/** @file
 *
 * VBoxGuestLib - A support library for VirtualBox guest additions:
 * Central calls header
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

#ifndef __VBOXCALLS__H
#define __VBOXCALLS__H

#include <VBox/VBoxGuestLib.h>
#ifndef _NTIFS_
# ifdef __WIN__
#  if (_MSC_VER >= 1400) && !defined(VBOX_WITH_PATCHED_DDK)
#   include <iprt/asm.h>
#   define _InterlockedExchange           _InterlockedExchange_StupidDDKvsCompilerCrap
#   define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKvsCompilerCrap
#   define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKvsCompilerCrap
#   define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKvsCompilerCrap
    __BEGIN_DECLS
#   include <ntddk.h>
    __END_DECLS
#   undef  _InterlockedExchange
#   undef  _InterlockedExchangeAdd
#   undef  _InterlockedCompareExchange
#   undef  _InterlockedAddLargeStatistic
#  else
    __BEGIN_DECLS
#   include <ntddk.h>
    __END_DECLS
#  endif
# endif
#endif

#ifdef DEBUG
# define LOG_ENABLED
#endif
#include "VBoxGuestLog.h"

#include <iprt/assert.h>
#define ASSERTVBSF AssertRelease

#include <VBox/shflsvc.h>

typedef struct _VBSFCLIENT
{
    uint32_t ulClientID;
    VBGLHGCMHANDLE handle;
} VBSFCLIENT;
typedef VBSFCLIENT *PVBSFCLIENT;

typedef struct _VBSFMAP
{
    SHFLROOT root;
} VBSFMAP, *PVBSFMAP;


#define VBSF_DRIVE_LETTER_FIRST   L'A'
#define VBSF_DRIVE_LETTER_LAST    L'Z'

#define VBSF_MAX_DRIVES           (VBSF_DRIVE_LETTER_LAST - VBSF_DRIVE_LETTER_FIRST)

/* Poller thread flags. */
#define VBSF_TF_NONE             (0x0000)
#define VBSF_TF_STARTED          (0x0001)
#define VBSF_TF_TERMINATE        (0x0002)
#define VBSF_TF_START_PROCESSING (0x0004)

#define DRIVE_FLAG_WORKING         (0x1)
#define DRIVE_FLAG_LOCKED          (0x2)
#define DRIVE_FLAG_WRITE_PROTECTED (0x4)

#ifdef __WIN__
/** Device extension structure for each drive letter we created. */
typedef struct _VBSFDRIVE
{
    /*  A pointer to the Driver object we created for the drive. */
    PDEVICE_OBJECT pDeviceObject;

    /** Root handle to access the drive. */
    SHFLROOT root;

    /** Informational string - the resource name on host. */
    WCHAR awcNameHost[256];

    /** Guest drive letter. */
    WCHAR wcDriveLetter;

    /** DRIVE_FLAG_* */
    uint32_t u32DriveFlags;

    /** Head of FCB list. */
    LIST_ENTRY FCBHead;

    /* Synchronise requests directed to the drive. */
    ERESOURCE DriveResource;
} VBSFDRIVE;
typedef VBSFDRIVE *PVBSFDRIVE;
#endif /* __WIN__ */

/* forward decl */
struct _MRX_VBOX_DEVICE_EXTENSION;
typedef struct _MRX_VBOX_DEVICE_EXTENSION *PMRX_VBOX_DEVICE_EXTENSION;

DECLVBGL(int)  vboxInit (void);
DECLVBGL(void) vboxUninit (void);
DECLVBGL(int)  vboxConnect (PVBSFCLIENT pClient);
DECLVBGL(void) vboxDisconnect (PVBSFCLIENT pClient);

DECLVBGL(int) vboxCallQueryMappings (PVBSFCLIENT pClient, SHFLMAPPING paMappings[], uint32_t *pcMappings);

DECLVBGL(int) vboxCallQueryMapName (PVBSFCLIENT pClient, SHFLROOT root, SHFLSTRING *pString, uint32_t size);

DECLVBGL(int) vboxCallCreate (PVBSFCLIENT pClient, PVBSFMAP pMap, PSHFLSTRING pParsedPath, PSHFLCREATEPARMS pCreateParms);

DECLVBGL(int) vboxCallClose (PVBSFCLIENT pClient, PVBSFMAP pMap, SHFLHANDLE Handle);
DECLVBGL(int) vboxCallRemove (PVBSFCLIENT pClient, PVBSFMAP pMap, PSHFLSTRING pParsedPath, uint32_t flags);
DECLVBGL(int) vboxCallRename (PVBSFCLIENT pClient, PVBSFMAP pMap, PSHFLSTRING pSrcPath, PSHFLSTRING pDestPath, uint32_t flags);
DECLVBGL(int) vboxCallFlush (PVBSFCLIENT pClient, PVBSFMAP pMap, SHFLHANDLE hFile);

DECLVBGL(int) vboxCallRead (PVBSFCLIENT pClient, PVBSFMAP pMap, SHFLHANDLE hFile, uint64_t offset, uint32_t *pcbBuffer, uint8_t *pBuffer);
DECLVBGL(int) vboxCallWrite (PVBSFCLIENT pClient, PVBSFMAP pMap, SHFLHANDLE hFile, uint64_t offset, uint32_t *pcbBuffer, uint8_t *pBuffer);

DECLVBGL(int) vboxCallLock (PVBSFCLIENT pClient, PVBSFMAP pMap, SHFLHANDLE hFile, uint64_t offset, uint64_t cbSize, uint32_t fLock);

DECLVBGL(int) vboxCallDirInfo (PVBSFCLIENT pClient, PVBSFMAP pMap, SHFLHANDLE hFile,PSHFLSTRING ParsedPath, uint32_t flags,
                               uint32_t index, uint32_t *pcbBuffer, PSHFLDIRINFO pBuffer, uint32_t *pcFiles);
DECLVBGL(int) vboxCallFSInfo (PVBSFCLIENT pClient, PVBSFMAP pMap, SHFLHANDLE hFile, uint32_t flags, uint32_t *pcbBuffer, PSHFLDIRINFO pBuffer);

DECLVBGL(int) vboxCallMapFolder (PVBSFCLIENT pClient, PSHFLSTRING szFolderName, PVBSFMAP pMap);
DECLVBGL(int) vboxCallUnmapFolder (PVBSFCLIENT pClient, PVBSFMAP pMap);
DECLVBGL(int) vboxCallSetUtf8 (PVBSFCLIENT pClient);

#endif /* __VBOXCALLS__H */
