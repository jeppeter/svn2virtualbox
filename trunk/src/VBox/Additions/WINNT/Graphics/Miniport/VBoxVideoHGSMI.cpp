/* $Id$ */
/** @file
 * VirtualBox Video miniport driver for NT/2k/XP - HGSMI related functions.
 */

/*
 * Copyright (C) 2006-2009 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <string.h>

#include "VBoxVideo.h"
#include "Helper.h"

#include <iprt/asm.h>
#include <iprt/log.h>
#include <iprt/thread.h>
#include <VBox/VMMDev.h>
#include <VBox/VBoxGuest.h>
#include <VBox/VBoxVideo.h>

// #include <VBoxDisplay.h>

// #include "vboxioctl.h"

void HGSMINotifyHostCmdComplete (PVBOXVIDEO_COMMON pCommon, HGSMIOFFSET offt)
{
    VBoxHGSMIHostWrite(pCommon, offt);
}

void HGSMIClearIrq (PVBOXVIDEO_COMMON pCommon)
{
    VBoxHGSMIHostWrite(pCommon, HGSMIOFFSET_VOID);
}

static void HGSMIHostCmdComplete (PVBOXVIDEO_COMMON pCommon, void * pvMem)
{
    HGSMIOFFSET offMem = HGSMIPointerToOffset (&pCommon->areaHostHeap, HGSMIBufferHeaderFromData (pvMem));
    Assert(offMem != HGSMIOFFSET_VOID);
    if(offMem != HGSMIOFFSET_VOID)
    {
        HGSMINotifyHostCmdComplete (pCommon, offMem);
    }
}

static void hgsmiHostCmdProcess(PVBOXVIDEO_COMMON pCommon, HGSMIOFFSET offBuffer)
{
    int rc = HGSMIBufferProcess (&pCommon->areaHostHeap,
                                &pCommon->channels,
                                offBuffer);
    Assert(!RT_FAILURE(rc));
    if(RT_FAILURE(rc))
    {
        /* failure means the command was not submitted to the handler for some reason
         * it's our responsibility to notify its completion in this case */
        HGSMINotifyHostCmdComplete(pCommon, offBuffer);
    }
    /* if the cmd succeeded it's responsibility of the callback to complete it */
}

static HGSMIOFFSET hgsmiGetHostBuffer (PVBOXVIDEO_COMMON pCommon)
{
    return VBoxHGSMIHostRead(pCommon);
}

static void hgsmiHostCommandQueryProcess (PVBOXVIDEO_COMMON pCommon)
{
    HGSMIOFFSET offset = hgsmiGetHostBuffer (pCommon);
    Assert(offset != HGSMIOFFSET_VOID);
    if(offset != HGSMIOFFSET_VOID)
    {
        hgsmiHostCmdProcess(pCommon, offset);
    }
}

void hgsmiProcessHostCommandQueue(PVBOXVIDEO_COMMON pCommon)
{
    while (pCommon->pHostFlags->u32HostFlags & HGSMIHOSTFLAGS_COMMANDS_PENDING)
    {
        if (!ASMAtomicCmpXchgBool(&pCommon->bHostCmdProcessing, true, false))
            return;
        hgsmiHostCommandQueryProcess(pCommon);
        ASMAtomicWriteBool(&pCommon->bHostCmdProcessing, false);
    }
}

/* Detect whether HGSMI is supported by the host. */
bool VBoxHGSMIIsSupported (void)
{
    uint16_t DispiId;

    VBoxVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    VBoxVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ID_HGSMI);

    DispiId = VBoxVideoCmnPortReadUshort(VBE_DISPI_IOPORT_DATA);

    return (DispiId == VBE_DISPI_ID_HGSMI);
}

typedef int FNHGSMICALLINIT (PVBOXVIDEO_COMMON pCommon, void *pvContext, void *pvData);
typedef FNHGSMICALLINIT *PFNHGSMICALLINIT;

typedef int FNHGSMICALLFINALIZE (PVBOXVIDEO_COMMON pCommon, void *pvContext, void *pvData);
typedef FNHGSMICALLFINALIZE *PFNHGSMICALLFINALIZE;

void* vboxHGSMIBufferAlloc(PVBOXVIDEO_COMMON pCommon,
                         HGSMISIZE cbData,
                         uint8_t u8Ch,
                         uint16_t u16Op)
{
#ifdef VBOX_WITH_WDDM
    /* @todo: add synchronization */
#endif
    return HGSMIHeapAlloc (&pCommon->hgsmiAdapterHeap, cbData, u8Ch, u16Op);
}

