/** @file
 *
 * VBox Host Guest Shared Memory Interface (HGSMI).
 * OS-independent guest structures.
 */

/*
 * Copyright (C) 2006-2008 Oracle Corporation
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
 */


#ifndef __HGSMI_GUEST_h__
#define __HGSMI_GUEST_h__

#include <VBox/HGSMI/HGSMI.h>
#include <VBox/HGSMI/HGSMIChSetup.h>

#ifdef VBOX_XPDM_MINIPORT
RT_C_DECLS_BEGIN
# include "miniport.h"
# include "ntddvdeo.h"
# include <Video.h>
RT_C_DECLS_END
#else
# include <iprt/asm-amd64-x86.h>
#endif

RT_C_DECLS_BEGIN

/**
 * Structure grouping the context needed for submitting commands to the host
 * via HGSMI 
 */
typedef struct _HGSMIGUESTCOMMANDCONTEXT
{
    /** Information about the memory heap located in VRAM from which data
     * structures to be sent to the host are allocated. */
    HGSMIHEAP heapCtx;
    /** The I/O port used for submitting commands to the host by writing their
     * offsets into the heap. */
    RTIOPORT port;
} HGSMIGUESTCOMMANDCONTEXT, *PHGSMIGUESTCOMMANDCONTEXT;


/**
 * Structure grouping the context needed for receiving commands from the host
 * via HGSMI 
 */
typedef struct _HGSMIHOSTCOMMANDCONTEXT
{
    /** Information about the memory area located in VRAM in which the host
     * places data structures to be read by the guest. */
    HGSMIAREA areaCtx;
    /** Convenience structure used for matching host commands to handlers. */
    /** @todo handlers are registered individually in code rather than just
     * passing a static structure in order to gain extra flexibility.  There is
     * currently no expected usage case for this though.  Is the additional
     * complexity really justified? */
    HGSMICHANNELINFO channels;
    /** Flag to indicate that one thread is currently processing the command
     * queue. */
    volatile bool fHostCmdProcessing;
    /* Pointer to the VRAM location where the HGSMI host flags are kept. */
    volatile HGSMIHOSTFLAGS *pfHostFlags;
    /** The I/O port used for receiving commands from the host as offsets into
     * the memory area and sending back confirmations (command completion,
     * IRQ acknowlegement). */
    RTIOPORT port;
} HGSMIHOSTCOMMANDCONTEXT, *PHGSMIHOSTCOMMANDCONTEXT;

/** @name Helper functions
 * @{ */
/** Write an 8-bit value to an I/O port. */
DECLINLINE(void) VBoxVideoCmnPortWriteUchar(RTIOPORT Port, uint8_t Value)
{
#ifdef VBOX_XPDM_MINIPORT
    VideoPortWritePortUchar((PUCHAR)Port, Value);
#else  /** @todo make these explicit */
    ASMOutU8(Port, Value);
#endif
}

/** Write a 16-bit value to an I/O port. */
DECLINLINE(void) VBoxVideoCmnPortWriteUshort(RTIOPORT Port, uint16_t Value)
{
#ifdef VBOX_XPDM_MINIPORT
    VideoPortWritePortUshort((PUSHORT)Port,Value);
#else
    ASMOutU16(Port, Value);
#endif
}

/** Write a 32-bit value to an I/O port. */
DECLINLINE(void) VBoxVideoCmnPortWriteUlong(RTIOPORT Port, uint32_t Value)
{
#ifdef VBOX_XPDM_MINIPORT
    VideoPortWritePortUlong((PULONG)Port,Value);
#else
    ASMOutU32(Port, Value);
#endif
}

/** Read an 8-bit value from an I/O port. */
DECLINLINE(uint8_t) VBoxVideoCmnPortReadUchar(RTIOPORT Port)
{
#ifdef VBOX_XPDM_MINIPORT
    return VideoPortReadPortUchar((PUCHAR)Port);
#else
    return ASMInU8(Port);
#endif
}

/** Read a 16-bit value from an I/O port. */
DECLINLINE(uint16_t) VBoxVideoCmnPortReadUshort(RTIOPORT Port)
{
#ifdef VBOX_XPDM_MINIPORT
    return VideoPortReadPortUshort((PUSHORT)Port);
#else
    return ASMInU16(Port);
#endif
}

