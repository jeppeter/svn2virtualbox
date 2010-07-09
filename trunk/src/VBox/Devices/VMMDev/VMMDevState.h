/* $Id$ */
/** @file
 * VMMDev - Guest <-> VMM/Host communication device, internal header.
 */

/*
 * Copyright (C) 2006-2007 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___VMMDev_VMMDevState_h
#define ___VMMDev_VMMDevState_h

#include <VBox/VMMDev.h>
#include <VBox/pdmdev.h>
#include <VBox/pdmifs.h>

#define TIMESYNC_BACKDOOR

typedef struct DISPLAYCHANGEINFO
{
    uint32_t xres;
    uint32_t yres;
    uint32_t bpp;
    uint32_t display;
} DISPLAYCHANGEINFO;

typedef struct DISPLAYCHANGEREQUEST
{
    bool fPending;
    DISPLAYCHANGEINFO displayChangeRequest;
    DISPLAYCHANGEINFO lastReadDisplayChangeRequest;
} DISPLAYCHANGEREQUEST;

typedef struct DISPLAYCHANGEDATA
{
    /* Which monitor is being reported to the guest. */
    int iCurrentMonitor;

    /** true if the guest responded to VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST at least once */
    bool fGuestSentChangeEventAck;

    DISPLAYCHANGEREQUEST aRequests[64]; // @todo maxMonitors
} DISPLAYCHANGEDATA;