void vboxHGSMIBufferFree (PVBOXVIDEO_COMMON pCommon, void *pvBuffer)
{
#ifdef VBOX_WITH_WDDM
    /* @todo: add synchronization */
#endif
    HGSMIHeapFree (&pCommon->hgsmiAdapterHeap, pvBuffer);
}

int vboxHGSMIBufferSubmit (PVBOXVIDEO_COMMON pCommon, void *pvBuffer)
{
    /* Initialize the buffer and get the offset for port IO. */
    HGSMIOFFSET offBuffer = HGSMIHeapBufferOffset (&pCommon->hgsmiAdapterHeap, pvBuffer);

    Assert(offBuffer != HGSMIOFFSET_VOID);
    if (offBuffer != HGSMIOFFSET_VOID)
    {
        /* Submit the buffer to the host. */
        VBoxHGSMIGuestWrite(pCommon, offBuffer);
        return VINF_SUCCESS;
    }

    return VERR_INVALID_PARAMETER;
}

static int vboxCallChannel (PVBOXVIDEO_COMMON pCommon,
                         uint8_t u8Ch,
                         uint16_t u16Op,
                         HGSMISIZE cbData,
                         PFNHGSMICALLINIT pfnInit,
                         PFNHGSMICALLFINALIZE pfnFinalize,
                         void *pvContext)
{
    int rc = VINF_SUCCESS;

    /* Allocate the IO buffer. */
    void *p = HGSMIHeapAlloc (&pCommon->hgsmiAdapterHeap, cbData, u8Ch, u16Op);

    if (!p)
    {
        rc = VERR_NO_MEMORY;
    }
    else
    {
        /* Prepare data to be sent to the host. */
        if (pfnInit)
        {
            rc = pfnInit (pCommon, pvContext, p);
        }

        if (RT_SUCCESS (rc))
        {
            /* Initialize the buffer and get the offset for port IO. */
            HGSMIOFFSET offBuffer = HGSMIHeapBufferOffset (&pCommon->hgsmiAdapterHeap,
                                                           p);

            /* Submit the buffer to the host. */
            VBoxHGSMIGuestWrite(pCommon, offBuffer);

            if (pfnFinalize)
            {
                rc = pfnFinalize (pCommon, pvContext, p);
            }
        }
        else
        {
            AssertFailed ();
            rc = VERR_INTERNAL_ERROR;
        }

        /* Free the IO buffer. */
        HGSMIHeapFree (&pCommon->hgsmiAdapterHeap, p);
    }

    return rc;
}

static int vboxCallVBVA (PVBOXVIDEO_COMMON pCommon,
                         uint16_t u16Op,
                         HGSMISIZE cbData,
                         PFNHGSMICALLINIT pfnInit,
                         PFNHGSMICALLFINALIZE pfnFinalize,
                         void *pvContext)
{
    return vboxCallChannel (pCommon,
                            HGSMI_CH_VBVA,
                            u16Op,
                            cbData,
                            pfnInit,
                            pfnFinalize,
                            pvContext);
}

typedef struct _QUERYCONFCTX
{
    uint32_t  u32Index;
    uint32_t *pulValue;
} QUERYCONFCTX;

static int vbvaInitQueryConf (PVBOXVIDEO_COMMON, void *pvContext, void *pvData)
{
    QUERYCONFCTX *pCtx = (QUERYCONFCTX *)pvContext;
    VBVACONF32 *p = (VBVACONF32 *)pvData;

    p->u32Index = pCtx->u32Index;
    p->u32Value = 0;

    return VINF_SUCCESS;
}

static int vbvaFinalizeQueryConf (PVBOXVIDEO_COMMON, void *pvContext, void *pvData)
{
    QUERYCONFCTX *pCtx = (QUERYCONFCTX *)pvContext;
    VBVACONF32 *p = (VBVACONF32 *)pvData;

    if (pCtx->pulValue)
    {
        *pCtx->pulValue = p->u32Value;
    }

    Log(("VBoxVideo::vboxQueryConf: u32Value = %d\n", p->u32Value));
    return VINF_SUCCESS;
}

static int vboxQueryConfHGSMI (PVBOXVIDEO_COMMON pCommon, uint32_t u32Index, uint32_t *pulValue)
{
    Log(("VBoxVideo::vboxQueryConf: u32Index = %d\n", u32Index));

    QUERYCONFCTX context;

    context.u32Index = u32Index;
    context.pulValue = pulValue;

    int rc = vboxCallVBVA (pCommon,
                           VBVA_QUERY_CONF32,
                           sizeof (VBVACONF32),
                           vbvaInitQueryConf,
                           vbvaFinalizeQueryConf,
                           &context);

    Log(("VBoxVideo::vboxQueryConf: rc = %d\n", rc));

    return rc;
}