/** Read a 32-bit value from an I/O port. */
DECLINLINE(uint32_t) VBoxVideoCmnPortReadUlong(RTIOPORT Port)
{
#ifdef VBOX_XPDM_MINIPORT
    return VideoPortReadPortUlong((PULONG)Port);
#else
    return ASMInU32(Port);
#endif
}

/** @}  */

/** @name Base HGSMI APIs
 * @{ */

/** Acknowlege an IRQ. */
DECLINLINE(void) VBoxHGSMIClearIrq(PHGSMIHOSTCOMMANDCONTEXT pCtx)
{
    VBoxVideoCmnPortWriteUlong(pCtx->port, HGSMIOFFSET_VOID);
}

RTDECL(void)     VBoxHGSMIHostCmdComplete(PHGSMIHOSTCOMMANDCONTEXT pCtx,
                                          void *pvMem);
RTDECL(void)     VBoxHGSMIProcessHostQueue(PHGSMIHOSTCOMMANDCONTEXT pCtx);
RTDECL(bool)     VBoxHGSMIIsSupported(void);
RTDECL(void *)   VBoxHGSMIBufferAlloc(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                      HGSMISIZE cbData,
                                      uint8_t u8Ch,
                                      uint16_t u16Op);
RTDECL(void)     VBoxHGSMIBufferFree(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                     void *pvBuffer);
RTDECL(int)      VBoxHGSMIBufferSubmit(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                       void *pvBuffer);

typedef struct VBVAINFOVIEW *PVBVAINFOVIEW;
/**
 * Callback funtion called from @a VBoxHGSMISendViewInfo to initialise
 * the @a VBVAINFOVIEW structure for each screen.
 *
 * @returns  iprt status code
 * @param  pvData  context data for the callback, passed to @a
 *                 VBoxHGSMISendViewInfo along with the callback
 * @param  pInfo   array of @a VBVAINFOVIEW structures to be filled in
 * @todo  explicitly pass the array size
 */
typedef DECLCALLBACK(int) FNHGSMIFILLVIEWINFO (void *pvData,
                                               PVBVAINFOVIEW pInfo);
/** Pointer to a FNHGSMIFILLVIEWINFO callback */
typedef FNHGSMIFILLVIEWINFO *PFNHGSMIFILLVIEWINFO;

RTDECL(int)      VBoxHGSMISendViewInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                       uint32_t u32Count,
                                       PFNHGSMIFILLVIEWINFO pfnFill,
                                       void *pvData);
RTDECL(void)     VBoxHGSMIGetBaseMappingInfo(uint32_t cbVRAM,
                                             uint32_t *poffVRAMBaseMapping,
                                             uint32_t *pcbMapping,
                                             uint32_t *poffGuestHeapMemory,
                                             uint32_t *pcbGuestHeapMemory,
                                             uint32_t *poffHostFlags);
/** @todo we should provide a cleanup function too as part of the API */
RTDECL(int)      VBoxHGSMISetupGuestContext(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                            void *pvGuestHeapMemory,
                                            uint32_t cbGuestHeapMemory,
                                            uint32_t offVRAMGuestHeapMemory);
RTDECL(void)     VBoxHGSMIGetHostAreaMapping(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                             uint32_t cbVRAM,
                                             uint32_t offVRAMBaseMapping,
                                             uint32_t *poffVRAMHostArea,
                                             uint32_t *pcbHostArea);
RTDECL(void)     VBoxHGSMISetupHostContext(PHGSMIHOSTCOMMANDCONTEXT pCtx,
                                           void *pvBaseMapping,
                                           uint32_t offHostFlags,
                                           void *pvHostAreaMapping,
                                           uint32_t offVRAMHostArea,
                                           uint32_t cbHostArea);
RTDECL(int)      VBoxHGSMISendHostCtxInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                          HGSMIOFFSET offVRAMFlagsLocation,
                                          uint32_t fCaps,
                                          uint32_t offVRAMHostArea,
                                          uint32_t cbHostArea);
RTDECL(uint32_t) VBoxHGSMIGetMonitorCount(PHGSMIGUESTCOMMANDCONTEXT pCtx);
RTDECL(bool)     VBoxHGSMIUpdatePointerShape(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                             uint32_t fFlags,
                                             uint32_t cHotX,
                                             uint32_t cHotY,
                                             uint32_t cWidth,
                                             uint32_t cHeight,
                                             uint8_t *pPixels,
                                             uint32_t cbLength);

/** @}  */

RT_C_DECLS_END

#endif /* __HGSMI_GUEST_h__*/