/** device structure containing all state information */
typedef struct VMMDevState
{
    /** The PCI device structure. */
    PCIDevice dev;

    /** The critical section for this device. */
    PDMCRITSECT CritSect;

    /** hypervisor address space size */
    uint32_t hypervisorSize;

    /** mouse capabilities of host and guest */
    uint32_t mouseCapabilities;
    /** absolute mouse position in pixels */
    uint32_t mouseXAbs;
    uint32_t mouseYAbs;
    /** Does the guest currently want the host pointer to be shown? */
    uint32_t fHostCursorRequested;

    /** Pointer to device instance. */
    PPDMDEVINSR3 pDevIns;
    /** LUN\#0 + Status: VMMDev port base interface. */
    PDMIBASE IBase;
    /** LUN\#0: VMMDev port interface. */
    PDMIVMMDEVPORT IPort;
#ifdef VBOX_WITH_HGCM
    /** LUN\#0: HGCM port interface. */
    PDMIHGCMPORT IHGCMPort;
#endif
    /** Pointer to base interface of the driver. */
    R3PTRTYPE(PPDMIBASE) pDrvBase;
    /** VMMDev connector interface */
    R3PTRTYPE(PPDMIVMMDEVCONNECTOR) pDrv;
#ifdef VBOX_WITH_HGCM
    /** HGCM connector interface */
    R3PTRTYPE(PPDMIHGCMCONNECTOR) pHGCMDrv;
#endif
    /** message buffer for backdoor logging. */
    char szMsg[512];
    /** message buffer index. */
    unsigned iMsg;
    /** Base port in the assigned I/O space. */
    RTIOPORT PortBase;

    /** IRQ number assigned to the device */
    uint32_t irq;
    /** Current host side event flags */
    uint32_t u32HostEventFlags;
    /** Mask of events guest is interested in. Note that the HGCM events
     *  are enabled automatically by the VMMDev device when guest issues
     *  HGCM commands.
     */
    uint32_t u32GuestFilterMask;
    /** Delayed mask of guest events */
    uint32_t u32NewGuestFilterMask;
    /** Flag whether u32NewGuestFilterMask is valid */
    bool fNewGuestFilterMask;

    /** R3 pointer to VMMDev RAM area */
    R3PTRTYPE(VMMDevMemory *) pVMMDevRAMR3;
    /** GC physical address of VMMDev RAM area */
    RTGCPHYS32 GCPhysVMMDevRAM;

    /** R3 pointer to VMMDev Heap RAM area
     */
    R3PTRTYPE(VMMDevMemory *) pVMMDevHeapR3;
    /** GC physical address of VMMDev Heap RAM area */
    RTGCPHYS32 GCPhysVMMDevHeap;

    /** Information reported by guest via VMMDevReportGuestInfo generic request.
     * Until this information is reported the VMMDev refuses any other requests.
     */
    VBoxGuestInfo guestInfo;

    /** Information reported by guest via VMMDevReportGuestCapabilities. */
    uint32_t      guestCaps;

    /** "Additions are Ok" indicator, set to true after processing VMMDevReportGuestInfo,
     * if additions version is compatible. This flag is here to avoid repeated comparing
     * of the version in guestInfo.
     */
    uint32_t fu32AdditionsOk;

    /** Video acceleration status set by guest. */
    uint32_t u32VideoAccelEnabled;

    DISPLAYCHANGEDATA displayChangeData;

    /** credentials for guest logon purposes */
    struct
    {
        char szUserName[VMMDEV_CREDENTIALS_STRLEN];
        char szPassword[VMMDEV_CREDENTIALS_STRLEN];
        char szDomain[VMMDEV_CREDENTIALS_STRLEN];
        bool fAllowInteractiveLogon;
    } credentialsLogon;

    /** credentials for verification by guest */
    struct
    {
        char szUserName[VMMDEV_CREDENTIALS_STRLEN];
        char szPassword[VMMDEV_CREDENTIALS_STRLEN];
        char szDomain[VMMDEV_CREDENTIALS_STRLEN];
    } credentialsJudge;

    /* memory balloon change request */
    uint32_t    u32MemoryBalloonSize, u32LastMemoryBalloonSize;

    /* guest ram size */
    uint64_t    cbGuestRAM;

    /* unique session id; the id will be different after each start, reset or restore of the VM. */
    uint64_t    idSession;

    /* statistics interval change request */
    uint32_t    u32StatIntervalSize, u32LastStatIntervalSize;

    /* seamless mode change request */
    bool fLastSeamlessEnabled, fSeamlessEnabled;

    bool fVRDPEnabled;
    uint32_t u32VRDPExperienceLevel;

#ifdef TIMESYNC_BACKDOOR
    bool fTimesyncBackdoorLo;
    uint64_t hostTime;
#endif
    /** Set if GetHostTime should fail.
     * Loaded from the GetHostTimeDisabled configuration value. */
    bool fGetHostTimeDisabled;

    /** Set if backdoor logging should be disabled (output will be ignored then) */
    bool fBackdoorLogDisabled;

    /** Don't clear credentials */
    bool fKeepCredentials;

    /** Heap enabled. */
    bool fHeapEnabled;

#ifdef VBOX_WITH_HGCM
    /** List of pending HGCM requests, used for saving the HGCM state. */
    R3PTRTYPE(PVBOXHGCMCMD) pHGCMCmdList;
    /** Critical section to protect the list. */
    RTCRITSECT critsectHGCMCmdList;
    /** Whether the HGCM events are already automatically enabled. */
    uint32_t u32HGCMEnabled;
#endif /* VBOX_WITH_HGCM */

    /** Status LUN: Shared folders LED */
    struct
    {
        /** The LED. */
        PDMLED                              Led;
        /** The LED ports. */
        PDMILEDPORTS                        ILeds;
        /** Partner of ILeds. */
        R3PTRTYPE(PPDMILEDCONNECTORS)       pLedsConnector;
    } SharedFolders;

    /** FLag whether CPU hotplug events are monitored */
    bool                fCpuHotPlugEventsEnabled;
    /** CPU hotplug event */
    VMMDevCpuEventType  enmCpuHotPlugEvent;
    /** Core id of the CPU to change */
    uint32_t            idCpuCore;
    /** Package id of the CPU to changhe */
    uint32_t            idCpuPackage;

    uint32_t            StatMemBalloonChunks;

    /** Set if RC/R0 is enabled. */
    bool                fRZEnabled;
    /** Set if testing is enabled. */
    bool                fTestingEnabled;
    /** The high timestamp value. */
    uint32_t            u32TestingHighTimestamp;

} VMMDevState;
AssertCompileMemberAlignment(VMMDevState, CritSect, 8);

void VMMDevNotifyGuest (VMMDevState *pVMMDevState, uint32_t u32EventMask);
void VMMDevCtlSetGuestFilterMask (VMMDevState *pVMMDevState,
                                  uint32_t u32OrMask,
                                  uint32_t u32NotMask);

#endif /* !___VMMDev_VMMDevState_h */