int VBoxHGSMISendViewInfo(PVBOXVIDEO_COMMON pCommon, uint32_t u32Count, PFNHGSMIFILLVIEWINFO pfnFill, void *pvData)
{
    int rc;
    /* Issue the screen info command. */
    void *p = vboxHGSMIBufferAlloc (pCommon, sizeof(VBVAINFOVIEW) * u32Count,
                                    HGSMI_CH_VBVA, VBVA_INFO_VIEW);
    Assert(p);
    if (p)
    {
        VBVAINFOVIEW *pInfo = (VBVAINFOVIEW *)p;
        rc = pfnFill(pvData, pInfo);
        if (RT_SUCCESS(rc))
            vboxHGSMIBufferSubmit (pCommon, p);
        vboxHGSMIBufferFree (pCommon, p);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


int vbvaInitInfoCaps (PVBOXVIDEO_COMMON pCommon, void *pvContext, void *pvData)
{
    VBVACAPS *pCaps = (VBVACAPS*)pvData;
    pCaps->rc = VERR_NOT_IMPLEMENTED;
    pCaps->fCaps = pCommon->fCaps;
    return VINF_SUCCESS;
}


int vbvaFinalizeInfoCaps (PVBOXVIDEO_COMMON, void *pvContext, void *pvData)
{
    VBVACAPS *pCaps = (VBVACAPS*)pvData;
    AssertRC(pCaps->rc);
    return pCaps->rc;
}

static int vbvaInitInfoHeap (PVBOXVIDEO_COMMON pCommon, void *pvContext, void *pvData)
{
    NOREF (pvContext);
    VBVAINFOHEAP *p = (VBVAINFOHEAP *)pvData;

    p->u32HeapOffset = pCommon->cbVRAM
                       - pCommon->cbMiniportHeap
                       - VBVA_ADAPTER_INFORMATION_SIZE;
    p->u32HeapSize = pCommon->cbMiniportHeap;

    return VINF_SUCCESS;
}

static int hgsmiInitFlagsLocation (PVBOXVIDEO_COMMON pCommon, void *pvContext, void *pvData)
{
    NOREF (pvContext);
    HGSMIBUFFERLOCATION *p = (HGSMIBUFFERLOCATION *)pvData;

    p->offLocation = pCommon->cbVRAM - sizeof (HGSMIHOSTFLAGS);
    p->cbLocation = sizeof (HGSMIHOSTFLAGS);

    return VINF_SUCCESS;
}


static int vboxSetupAdapterInfoHGSMI (PVBOXVIDEO_COMMON pCommon)
{
    Log(("VBoxVideo::vboxSetupAdapterInfo\n"));

    /* setup the flags first to ensure they are initialized by the time the host heap is ready */
    int rc = vboxCallChannel(pCommon,
            HGSMI_CH_HGSMI,
            HGSMI_CC_HOST_FLAGS_LOCATION,
                       sizeof (HGSMIBUFFERLOCATION),
                       hgsmiInitFlagsLocation,
                       NULL,
                       NULL);
    AssertRC(rc);
    if (RT_SUCCESS(rc) && pCommon->fCaps)
    {
        /* Inform about caps */
        rc = vboxCallVBVA (pCommon,
                               VBVA_INFO_CAPS,
                               sizeof (VBVACAPS),
                               vbvaInitInfoCaps,
                               vbvaFinalizeInfoCaps,
                               NULL);
        AssertRC(rc);
    }
    if (RT_SUCCESS (rc))
    {
        /* Report the host heap location. */
        rc = vboxCallVBVA (pCommon,
                           VBVA_INFO_HEAP,
                           sizeof (VBVAINFOHEAP),
                           vbvaInitInfoHeap,
                           NULL,
                           NULL);
        AssertRC(rc);
    }


    Log(("VBoxVideo::vboxSetupAdapterInfo finished rc = %d\n", rc));

    return rc;
}


/**
 * Helper function to register secondary displays (DualView). Note that this will not
 * be available on pre-XP versions, and some editions on XP will fail because they are
 * intentionally crippled.
 *
 * HGSMI variant is a bit different because it uses only HGSMI interface (VBVA channel)
 * to talk to the host.
 */
void VBoxSetupDisplaysHGSMI(PVBOXVIDEO_COMMON pCommon,
                            uint32_t AdapterMemorySize, uint32_t fCaps)
{
    /** @todo I simply converted this from Windows error codes.  That is wrong,
     * but we currently freely mix and match those (failure == rc > 0) and iprt
     * ones (failure == rc < 0) anyway.  This needs to be fully reviewed and
     * fixed. */
    int rc = VINF_SUCCESS;

    Log(("VBoxVideo::VBoxSetupDisplays: pCommon = %p\n", pCommon));

    memset(pCommon, 0, sizeof(*pCommon));
    pCommon->cbVRAM    = AdapterMemorySize;
    pCommon->fCaps     = fCaps;
    pCommon->cDisplays = 1;
    pCommon->bHGSMI    = VBoxHGSMIIsSupported ();
    /* Why does this use VBoxVideoCmnMemZero?  The MSDN docs say that it should
     * only be used on mapped display adapter memory.  Done with memset above. */
    // VBoxVideoCmnMemZero(&pCommon->areaHostHeap, sizeof(HGSMIAREA));
    if (pCommon->bHGSMI)
    {
        /** @note (michael) moved this here as it is done unconditionally in both
         * driver branches.  Feel free to fix if that is ever changed. */
        pCommon->IOPortHost = (RTIOPORT)VGA_PORT_HGSMI_HOST;
        pCommon->IOPortGuest = (RTIOPORT)VGA_PORT_HGSMI_GUEST;

        /* Map the adapter information. It will be needed for HGSMI IO. */
        /** @todo all callers of VBoxMapAdapterMemory expect it to use iprt
         * error codes, but it doesn't. */
        rc = VBoxMapAdapterMemory (pCommon, &pCommon->pvAdapterInformation,
                                   AdapterMemorySize - VBVA_ADAPTER_INFORMATION_SIZE,
                                   VBVA_ADAPTER_INFORMATION_SIZE
                                  );
        if (RT_FAILURE(rc))
        {
            Log(("VBoxVideo::VBoxSetupDisplays: VBoxMapAdapterMemory pvAdapterInfoirrmation failed rc = %d\n",
                     rc));

            pCommon->bHGSMI = false;
        }
        else
        {
            /* Setup a HGSMI heap within the adapter information area. */
            rc = HGSMIHeapSetup (&pCommon->hgsmiAdapterHeap,
                                 pCommon->pvAdapterInformation,
                                 VBVA_ADAPTER_INFORMATION_SIZE - sizeof(HGSMIHOSTFLAGS),
                                 pCommon->cbVRAM - VBVA_ADAPTER_INFORMATION_SIZE,
                                 false /*fOffsetBased*/);

            if (RT_FAILURE(rc))
            {
                Log(("VBoxVideo::VBoxSetupDisplays: HGSMIHeapSetup failed rc = %d\n",
                         rc));

                pCommon->bHGSMI = false;
            }
            else
            {
                pCommon->pHostFlags = (HGSMIHOSTFLAGS*)(((uint8_t*)pCommon->pvAdapterInformation)
                                                            + VBVA_ADAPTER_INFORMATION_SIZE - sizeof(HGSMIHOSTFLAGS));
            }
        }
    }

    /* Setup the host heap and the adapter memory. */
    if (pCommon->bHGSMI)
    {
        /* The miniport heap is used for the host buffers. */
        uint32_t cbMiniportHeap = 0;
        vboxQueryConfHGSMI (pCommon, VBOX_VBVA_CONF32_HOST_HEAP_SIZE, &cbMiniportHeap);

        if (cbMiniportHeap != 0)
        {
            /* Do not allow too big heap. No more than 25% of VRAM is allowed. */
            uint32_t cbMiniportHeapMaxSize = AdapterMemorySize / 4;

            if (cbMiniportHeapMaxSize >= VBVA_ADAPTER_INFORMATION_SIZE)
            {
                cbMiniportHeapMaxSize -= VBVA_ADAPTER_INFORMATION_SIZE;
            }

            if (cbMiniportHeap > cbMiniportHeapMaxSize)
            {
                cbMiniportHeap = cbMiniportHeapMaxSize;
            }

            /* Round up to 4096 bytes. */
            pCommon->cbMiniportHeap = (cbMiniportHeap + 0xFFF) & ~0xFFF;

            Log(("VBoxVideo::VBoxSetupDisplays: cbMiniportHeap = 0x%08X, pCommon->cbMiniportHeap = 0x%08X, cbMiniportHeapMaxSize = 0x%08X\n",
                     cbMiniportHeap, pCommon->cbMiniportHeap, cbMiniportHeapMaxSize));

            /* Map the heap region.
             *
             * Note: the heap will be used for the host buffers submitted to the guest.
             *       The miniport driver is responsible for reading FIFO and notifying
             *       display drivers.
             */
            rc = VBoxMapAdapterMemory (pCommon, &pCommon->pvMiniportHeap,
                                       pCommon->cbVRAM
                                       - VBVA_ADAPTER_INFORMATION_SIZE
                                       - pCommon->cbMiniportHeap,
                                       pCommon->cbMiniportHeap
                                      );
            if (RT_FAILURE(rc))
            {
                pCommon->pvMiniportHeap = NULL;
                pCommon->cbMiniportHeap = 0;
                pCommon->bHGSMI = false;
            }
            else
            {
                HGSMIOFFSET offBase = pCommon->cbVRAM
                                      - VBVA_ADAPTER_INFORMATION_SIZE
                                      - pCommon->cbMiniportHeap;

                /* Init the host hap area. Buffers from the host will be placed there. */
                HGSMIAreaInitialize (&pCommon->areaHostHeap,
                                     pCommon->pvMiniportHeap,
                                     pCommon->cbMiniportHeap,
                                     offBase);
            }
        }
        else
        {
            /* Host has not requested a heap. */
            pCommon->pvMiniportHeap = NULL;
            pCommon->cbMiniportHeap = 0;
        }
    }

    /* Check whether the guest supports multimonitors. */
    if (pCommon->bHGSMI)
    {
        /* Query the configured number of displays. */
        uint32_t cDisplays = 0;
        vboxQueryConfHGSMI (pCommon, VBOX_VBVA_CONF32_MONITOR_COUNT, &cDisplays);

        Log(("VBoxVideo::VBoxSetupDisplays: cDisplays = %d\n",
                 cDisplays));

        if (cDisplays == 0 || cDisplays > VBOX_VIDEO_MAX_SCREENS)
        {
            /* Host reported some bad value. Continue in the 1 screen mode. */
            cDisplays = 1;
        }
        pCommon->cDisplays = cDisplays;
    }

    if (pCommon->bHGSMI)
    {
        /* Setup the information for the host. */
        rc = vboxSetupAdapterInfoHGSMI (pCommon);

        if (RT_FAILURE(rc))
        {
            pCommon->bHGSMI = false;
        }
    }

    if (!pCommon->bHGSMI)
        VBoxFreeDisplaysHGSMI(pCommon);

    Log(("VBoxVideo::VBoxSetupDisplays: finished\n"));
}

static bool VBoxUnmapAdpInfoCallback(void *pvCommon)
{
    PVBOXVIDEO_COMMON pCommon = (PVBOXVIDEO_COMMON)pvCommon;

    pCommon->pHostFlags = NULL;
    return true;
}

void VBoxFreeDisplaysHGSMI(PVBOXVIDEO_COMMON pCommon)
{
    VBoxUnmapAdapterMemory(pCommon, &pCommon->pvMiniportHeap);
    HGSMIHeapDestroy(&pCommon->hgsmiAdapterHeap);

    /* Unmap the adapter information needed for HGSMI IO. */
    VBoxSyncToVideoIRQ(pCommon, VBoxUnmapAdpInfoCallback, pCommon);
    VBoxUnmapAdapterMemory(pCommon, &pCommon->pvAdapterInformation);
}

/*
 * Send the pointer shape to the host.
 */
typedef struct _MOUSEPOINTERSHAPECTX
{
    uint32_t fFlags;
    uint32_t cHotX;
    uint32_t cHotY;
    uint32_t cWidth;
    uint32_t cHeight;
    uint32_t cbData;
    uint8_t *pPixels;
    int32_t  i32Result;
} MOUSEPOINTERSHAPECTX;

static int vbvaInitMousePointerShape (PVBOXVIDEO_COMMON, void *pvContext, void *pvData)
{
    MOUSEPOINTERSHAPECTX *pCtx = (MOUSEPOINTERSHAPECTX *)pvContext;
    VBVAMOUSEPOINTERSHAPE *p = (VBVAMOUSEPOINTERSHAPE *)pvData;

    /* Will be updated by the host. */
    p->i32Result = VINF_SUCCESS;

    /* We have our custom flags in the field */
    p->fu32Flags = pCtx->fFlags;

    p->u32HotX   = pCtx->cHotX;
    p->u32HotY   = pCtx->cHotY;
    p->u32Width  = pCtx->cWidth;
    p->u32Height = pCtx->cHeight;

    if (p->fu32Flags & VBOX_MOUSE_POINTER_SHAPE)
    {
        /* If shape is supplied, then always create the pointer visible.
         * See comments in 'vboxUpdatePointerShape'
         */
        p->fu32Flags |= VBOX_MOUSE_POINTER_VISIBLE;

        /* Copy the actual pointer data. */
        memcpy (p->au8Data, pCtx->pPixels, pCtx->cbData);
    }

    return VINF_SUCCESS;
}

static int vbvaFinalizeMousePointerShape (PVBOXVIDEO_COMMON, void *pvContext, void *pvData)
{
    MOUSEPOINTERSHAPECTX *pCtx = (MOUSEPOINTERSHAPECTX *)pvContext;
    VBVAMOUSEPOINTERSHAPE *p = (VBVAMOUSEPOINTERSHAPE *)pvData;

    pCtx->i32Result = p->i32Result;

    return VINF_SUCCESS;
}

bool vboxUpdatePointerShape (PVBOXVIDEO_COMMON pCommon,
                             uint32_t fFlags,
                             uint32_t cHotX,
                             uint32_t cHotY,
                             uint32_t cWidth,
                             uint32_t cHeight,
                             uint8_t *pPixels,
                             uint32_t cbLength)
{
    uint32_t cbData = 0;

    if (fFlags & VBOX_MOUSE_POINTER_SHAPE)
    {
        /* Size of the pointer data: sizeof (AND mask) + sizeof (XOR_MASK) */
        cbData = ((((cWidth + 7) / 8) * cHeight + 3) & ~3)
                 + cWidth * 4 * cHeight;
    }

#ifndef DEBUG_misha
    Log(("vboxUpdatePointerShape: cbData %d, %dx%d\n",
             cbData, cWidth, cHeight));
#endif

    if (cbData > cbLength)
    {
        Log(("vboxUpdatePointerShape: calculated pointer data size is too big (%d bytes, limit %d)\n",
                 cbData, cbLength));
        return false;
    }

    MOUSEPOINTERSHAPECTX ctx;

    ctx.fFlags    = fFlags;
    ctx.cHotX     = cHotX;
    ctx.cHotY     = cHotY;
    ctx.cWidth    = cWidth;
    ctx.cHeight   = cHeight;
    ctx.pPixels   = pPixels;
    ctx.cbData    = cbData;
    ctx.i32Result = VERR_NOT_SUPPORTED;

    int rc = vboxCallVBVA (pCommon, VBVA_MOUSE_POINTER_SHAPE,
                           sizeof (VBVAMOUSEPOINTERSHAPE) + cbData,
                           vbvaInitMousePointerShape,
                           vbvaFinalizeMousePointerShape,
                           &ctx);
#ifndef DEBUG_misha
    Log(("VBoxVideo::vboxMousePointerShape: rc %d, i32Result = %d\n", rc, ctx.i32Result));
#endif

    return RT_SUCCESS(rc) && RT_SUCCESS(ctx.i32Result);
}

#ifndef VBOX_WITH_WDDM
typedef struct _VBVAMINIPORT_CHANNELCONTEXT
{
    PFNHGSMICHANNELHANDLER pfnChannelHandler;
    void *pvChannelHandler;
}VBVAMINIPORT_CHANNELCONTEXT;

typedef struct _VBVADISP_CHANNELCONTEXT
{
    /** The generic command handler builds up a list of commands - in reverse
     * order! - here */
    VBVAHOSTCMD *pCmd;
    bool bValid;
}VBVADISP_CHANNELCONTEXT;

typedef struct _VBVA_CHANNELCONTEXTS
{
    PVBOXVIDEO_COMMON pCommon;
    uint32_t cUsed;
    uint32_t cContexts;
    VBVAMINIPORT_CHANNELCONTEXT mpContext;
    VBVADISP_CHANNELCONTEXT aContexts[1];
}VBVA_CHANNELCONTEXTS;

static int vboxVBVADeleteChannelContexts(PVBOXVIDEO_COMMON pCommon,
                                         VBVA_CHANNELCONTEXTS * pContext)
{
    VBoxVideoCmnMemFreeDriver(pCommon, pContext);
    return VINF_SUCCESS;
}

static int vboxVBVACreateChannelContexts(PVBOXVIDEO_COMMON pCommon, VBVA_CHANNELCONTEXTS ** ppContext)
{
    uint32_t cDisplays = (uint32_t)pCommon->cDisplays;
    const size_t size = RT_OFFSETOF(VBVA_CHANNELCONTEXTS, aContexts[cDisplays]);
    VBVA_CHANNELCONTEXTS * pContext = (VBVA_CHANNELCONTEXTS*)VBoxVideoCmnMemAllocDriver(pCommon, size);
    if(pContext)
    {
        memset(pContext, 0, size);
        pContext->cContexts = cDisplays;
        pContext->pCommon = pCommon;
        *ppContext = pContext;
        return VINF_SUCCESS;
    }
    return VERR_GENERAL_FAILURE;
}

static VBVADISP_CHANNELCONTEXT* vboxVBVAFindHandlerInfo(VBVA_CHANNELCONTEXTS *pCallbacks, int iId)
{
    if(iId < 0)
    {
        return NULL;
    }
    else if(pCallbacks->cContexts > (uint32_t)iId)
    {
        return &pCallbacks->aContexts[iId];
    }
    return NULL;
}

DECLCALLBACK(void) hgsmiHostCmdComplete (HVBOXVIDEOHGSMI hHGSMI, struct _VBVAHOSTCMD * pCmd)
{
    PVBOXVIDEO_COMMON pCommon = (PVBOXVIDEO_COMMON)hHGSMI;
    HGSMIHostCmdComplete (pCommon, pCmd);
}

/** Reverses a NULL-terminated linked list of VBVAHOSTCMD structures. */
static VBVAHOSTCMD *vboxVBVAReverseList(VBVAHOSTCMD *pList)
{
    VBVAHOSTCMD *pFirst = NULL;
    while (pList)
    {
        VBVAHOSTCMD *pNext = pList;
        pList = pList->u.pNext;
        pNext->u.pNext = pFirst;
        pFirst = pNext;
    }
    return pFirst;
}

DECLCALLBACK(int) hgsmiHostCmdRequest (HVBOXVIDEOHGSMI hHGSMI, uint8_t u8Channel, uint32_t iDevice, struct _VBVAHOSTCMD ** ppCmd)
{
//    if(display < 0)
//        return VERR_INVALID_PARAMETER;
    if(!ppCmd)
        return VERR_INVALID_PARAMETER;

    PVBOXVIDEO_COMMON pCommon = (PVBOXVIDEO_COMMON)hHGSMI;

    /* pick up the host commands */
    hgsmiProcessHostCommandQueue(pCommon);

    HGSMICHANNEL * pChannel = HGSMIChannelFindById (&pCommon->channels, u8Channel);
    if(pChannel)
    {
        VBVA_CHANNELCONTEXTS * pContexts = (VBVA_CHANNELCONTEXTS *)pChannel->handler.pvHandler;
        VBVADISP_CHANNELCONTEXT *pDispContext = vboxVBVAFindHandlerInfo(pContexts, iDevice);
        Assert(pDispContext);
        if(pDispContext)
        {
            VBVAHOSTCMD *pCmd;
            do
                pCmd = ASMAtomicReadPtrT(&pDispContext->pCmd, VBVAHOSTCMD *);
            while (!ASMAtomicCmpXchgPtr(&pDispContext->pCmd, NULL, pCmd));
            *ppCmd = vboxVBVAReverseList(pCmd);

            return VINF_SUCCESS;
        }
    }

    return VERR_INVALID_PARAMETER;
}


static DECLCALLBACK(int) vboxVBVAChannelGenericHandler(void *pvHandler, uint16_t u16ChannelInfo, void *pvBuffer, HGSMISIZE cbBuffer)
{
    VBVA_CHANNELCONTEXTS *pCallbacks = (VBVA_CHANNELCONTEXTS*)pvHandler;
//    Assert(0);
    Assert(cbBuffer > VBVAHOSTCMD_HDRSIZE);
    if(cbBuffer > VBVAHOSTCMD_HDRSIZE)
    {
        VBVAHOSTCMD *pHdr = (VBVAHOSTCMD*)pvBuffer;
        Assert(pHdr->iDstID >= 0);
        if(pHdr->iDstID >= 0)
        {
            VBVADISP_CHANNELCONTEXT* pHandler = vboxVBVAFindHandlerInfo(pCallbacks, pHdr->iDstID);
            Assert(pHandler && pHandler->bValid);
            if(pHandler && pHandler->bValid)
            {
                VBVAHOSTCMD *pFirst = NULL, *pLast = NULL;
                for(VBVAHOSTCMD *pCur = pHdr; pCur; )
                {
                    Assert(!pCur->u.Data);
                    Assert(!pFirst);
                    Assert(!pLast);

                    switch(u16ChannelInfo)
                    {
                        case VBVAHG_DISPLAY_CUSTOM:
                        {
#if 0  /* Never taken */
                            if(pLast)
                            {
                                pLast->u.pNext = pCur;
                                pLast = pCur;
                            }
                            else
#endif
                            {
                                pFirst = pCur;
                                pLast = pCur;
                            }
                            Assert(!pCur->u.Data);
#if 0  /* Who is supposed to set pNext? */
                            //TODO: use offset here
                            pCur = pCur->u.pNext;
                            Assert(!pCur);
#else
                            Assert(!pCur->u.pNext);
                            pCur = NULL;
#endif
                            Assert(pFirst);
                            Assert(pFirst == pLast);
                            break;
                        }
                        case VBVAHG_EVENT:
                        {
                            VBVAHOSTCMDEVENT *pEventCmd = VBVAHOSTCMD_BODY(pCur, VBVAHOSTCMDEVENT);
                            VBoxVideoCmnSignalEvent(pCallbacks->pCommon, pEventCmd->pEvent);
                        }
                        default:
                        {
                            Assert(u16ChannelInfo==VBVAHG_EVENT);
                            Assert(!pCur->u.Data);
#if 0  /* pLast has been asserted to be NULL, and who should set pNext? */
                            //TODO: use offset here
                            if(pLast)
                                pLast->u.pNext = pCur->u.pNext;
                            VBVAHOSTCMD * pNext = pCur->u.pNext;
                            pCur->u.pNext = NULL;
#else
                            Assert(!pCur->u.pNext);
#endif
                            HGSMIHostCmdComplete(pCallbacks->pCommon, pCur);
#if 0  /* pNext is NULL, and the other things have already been asserted */
                            pCur = pNext;
                            Assert(!pCur);
                            Assert(!pFirst);
                            Assert(pFirst == pLast);
#else
                            pCur = NULL;
#endif
                            break;
                        }
                    }
                }

                /* we do not support lists currently */
                Assert(pFirst == pLast);
                if(pLast)
                {
                    Assert(pLast->u.pNext == NULL);
                }

                if(pFirst)
                {
                    Assert(pLast);
                    VBVAHOSTCMD *pCmd;
                    do
                    {
                        pCmd = ASMAtomicReadPtrT(&pHandler->pCmd, VBVAHOSTCMD *);
                        pFirst->u.pNext = pCmd;
                    }
                    while (!ASMAtomicCmpXchgPtr(&pHandler->pCmd, pFirst, pCmd));
                }
                else
                {
                    Assert(!pLast);
                }
                return VINF_SUCCESS;
            }
        }
        else
        {
            //TODO: impl
//          HGSMIMINIPORT_CHANNELCONTEXT *pHandler = vboxVideoHGSMIFindHandler;
//           if(pHandler && pHandler->pfnChannelHandler)
//           {
//               pHandler->pfnChannelHandler(pHandler->pvChannelHandler, u16ChannelInfo, pHdr, cbBuffer);
//
//               return VINF_SUCCESS;
//           }
        }
    }
    /* no handlers were found, need to complete the command here */
    HGSMIHostCmdComplete(pCallbacks->pCommon, pvBuffer);
    return VINF_SUCCESS;
}

static HGSMICHANNELHANDLER g_OldHandler;

int vboxVBVAChannelDisplayEnable(PVBOXVIDEO_COMMON pCommon,
        int iDisplay, /* negative would mean this is a miniport handler */
        uint8_t u8Channel)
{
    VBVA_CHANNELCONTEXTS * pContexts;
    HGSMICHANNEL * pChannel = HGSMIChannelFindById (&pCommon->channels, u8Channel);
    if(!pChannel)
    {
        int rc = vboxVBVACreateChannelContexts(pCommon, &pContexts);
        if(RT_FAILURE(rc))
        {
            return rc;
        }
    }
    else
    {
        pContexts = (VBVA_CHANNELCONTEXTS *)pChannel->handler.pvHandler;
    }

    VBVADISP_CHANNELCONTEXT *pDispContext = vboxVBVAFindHandlerInfo(pContexts, iDisplay);
    Assert(pDispContext);
    if(pDispContext)
    {
#ifdef DEBUGVHWASTRICT
        Assert(!pDispContext->bValid);
#endif
        Assert(!pDispContext->pCmd);
        if(!pDispContext->bValid)
        {
            pDispContext->bValid = true;
            pDispContext->pCmd = NULL;

            int rc = VINF_SUCCESS;
            if(!pChannel)
            {
                rc = HGSMIChannelRegister (&pCommon->channels,
                                           u8Channel,
                                           "VGA Miniport HGSMI channel",
                                           vboxVBVAChannelGenericHandler,
                                           pContexts,
                                           &g_OldHandler);
            }

            if(RT_SUCCESS(rc))
            {
                pContexts->cUsed++;
                return VINF_SUCCESS;
            }
        }
    }

    if(!pChannel)
    {
        vboxVBVADeleteChannelContexts(pCommon, pContexts);
    }

    return VERR_GENERAL_FAILURE;
}
#endif /* !VBOX_WITH_WDDM */

/** @todo Mouse pointer position to be read from VMMDev memory, address of the memory region
 * can be queried from VMMDev via an IOCTL. This VMMDev memory region will contain
 * host information which is needed by the guest.
 *
 * Reading will not cause a switch to the host.
 *
 * Have to take into account:
 *  * synchronization: host must write to the memory only from EMT,
 *    large structures must be read under flag, which tells the host
 *    that the guest is currently reading the memory (OWNER flag?).
 *  * guest writes: may be allocate a page for the host info and make
 *    the page readonly for the guest.
 *  * the information should be available only for additions drivers.
 *  * VMMDev additions driver will inform the host which version of the info it expects,
 *    host must support all versions.
 *
 */

