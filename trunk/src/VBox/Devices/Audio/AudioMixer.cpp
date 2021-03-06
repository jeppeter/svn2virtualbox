/* $Id$ */
/** @file
 * Audio mixing routines for multiplexing audio sources in device emulations.
 *
 * Overview
 * ========
 *
 * This mixer acts as a layer between the audio connector interface and
 * the actual device emulation, providing mechanisms for audio sources (input)
 * and audio sinks (output).
 *
 * Think of this mixer as kind of a high(er) level interface for the audio
 * connector interface, abstracting common tasks such as creating and managing
 * various audio sources and sinks. This mixer class is purely optional and can
 * be left out when implementing a new device emulation, using only the audi
 * connector interface instead.  For example, the SB16 emulation does not use
 * this mixer and does all its stream management on its own.
 *
 * As audio driver instances are handled as LUNs on the device level, this
 * audio mixer then can take care of e.g. mixing various inputs/outputs to/from
 * a specific source/sink.
 *
 * How and which audio streams are connected to sinks/sources depends on how
 * the audio mixer has been set up.
 *
 * A sink can connect multiple output streams together, whereas a source
 * does this with input streams. Each sink / source consists of one or more
 * so-called mixer streams, which then in turn have pointers to the actual
 * PDM audio input/output streams.
 *
 * Playback
 * ========
 *
 * For output sinks there can be one or more mixing stream attached.
 * As the host sets the overall pace for the device emulation (virtual time
 * in the guest OS vs. real time on the host OS), an output mixing sink
 * needs to make sure that all connected output streams are able to accept
 * all the same amount of data at a time.
 *
 * This is called synchronous multiplexing.
 *
 * A mixing sink employs an own audio mixing buffer, which in turn can convert
 * the audio (output) data supplied from the device emulation into the sink's
 * audio format. As all connected mixing streams in theory could have the same
 * audio format as the mixing sink (parent), this can save processing time when
 * it comes to serving a lot of mixing streams at once. That way only one
 * conversion must be done, instead of each stream having to iterate over the
 * data.
 *
 * Recording
 * =========
 *
 * For input sinks only one mixing stream at a time can be the recording
 * source currently. A recording source is optional, e.g. it is possible to
 * have no current recording source set. Switching to a different recording
 * source at runtime is possible.
 */

/*
 * Copyright (C) 2014-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_AUDIO_MIXER
#include <VBox/log.h>
#include "AudioMixer.h"
#include "AudioMixBuffer.h"
#include "AudioHlp.h"

#include <VBox/vmm/pdm.h>
#include <VBox/err.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>

#include <iprt/alloc.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int audioMixerAddSinkInternal(PAUDIOMIXER pMixer, PAUDMIXSINK pSink);
static int audioMixerRemoveSinkInternal(PAUDIOMIXER pMixer, PAUDMIXSINK pSink);

static int audioMixerSinkInit(PAUDMIXSINK pSink, PAUDIOMIXER pMixer, const char *pcszName, AUDMIXSINKDIR enmDir);
static void audioMixerSinkDestroyInternal(PAUDMIXSINK pSink, PPDMDEVINS pDevIns);
static int audioMixerSinkUpdateVolume(PAUDMIXSINK pSink, const PPDMAUDIOVOLUME pVolMaster);
static void audioMixerSinkRemoveAllStreamsInternal(PAUDMIXSINK pSink);
static int audioMixerSinkRemoveStreamInternal(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream);
static void audioMixerSinkReset(PAUDMIXSINK pSink);
static int audioMixerSinkSetRecSourceInternal(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream);

static int audioMixerStreamCtlInternal(PAUDMIXSTREAM pMixStream, PDMAUDIOSTREAMCMD enmCmd, uint32_t fCtl);
static void audioMixerStreamDestroyInternal(PAUDMIXSTREAM pStream, PPDMDEVINS pDevIns);
static int audioMixerStreamUpdateStatus(PAUDMIXSTREAM pMixStream);


/** size of output buffer for dbgAudioMixerSinkStatusToStr.   */
#define AUDIOMIXERSINK_STATUS_STR_MAX sizeof("RUNNING PENDING_DISABLE DIRTY 0x12345678")

/**
 * Converts a mixer sink status to a string.
 *
 * @returns pszDst
 * @param   fStatus     The mixer sink status.
 * @param   pszDst      The output buffer.  Must be at least
 *                      AUDIOMIXERSINK_STATUS_STR_MAX in length.
 */
static const char *dbgAudioMixerSinkStatusToStr(AUDMIXSINKSTS fStatus, char pszDst[AUDIOMIXERSINK_STATUS_STR_MAX])
{
    if (!fStatus)
        return strcpy(pszDst, "NONE");
    static const struct
    {
        const char *pszMnemonic;
        uint32_t    cchMnemonic;
        uint32_t    fStatus;
    } s_aFlags[] =
    {
        { RT_STR_TUPLE("RUNNING "),          AUDMIXSINK_STS_RUNNING },
        { RT_STR_TUPLE("PENDING_DISABLE "),  AUDMIXSINK_STS_PENDING_DISABLE },
        { RT_STR_TUPLE("DIRTY "),            AUDMIXSINK_STS_DIRTY },
    };
    char *psz = pszDst;
    for (size_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
        if (fStatus & s_aFlags[i].fStatus)
        {
            memcpy(psz, s_aFlags[i].pszMnemonic, s_aFlags[i].cchMnemonic);
            psz += s_aFlags[i].cchMnemonic;
            fStatus &= ~s_aFlags[i].fStatus;
            if (!fStatus)
            {
                psz[-1] = '\0';
                return pszDst;
            }
        }
    RTStrPrintf(psz, AUDIOMIXERSINK_STATUS_STR_MAX - (psz - pszDst), "%#x", fStatus);
    return pszDst;
}

/**
 * Creates an audio sink and attaches it to the given mixer.
 *
 * @returns VBox status code.
 * @param   pMixer      Mixer to attach created sink to.
 * @param   pszName     Name of the sink to create.
 * @param   enmDir      Direction of the sink to create.
 * @param   pDevIns     The device instance to register statistics under.
 * @param   ppSink      Pointer which returns the created sink on success.
 */
int AudioMixerCreateSink(PAUDIOMIXER pMixer, const char *pszName, AUDMIXSINKDIR enmDir, PPDMDEVINS pDevIns, PAUDMIXSINK *ppSink)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    /* ppSink is optional. */

    int rc = RTCritSectEnter(&pMixer->CritSect);
    AssertRCReturn(rc, rc);

    PAUDMIXSINK pSink = (PAUDMIXSINK)RTMemAllocZ(sizeof(AUDMIXSINK));
    if (pSink)
    {
        rc = audioMixerSinkInit(pSink, pMixer, pszName, enmDir);
        if (RT_SUCCESS(rc))
        {
            rc = audioMixerAddSinkInternal(pMixer, pSink);
            if (RT_SUCCESS(rc))
            {
                RTCritSectLeave(&pMixer->CritSect);

                char szPrefix[128];
                RTStrPrintf(szPrefix, sizeof(szPrefix), "MixerSink-%s/", pSink->pszName);
                PDMDevHlpSTAMRegisterF(pDevIns, &pSink->MixBuf.cFrames, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                                       "Sink mixer buffer size in frames.",         "%sMixBufSize", szPrefix);
                PDMDevHlpSTAMRegisterF(pDevIns, &pSink->MixBuf.cUsed, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                                       "Sink mixer buffer fill size in frames.",    "%sMixBufUsed", szPrefix);
                PDMDevHlpSTAMRegisterF(pDevIns, &pSink->cStreams, STAMTYPE_U8, STAMVISIBILITY_USED, STAMUNIT_NONE,
                                       "Number of streams attached to the sink.",   "%sStreams", szPrefix);

                if (ppSink)
                    *ppSink = pSink;
                return VINF_SUCCESS;
            }
        }

        audioMixerSinkDestroyInternal(pSink, pDevIns);

        RTMemFree(pSink);
        pSink = NULL;
    }
    else
        rc = VERR_NO_MEMORY;

    RTCritSectLeave(&pMixer->CritSect);
    return rc;
}

/**
 * Creates an audio mixer.
 *
 * @returns VBox status code.
 * @param   pcszName            Name of the audio mixer.
 * @param   fFlags              Creation flags.
 * @param   ppMixer             Pointer which returns the created mixer object.
 */
int AudioMixerCreate(const char *pcszName, uint32_t fFlags, PAUDIOMIXER *ppMixer)
{
    AssertPtrReturn(pcszName, VERR_INVALID_POINTER);
    AssertReturn   (!(fFlags & ~AUDMIXER_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppMixer, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    PAUDIOMIXER pMixer = (PAUDIOMIXER)RTMemAllocZ(sizeof(AUDIOMIXER));
    if (pMixer)
    {
        pMixer->pszName = RTStrDup(pcszName);
        if (!pMixer->pszName)
            rc = VERR_NO_MEMORY;

        if (RT_SUCCESS(rc))
            rc = RTCritSectInit(&pMixer->CritSect);

        if (RT_SUCCESS(rc))
        {
            pMixer->cSinks = 0;
            RTListInit(&pMixer->lstSinks);

            pMixer->fFlags = fFlags;

            if (pMixer->fFlags & AUDMIXER_FLAGS_DEBUG)
                LogRel(("Audio Mixer: Debug mode enabled\n"));

            /* Set master volume to the max. */
            pMixer->VolMaster.fMuted = false;
            pMixer->VolMaster.uLeft  = PDMAUDIO_VOLUME_MAX;
            pMixer->VolMaster.uRight = PDMAUDIO_VOLUME_MAX;

            LogFlowFunc(("Created mixer '%s'\n", pMixer->pszName));

            *ppMixer = pMixer;
        }
        else
            RTMemFree(pMixer); /** @todo leaks pszName due to badly structured code */
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Helper function for the internal debugger to print the mixer's current
 * state, along with the attached sinks.
 *
 * @param   pMixer              Mixer to print debug output for.
 * @param   pHlp                Debug info helper to use.
 * @param   pszArgs             Optional arguments. Not being used at the moment.
 */
void AudioMixerDebug(PAUDIOMIXER pMixer, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PAUDMIXSINK pSink;
    unsigned    iSink = 0;

    int rc2 = RTCritSectEnter(&pMixer->CritSect);
    if (RT_FAILURE(rc2))
        return;

    pHlp->pfnPrintf(pHlp, "[Master] %s: lVol=%u, rVol=%u, fMuted=%RTbool\n", pMixer->pszName,
                    pMixer->VolMaster.uLeft, pMixer->VolMaster.uRight, pMixer->VolMaster.fMuted);

    RTListForEach(&pMixer->lstSinks, pSink, AUDMIXSINK, Node)
    {
        pHlp->pfnPrintf(pHlp, "[Sink %u] %s: lVol=%u, rVol=%u, fMuted=%RTbool\n", iSink, pSink->pszName,
                        pSink->Volume.uLeft, pSink->Volume.uRight, pSink->Volume.fMuted);
        ++iSink;
    }

    rc2 = RTCritSectLeave(&pMixer->CritSect);
    AssertRC(rc2);
}

/**
 * Destroys an audio mixer.
 *
 * @param   pMixer      Audio mixer to destroy.
 * @param   pDevIns     The device instance the statistics are associated with.
 */
void AudioMixerDestroy(PAUDIOMIXER pMixer, PPDMDEVINS pDevIns)
{
    if (!pMixer)
        return;

    int rc2 = RTCritSectEnter(&pMixer->CritSect);
    AssertRC(rc2);

    LogFlowFunc(("Destroying %s ...\n", pMixer->pszName));

    PAUDMIXSINK pSink, pSinkNext;
    RTListForEachSafe(&pMixer->lstSinks, pSink, pSinkNext, AUDMIXSINK, Node)
    {
        audioMixerSinkDestroyInternal(pSink, pDevIns);
        audioMixerRemoveSinkInternal(pMixer, pSink);
        RTMemFree(pSink);
    }

    Assert(pMixer->cSinks == 0);

    RTStrFree(pMixer->pszName);
    pMixer->pszName = NULL;

    rc2 = RTCritSectLeave(&pMixer->CritSect);
    AssertRC(rc2);

    RTCritSectDelete(&pMixer->CritSect);

    RTMemFree(pMixer);
    pMixer = NULL;
}

/**
 * Invalidates all internal data, internal version.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to invalidate data for.
 */
int audioMixerInvalidateInternal(PAUDIOMIXER pMixer)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);

    LogFlowFunc(("[%s]\n", pMixer->pszName));

    /* Propagate new master volume to all connected sinks. */
    PAUDMIXSINK pSink;
    RTListForEach(&pMixer->lstSinks, pSink, AUDMIXSINK, Node)
    {
        int rc2 = audioMixerSinkUpdateVolume(pSink, &pMixer->VolMaster);
        AssertRC(rc2);
    }

    return VINF_SUCCESS;
}

/**
 * Invalidates all internal data.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to invalidate data for.
 */
void AudioMixerInvalidate(PAUDIOMIXER pMixer)
{
    AssertPtrReturnVoid(pMixer);

    int rc2 = RTCritSectEnter(&pMixer->CritSect);
    AssertRC(rc2);

    LogFlowFunc(("[%s]\n", pMixer->pszName));

    rc2 = audioMixerInvalidateInternal(pMixer);
    AssertRC(rc2);

    rc2 = RTCritSectLeave(&pMixer->CritSect);
    AssertRC(rc2);
}

/**
 * Adds sink to an existing mixer.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to add sink to.
 * @param   pSink               Sink to add.
 */
static int audioMixerAddSinkInternal(PAUDIOMIXER pMixer, PAUDMIXSINK pSink)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);
    AssertPtrReturn(pSink,  VERR_INVALID_POINTER);

    /** @todo Check upper sink limit? */
    /** @todo Check for double-inserted sinks? */

    RTListAppend(&pMixer->lstSinks, &pSink->Node);
    pMixer->cSinks++;

    LogFlowFunc(("pMixer=%p, pSink=%p, cSinks=%RU8\n",
                 pMixer, pSink, pMixer->cSinks));

    return VINF_SUCCESS;
}

/**
 * Removes a formerly attached audio sink for an audio mixer, internal version.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to remove sink from.
 * @param   pSink               Sink to remove.
 */
static int audioMixerRemoveSinkInternal(PAUDIOMIXER pMixer, PAUDMIXSINK pSink)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);
    if (!pSink)
        return VERR_NOT_FOUND;

    AssertMsgReturn(pSink->pParent == pMixer, ("%s: Is not part of mixer '%s'\n",
                                               pSink->pszName, pMixer->pszName), VERR_NOT_FOUND);

    LogFlowFunc(("[%s] pSink=%s, cSinks=%RU8\n",
                 pMixer->pszName, pSink->pszName, pMixer->cSinks));

    /* Remove sink from mixer. */
    RTListNodeRemove(&pSink->Node);

    Assert(pMixer->cSinks);
    pMixer->cSinks--;

    /* Set mixer to NULL so that we know we're not part of any mixer anymore. */
    pSink->pParent = NULL;

    return VINF_SUCCESS;
}

/**
 * Removes a formerly attached audio sink for an audio mixer.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to remove sink from.
 * @param   pSink               Sink to remove.
 */
void AudioMixerRemoveSink(PAUDIOMIXER pMixer, PAUDMIXSINK pSink)
{
    int rc2 = RTCritSectEnter(&pMixer->CritSect);
    AssertRC(rc2);

    audioMixerSinkRemoveAllStreamsInternal(pSink);
    audioMixerRemoveSinkInternal(pMixer, pSink);

    rc2 = RTCritSectLeave(&pMixer->CritSect);
}

/**
 * Sets the mixer's master volume.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to set master volume for.
 * @param   pVol                Volume to set.
 */
int AudioMixerSetMasterVolume(PAUDIOMIXER pMixer, PPDMAUDIOVOLUME pVol)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);
    AssertPtrReturn(pVol,   VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pMixer->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    memcpy(&pMixer->VolMaster, pVol, sizeof(PDMAUDIOVOLUME));

    LogFlowFunc(("[%s] lVol=%RU32, rVol=%RU32 => fMuted=%RTbool, lVol=%RU32, rVol=%RU32\n",
                 pMixer->pszName, pVol->uLeft, pVol->uRight,
                 pMixer->VolMaster.fMuted, pMixer->VolMaster.uLeft, pMixer->VolMaster.uRight));

    rc = audioMixerInvalidateInternal(pMixer);

    int rc2 = RTCritSectLeave(&pMixer->CritSect);
    AssertRC(rc2);

    return rc;
}

/*********************************************************************************************************************************
 * Mixer Sink implementation.
 ********************************************************************************************************************************/

/**
 * Adds an audio stream to a specific audio sink.
 *
 * @returns VBox status code.
 * @param   pSink               Sink to add audio stream to.
 * @param   pStream             Stream to add.
 */
int AudioMixerSinkAddStream(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream)
{
    AssertPtrReturn(pSink,   VERR_INVALID_POINTER);
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    if (pSink->cStreams == UINT8_MAX) /* 255 streams per sink max. */
    {
        int rc2 = RTCritSectLeave(&pSink->CritSect);
        AssertRC(rc2);

        return VERR_NO_MORE_HANDLES;
    }

    LogFlowFuncEnter();

    /** @todo Check if stream already is assigned to (another) sink. */

    /* If the sink is running and not in pending disable mode,
     * make sure that the added stream also is enabled. */
    if (    (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
        && !(pSink->fStatus & AUDMIXSINK_STS_PENDING_DISABLE))
    {
        rc = audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_ENABLE, AUDMIXSTRMCTL_F_NONE);
        if (rc == VERR_AUDIO_STREAM_NOT_READY)
            rc = VINF_SUCCESS; /* Not fatal here, stream can become available at some later point in time. */
    }

    if (RT_SUCCESS(rc) && pSink->enmDir != AUDMIXSINKDIR_OUTPUT)
    {
        /* Apply the sink's combined volume to the stream. */
        rc = pStream->pConn->pfnStreamSetVolume(pStream->pConn, pStream->pStream, &pSink->VolumeCombined);
        AssertRC(rc);
    }

    if (RT_SUCCESS(rc))
    {
        /* Save pointer to sink the stream is attached to. */
        pStream->pSink = pSink;

        /* Append stream to sink's list. */
        RTListAppend(&pSink->lstStreams, &pStream->Node);
        pSink->cStreams++;
    }

    LogFlowFunc(("[%s] cStreams=%RU8, rc=%Rrc\n", pSink->pszName, pSink->cStreams, rc));

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return rc;
}

/**
 * Creates an audio mixer stream.
 *
 * @returns VBox status code.
 * @param   pSink       Sink to use for creating the stream.
 * @param   pConn       Audio connector interface to use.
 * @param   pCfg        Audio stream configuration to use.  This may be modified
 *                      in some unspecified way (see
 *                      PDMIAUDIOCONNECTOR::pfnStreamCreate).
 * @param   fFlags      Stream flags. Currently unused, set to 0.
 * @param   pDevIns     The device instance to register statistics with.
 * @param   ppStream    Pointer which receives the newly created audio stream.
 */
int AudioMixerSinkCreateStream(PAUDMIXSINK pSink, PPDMIAUDIOCONNECTOR pConn, PPDMAUDIOSTREAMCFG pCfg,
                               AUDMIXSTREAMFLAGS fFlags, PPDMDEVINS pDevIns, PAUDMIXSTREAM *ppStream)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    AssertPtrReturn(pConn, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfg,  VERR_INVALID_POINTER);
    /** @todo Validate fFlags. */
    /* ppStream is optional. */
    RT_NOREF(pDevIns); /* we'll probably be adding more statistics */

    /*
     * Check status and get the host driver config.
     */
    if (pConn->pfnGetStatus(pConn, PDMAUDIODIR_DUPLEX) == PDMAUDIOBACKENDSTS_NOT_ATTACHED)
        return VERR_AUDIO_BACKEND_NOT_ATTACHED;

    PDMAUDIOBACKENDCFG BackendCfg;
    int rc = pConn->pfnGetConfig(pConn, &BackendCfg);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Allocate the instance.
     */
    PAUDMIXSTREAM pMixStream = (PAUDMIXSTREAM)RTMemAllocZ(sizeof(AUDMIXSTREAM));
    AssertReturn(pMixStream, VERR_NO_MEMORY);

    pMixStream->fFlags = fFlags;

    /* Assign the backend's name to the mixer stream's name for easier identification in the (release) log. */
    pMixStream->pszName = RTStrAPrintf2("[%s] %s", pCfg->szName, BackendCfg.szName);
    pMixStream->pszStatPrefix = RTStrAPrintf2("MixerSink-%s/%s/", pSink->pszName, BackendCfg.szName);
    if (pMixStream->pszName && pMixStream->pszStatPrefix)
    {
        rc = RTCritSectInit(&pMixStream->CritSect);
        if (RT_SUCCESS(rc))
        {
            /*
             * Lock the sink so we can safely get it's properties and call
             * down into the audio driver to create that end of the stream.
             */
            rc = RTCritSectEnter(&pSink->CritSect);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                LogFlowFunc(("[%s] fFlags=0x%x (enmDir=%ld, %u bits, %RU8 channels, %RU32Hz)\n", pSink->pszName, fFlags, pCfg->enmDir,
                             PDMAudioPropsSampleBits(&pCfg->Props), PDMAudioPropsChannels(&pCfg->Props), pCfg->Props.uHz));

                /*
                 * Initialize the host-side configuration for the stream to be created.
                 * Always use the sink's PCM audio format as the host side when creating a stream for it.
                 */
                AssertMsg(AudioHlpPcmPropsAreValid(&pSink->PCMProps),
                          ("%s: Does not (yet) have a format set when it must\n", pSink->pszName));

                PDMAUDIOSTREAMCFG CfgHost;
                rc = PDMAudioStrmCfgInitWithProps(&CfgHost, &pSink->PCMProps);
                AssertRC(rc); /* cannot fail */

                /* Apply the sink's direction for the configuration to use to create the stream. */
                if (pSink->enmDir == AUDMIXSINKDIR_INPUT)
                {
                    CfgHost.enmDir      = PDMAUDIODIR_IN;
                    CfgHost.u.enmSrc    = pCfg->u.enmSrc;
                    CfgHost.enmLayout   = pCfg->enmLayout;
                }
                else
                {
                    CfgHost.enmDir      = PDMAUDIODIR_OUT;
                    CfgHost.u.enmDst    = pCfg->u.enmDst;
                    CfgHost.enmLayout   = pCfg->enmLayout;
                }

                RTStrCopy(CfgHost.szName, sizeof(CfgHost.szName), pCfg->szName);

                /*
                 * Create the stream.
                 *
                 * Output streams are not using any mixing buffers in DrvAudio.  This will
                 * become the norm after we move the input mixing here and convert DevSB16
                 * to use this mixer code too.
                 */
                PPDMAUDIOSTREAM pStream;
                rc = pConn->pfnStreamCreate(pConn, pSink->enmDir == AUDMIXSINKDIR_OUTPUT ? PDMAUDIOSTREAM_CREATE_F_NO_MIXBUF : 0,
                                            &CfgHost, pCfg, &pStream);
                if (RT_SUCCESS(rc))
                {
                    /* Set up the mixing buffer conversion state. */
                    if (pSink->enmDir == AUDMIXSINKDIR_OUTPUT)
                        rc = AudioMixBufInitPeekState(&pSink->MixBuf, &pMixStream->PeekState, &pStream->Props);
                    if (RT_SUCCESS(rc))
                    {
                        /* Save the audio stream pointer to this mixing stream. */
                        pMixStream->pStream = pStream;

                        /* Increase the stream's reference count to let others know
                         * we're reyling on it to be around now. */
                        pConn->pfnStreamRetain(pConn, pStream);
                        pMixStream->pConn = pConn;

                        RTCritSectLeave(&pSink->CritSect);

                        if (ppStream)
                            *ppStream = pMixStream;
                        return VINF_SUCCESS;
                    }

                    rc = pConn->pfnStreamDestroy(pConn, pStream);
                }

                /*
                 * Failed.  Tear down the stream.
                 */
                int rc2 = RTCritSectLeave(&pSink->CritSect);
                AssertRC(rc2);
            }
            RTCritSectDelete(&pMixStream->CritSect);
        }
    }
    else
        rc = VERR_NO_STR_MEMORY;

    RTStrFree(pMixStream->pszStatPrefix);
    pMixStream->pszStatPrefix = NULL;
    RTStrFree(pMixStream->pszName);
    pMixStream->pszName = NULL;
    RTMemFree(pMixStream);
    return rc;
}

/**
 * Static helper function to translate a sink command
 * to a PDM audio stream command.
 *
 * @returns PDM audio stream command, or PDMAUDIOSTREAMCMD_UNKNOWN if not found.
 * @param   enmCmd              Mixer sink command to translate.
 */
static PDMAUDIOSTREAMCMD audioMixerSinkToStreamCmd(AUDMIXSINKCMD enmCmd)
{
    switch (enmCmd)
    {
        case AUDMIXSINKCMD_ENABLE:   return PDMAUDIOSTREAMCMD_ENABLE;
        case AUDMIXSINKCMD_DISABLE:  return PDMAUDIOSTREAMCMD_DISABLE;
        case AUDMIXSINKCMD_PAUSE:    return PDMAUDIOSTREAMCMD_PAUSE;
        case AUDMIXSINKCMD_RESUME:   return PDMAUDIOSTREAMCMD_RESUME;
        default:                     break;
    }

    AssertMsgFailed(("Unsupported sink command %d\n", enmCmd));
    return PDMAUDIOSTREAMCMD_INVALID;
}

/**
 * Controls a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink               Mixer sink to control.
 * @param   enmSinkCmd          Sink command to set.
 */
int AudioMixerSinkCtl(PAUDMIXSINK pSink, AUDMIXSINKCMD enmSinkCmd)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);

    PDMAUDIOSTREAMCMD enmCmdStream = audioMixerSinkToStreamCmd(enmSinkCmd);
    if (enmCmdStream == PDMAUDIOSTREAMCMD_INVALID)
        return VERR_NOT_SUPPORTED;

    int rc = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    /* Input sink and no recording source set? Bail out early. */
    if (   pSink->enmDir == AUDMIXSINKDIR_INPUT
        && pSink->In.pStreamRecSource == NULL)
    {
        int rc2 = RTCritSectLeave(&pSink->CritSect);
        AssertRC(rc2);

        return rc;
    }

    PAUDMIXSTREAM pStream;
    if (   pSink->enmDir == AUDMIXSINKDIR_INPUT
        && pSink->In.pStreamRecSource) /* Any recording source set? */
    {
        RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
        {
            if (pStream == pSink->In.pStreamRecSource)
            {
                int rc2 = audioMixerStreamCtlInternal(pStream, enmCmdStream, AUDMIXSTRMCTL_F_NONE);
                if (rc2 == VERR_NOT_SUPPORTED)
                    rc2 = VINF_SUCCESS;

                if (RT_SUCCESS(rc))
                    rc = rc2;
                /* Keep going. Flag? */
            }
        }
    }
    else if (pSink->enmDir == AUDMIXSINKDIR_OUTPUT)
    {
        RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
        {
            int rc2 = audioMixerStreamCtlInternal(pStream, enmCmdStream, AUDMIXSTRMCTL_F_NONE);
            if (rc2 == VERR_NOT_SUPPORTED)
                rc2 = VINF_SUCCESS;

            if (RT_SUCCESS(rc))
                rc = rc2;
            /* Keep going. Flag? */
        }
    }

    switch (enmSinkCmd)
    {
        case AUDMIXSINKCMD_ENABLE:
        {
            /* Make sure to clear any other former flags again by assigning AUDMIXSINK_STS_RUNNING directly. */
            pSink->fStatus = AUDMIXSINK_STS_RUNNING;
            break;
        }

        case AUDMIXSINKCMD_DISABLE:
        {
            if (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
            {
                /* Set the sink in a pending disable state first.
                 * The final status (disabled) will be set in the sink's iteration. */
                pSink->fStatus |= AUDMIXSINK_STS_PENDING_DISABLE;
            }
            break;
        }

        default:
            AssertFailedStmt(rc = VERR_NOT_IMPLEMENTED);
            break;
    }

#if defined(RTLOG_REL_ENABLED) || defined(LOG_ENABLED)
    char szStatus[AUDIOMIXERSINK_STATUS_STR_MAX];
#endif
    LogRel2(("Audio Mixer: Set new status of sink '%s': %s (enmCmd=%RU32 rc=%Rrc)\n",
             pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus), enmSinkCmd, rc));

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return rc;
}

/**
 * Initializes a sink.
 *
 * @returns VBox status code.
 * @param   pSink               Sink to initialize.
 * @param   pMixer              Mixer the sink is assigned to.
 * @param   pcszName            Name of the sink.
 * @param   enmDir              Direction of the sink.
 */
static int audioMixerSinkInit(PAUDMIXSINK pSink, PAUDIOMIXER pMixer, const char *pcszName, AUDMIXSINKDIR enmDir)
{
    pSink->pszName = RTStrDup(pcszName);
    if (!pSink->pszName)
        return VERR_NO_MEMORY;

    int rc = RTCritSectInit(&pSink->CritSect);
    if (RT_SUCCESS(rc))
    {
        pSink->pParent  = pMixer;
        pSink->enmDir   = enmDir;

        RTListInit(&pSink->lstStreams);

        /* Set initial volume to max. */
        pSink->Volume.fMuted = false;
        pSink->Volume.uLeft  = PDMAUDIO_VOLUME_MAX;
        pSink->Volume.uRight = PDMAUDIO_VOLUME_MAX;

        /* Ditto for the combined volume. */
        pSink->VolumeCombined.fMuted = false;
        pSink->VolumeCombined.uLeft  = PDMAUDIO_VOLUME_MAX;
        pSink->VolumeCombined.uRight = PDMAUDIO_VOLUME_MAX;

        const size_t cbScratchBuf = _1K; /** @todo Make this configurable? */

        pSink->pabScratchBuf = (uint8_t *)RTMemAlloc(cbScratchBuf);
        AssertPtrReturn(pSink->pabScratchBuf, VERR_NO_MEMORY);
        pSink->cbScratchBuf  = cbScratchBuf;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Destroys a mixer sink and removes it from the attached mixer (if any).
 *
 * @param   pSink       Mixer sink to destroy.
 * @param   pDevIns     The device instance that statistics are registered with.
 */
void AudioMixerSinkDestroy(PAUDMIXSINK pSink, PPDMDEVINS pDevIns)
{
    if (!pSink)
        return;

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    AssertRC(rc2);

    if (pSink->pParent)
    {
        /* Save mixer pointer, as after audioMixerRemoveSinkInternal() the
         * pointer will be gone from the stream. */
        PAUDIOMIXER pMixer = pSink->pParent;
        AssertPtr(pMixer);

        audioMixerRemoveSinkInternal(pMixer, pSink);
    }

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    audioMixerSinkDestroyInternal(pSink, pDevIns);

    RTMemFree(pSink);
    pSink = NULL;
}

/**
 * Destroys a mixer sink.
 *
 * @param   pSink       Mixer sink to destroy.
 * @param   pDevIns     The device instance statistics are registered with.
 */
static void audioMixerSinkDestroyInternal(PAUDMIXSINK pSink, PPDMDEVINS pDevIns)
{
    AssertPtrReturnVoid(pSink);

    LogFunc(("%s\n", pSink->pszName));

    PAUDMIXSTREAM pStream, pStreamNext;
    RTListForEachSafe(&pSink->lstStreams, pStream, pStreamNext, AUDMIXSTREAM, Node)
    {
        audioMixerSinkRemoveStreamInternal(pSink, pStream);
        audioMixerStreamDestroyInternal(pStream, pDevIns);
    }

    if (   pSink->pParent
        && pSink->pParent->fFlags & AUDMIXER_FLAGS_DEBUG)
    {
        AudioHlpFileDestroy(pSink->Dbg.pFile);
        pSink->Dbg.pFile = NULL;
    }

    char szPrefix[128];
    RTStrPrintf(szPrefix, sizeof(szPrefix), "MixerSink-%s/", pSink->pszName);
    PDMDevHlpSTAMDeregisterByPrefix(pDevIns, szPrefix);

    RTStrFree(pSink->pszName);
    pSink->pszName = NULL;

    RTMemFree(pSink->pabScratchBuf);
    pSink->pabScratchBuf = NULL;
    pSink->cbScratchBuf = 0;

    AudioMixBufDestroy(&pSink->MixBuf);
    RTCritSectDelete(&pSink->CritSect);
}

/**
 * Returns the amount of bytes ready to be read from a sink since the last call
 * to AudioMixerSinkUpdate().
 *
 * @returns Amount of bytes ready to be read from the sink.
 * @param   pSink               Sink to return number of available bytes for.
 */
uint32_t AudioMixerSinkGetReadable(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, 0);

    AssertMsg(pSink->enmDir == AUDMIXSINKDIR_INPUT, ("%s: Can't read from a non-input sink\n", pSink->pszName));

    int rc = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc))
        return 0;

    uint32_t cbReadable = 0;

    if (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
    {
#ifdef VBOX_AUDIO_MIXER_WITH_MIXBUF_IN
# error "Implement me!"
#else
        PAUDMIXSTREAM pStreamRecSource = pSink->In.pStreamRecSource;
        if (!pStreamRecSource)
        {
            Log3Func(("[%s] No recording source specified, skipping ...\n", pSink->pszName));
        }
        else
        {
            AssertPtr(pStreamRecSource->pConn);
            cbReadable = pStreamRecSource->pConn->pfnStreamGetReadable(pStreamRecSource->pConn, pStreamRecSource->pStream);
        }
#endif
    }

    Log3Func(("[%s] cbReadable=%RU32\n", pSink->pszName, cbReadable));

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return cbReadable;
}

/**
 * Returns the sink's current recording source.
 *
 * @return  Mixer stream which currently is set as current recording source, NULL if none is set.
 * @param   pSink               Audio mixer sink to return current recording source for.
 */
PAUDMIXSTREAM AudioMixerSinkGetRecordingSource(PAUDMIXSINK pSink)
{
    int rc = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc))
        return NULL;

    AssertMsg(pSink->enmDir == AUDMIXSINKDIR_INPUT, ("Specified sink is not an input sink\n"));

    PAUDMIXSTREAM pStream = pSink->In.pStreamRecSource;

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return pStream;
}

/**
 * Returns the amount of bytes ready to be written to a sink since the last call
 * to AudioMixerSinkUpdate().
 *
 * @returns Amount of bytes ready to be written to the sink.
 * @param   pSink               Sink to return number of available bytes for.
 */
uint32_t AudioMixerSinkGetWritable(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, 0);

    AssertMsg(pSink->enmDir == AUDMIXSINKDIR_OUTPUT, ("%s: Can't write to a non-output sink\n", pSink->pszName));

    int rc = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc))
        return 0;

    uint32_t cbWritable = 0;

    if (    (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
        && !(pSink->fStatus & AUDMIXSINK_STS_PENDING_DISABLE))
    {
        cbWritable = AudioMixBufFreeBytes(&pSink->MixBuf);
    }

    Log3Func(("[%s] cbWritable=%RU32 (%RU64ms)\n",
              pSink->pszName, cbWritable, PDMAudioPropsBytesToMilli(&pSink->PCMProps, cbWritable)));

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return cbWritable;
}

/**
 * Returns the sink's mixing direction.
 *
 * @returns Mixing direction.
 * @param   pSink               Sink to return direction for.
 */
AUDMIXSINKDIR AudioMixerSinkGetDir(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, AUDMIXSINKDIR_UNKNOWN);

    int rc = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc))
        return AUDMIXSINKDIR_UNKNOWN;

    const AUDMIXSINKDIR enmDir = pSink->enmDir;

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return enmDir;
}

/**
 * Returns the sink's (friendly) name.
 *
 * @returns The sink's (friendly) name.
 */
const char *AudioMixerSinkGetName(const PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, "<Unknown>");

    return pSink->pszName;
}

/**
 * Returns a specific mixer stream from a sink, based on its index.
 *
 * @returns Mixer stream if found, or NULL if not found.
 * @param   pSink               Sink to retrieve mixer stream from.
 * @param   uIndex              Index of the mixer stream to return.
 */
PAUDMIXSTREAM AudioMixerSinkGetStream(PAUDMIXSINK pSink, uint8_t uIndex)
{
    AssertPtrReturn(pSink, NULL);

    int rc = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc))
        return NULL;

    AssertMsgReturn(uIndex < pSink->cStreams,
                    ("Index %RU8 exceeds stream count (%RU8)", uIndex, pSink->cStreams), NULL);

    /* Slow lookup, d'oh. */
    PAUDMIXSTREAM pStream = RTListGetFirst(&pSink->lstStreams, AUDMIXSTREAM, Node);
    while (uIndex)
    {
        pStream = RTListGetNext(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node);
        uIndex--;
    }

    /** @todo Do we need to raise the stream's reference count here? */

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    AssertPtr(pStream);
    return pStream;
}

/**
 * Returns the current status of a mixer sink.
 *
 * @returns The sink's current status.
 * @param   pSink               Mixer sink to return status for.
 */
AUDMIXSINKSTS AudioMixerSinkGetStatus(PAUDMIXSINK pSink)
{
    if (!pSink)
        return AUDMIXSINK_STS_NONE;

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc2))
        return AUDMIXSINK_STS_NONE;

    /* If the dirty flag is set, there is unprocessed data in the sink. */
    AUDMIXSINKSTS stsSink = pSink->fStatus;

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return stsSink;
}

/**
 * Returns the number of attached mixer streams to a mixer sink.
 *
 * @returns The number of attached mixer streams.
 * @param   pSink               Mixer sink to return number for.
 */
uint8_t AudioMixerSinkGetStreamCount(PAUDMIXSINK pSink)
{
    if (!pSink)
        return 0;

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc2))
        return 0;

    const uint8_t cStreams = pSink->cStreams;

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return cStreams;
}

/**
 * Returns whether the sink is in an active state or not.
 * Note: The pending disable state also counts as active.
 *
 * @returns True if active, false if not.
 * @param   pSink               Sink to return active state for.
 */
bool AudioMixerSinkIsActive(PAUDMIXSINK pSink)
{
    if (!pSink)
        return false;

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc2))
        return false;

    const bool fIsActive = pSink->fStatus & AUDMIXSINK_STS_RUNNING;
    /* Note: AUDMIXSINK_STS_PENDING_DISABLE implies AUDMIXSINK_STS_RUNNING. */

    Log3Func(("[%s] fActive=%RTbool\n", pSink->pszName, fIsActive));

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return fIsActive;
}

/**
 * Reads audio data from a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink               Mixer sink to read data from.
 * @param   enmOp               Mixer operation to use for reading the data.
 * @param   pvBuf               Buffer where to store the read data.
 * @param   cbBuf               Buffer size (in bytes) where to store the data.
 * @param   pcbRead             Number of bytes read. Optional.
 */
int AudioMixerSinkRead(PAUDMIXSINK pSink, AUDMIXOP enmOp, void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    RT_NOREF(enmOp);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf,    VERR_INVALID_PARAMETER);
    /* pcbRead is optional. */

    /** @todo Handle mixing operation enmOp! */

    int rc = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    AssertMsg(pSink->enmDir == AUDMIXSINKDIR_INPUT,
              ("Can't read from a sink which is not an input sink\n"));

    uint32_t cbRead = 0;

    /* Flag indicating whether this sink is in a 'clean' state,
     * e.g. there is no more data to read from. */
    bool fClean = true;

    PAUDMIXSTREAM pStreamRecSource = pSink->In.pStreamRecSource;
    if (!pStreamRecSource)
    {
        Log3Func(("[%s] No recording source specified, skipping ...\n", pSink->pszName));
    }
    else if (!(pStreamRecSource->fStatus & AUDMIXSTREAM_STATUS_ENABLED))
    {
        Log3Func(("[%s] Stream '%s' disabled, skipping ...\n", pSink->pszName, pStreamRecSource->pszName));
    }
    else
    {
        uint32_t cbToRead = cbBuf;
        while (cbToRead)
        {
            uint32_t cbReadStrm;
            AssertPtr(pStreamRecSource->pConn);
#ifdef VBOX_AUDIO_MIXER_WITH_MIXBUF_IN
# error "Implement me!"
#else
            rc = pStreamRecSource->pConn->pfnStreamRead(pStreamRecSource->pConn, pStreamRecSource->pStream,
                                                        (uint8_t *)pvBuf + cbRead, cbToRead, &cbReadStrm);
#endif
            if (RT_FAILURE(rc))
                LogFunc(("[%s] Failed reading from stream '%s': %Rrc\n", pSink->pszName, pStreamRecSource->pszName, rc));

            Log3Func(("[%s] Stream '%s': Read %RU32 bytes\n", pSink->pszName, pStreamRecSource->pszName, cbReadStrm));

            if (   RT_FAILURE(rc)
                || !cbReadStrm)
                break;

            AssertBreakStmt(cbReadStrm <= cbToRead, rc = VERR_BUFFER_OVERFLOW);
            cbToRead -= cbReadStrm;
            cbRead   += cbReadStrm;
            Assert(cbRead <= cbBuf);
        }

        uint32_t cbReadable = pStreamRecSource->pConn->pfnStreamGetReadable(pStreamRecSource->pConn, pStreamRecSource->pStream);

        /* Still some data available? Then sink is not clean (yet). */
        if (cbReadable)
            fClean = false;

        if (RT_SUCCESS(rc))
        {
            if (fClean)
                pSink->fStatus &= ~AUDMIXSINK_STS_DIRTY;

            /* Update our last read time stamp. */
            pSink->tsLastReadWrittenNs = RTTimeNanoTS();

            if (pSink->pParent->fFlags & AUDMIXER_FLAGS_DEBUG)
            {
                int rc2 = AudioHlpFileWrite(pSink->Dbg.pFile, pvBuf, cbRead, 0 /* fFlags */);
                AssertRC(rc2);
            }
        }
    }

#ifdef LOG_ENABLED
    char szStatus[AUDIOMIXERSINK_STATUS_STR_MAX];
#endif
    Log2Func(("[%s] cbRead=%RU32, fClean=%RTbool, fStatus=%s, rc=%Rrc\n",
              pSink->pszName, cbRead, fClean, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus), rc));

    if (pcbRead)
        *pcbRead = cbRead;

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return rc;
}

/**
 * Removes a mixer stream from a mixer sink, internal version.
 *
 * @returns VBox status code.
 * @param   pSink               Sink to remove mixer stream from.
 * @param   pStream             Stream to remove.
 */
static int audioMixerSinkRemoveStreamInternal(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream)
{
    AssertPtrReturn(pSink, VERR_INVALID_PARAMETER);
    if (   !pStream
        || !pStream->pSink) /* Not part of a sink anymore? */
    {
        return VERR_NOT_FOUND;
    }

    AssertMsgReturn(pStream->pSink == pSink, ("Stream '%s' is not part of sink '%s'\n",
                                              pStream->pszName, pSink->pszName), VERR_NOT_FOUND);

    LogFlowFunc(("[%s] (Stream = %s), cStreams=%RU8\n",
                 pSink->pszName, pStream->pStream->szName, pSink->cStreams));

    /* Remove stream from sink. */
    RTListNodeRemove(&pStream->Node);

    int rc = VINF_SUCCESS;

    if (pSink->enmDir == AUDMIXSINKDIR_INPUT)
    {
        /* Make sure to also un-set the recording source if this stream was set
         * as the recording source before. */
        if (pStream == pSink->In.pStreamRecSource)
            rc = audioMixerSinkSetRecSourceInternal(pSink, NULL);
    }

    /* Set sink to NULL so that we know we're not part of any sink anymore. */
    pStream->pSink = NULL;

    return rc;
}

/**
 * Removes a mixer stream from a mixer sink.
 *
 * @param   pSink               Sink to remove mixer stream from.
 * @param   pStream             Stream to remove.
 */
void AudioMixerSinkRemoveStream(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream)
{
    int rc2 = RTCritSectEnter(&pSink->CritSect);
    AssertRC(rc2);

    rc2 = audioMixerSinkRemoveStreamInternal(pSink, pStream);
    if (RT_SUCCESS(rc2))
    {
        Assert(pSink->cStreams);
        pSink->cStreams--;
    }

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);
}

/**
 * Removes all attached streams from a given sink.
 *
 * @param pSink                 Sink to remove attached streams from.
 */
static void audioMixerSinkRemoveAllStreamsInternal(PAUDMIXSINK pSink)
{
    if (!pSink)
        return;

    LogFunc(("%s\n", pSink->pszName));

    PAUDMIXSTREAM pStream, pStreamNext;
    RTListForEachSafe(&pSink->lstStreams, pStream, pStreamNext, AUDMIXSTREAM, Node)
        audioMixerSinkRemoveStreamInternal(pSink, pStream);
}

/**
 * Resets the sink's state.
 *
 * @param   pSink               Sink to reset.
 */
static void audioMixerSinkReset(PAUDMIXSINK pSink)
{
    if (!pSink)
        return;

    LogFunc(("[%s]\n", pSink->pszName));

    AudioMixBufReset(&pSink->MixBuf);

    /* Update last updated timestamp. */
    pSink->tsLastUpdatedMs = 0;

    /* Reset status. */
    pSink->fStatus = AUDMIXSINK_STS_NONE;
}

/**
 * Removes all attached streams from a given sink.
 *
 * @param pSink                 Sink to remove attached streams from.
 */
void AudioMixerSinkRemoveAllStreams(PAUDMIXSINK pSink)
{
    if (!pSink)
        return;

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    AssertRC(rc2);

    audioMixerSinkRemoveAllStreamsInternal(pSink);

    pSink->cStreams = 0;

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);
}

/**
 * Resets a sink. This will immediately stop all processing.
 *
 * @param pSink                 Sink to reset.
 */
void AudioMixerSinkReset(PAUDMIXSINK pSink)
{
    if (!pSink)
        return;

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    AssertRC(rc2);

    LogFlowFunc(("[%s]\n", pSink->pszName));

    audioMixerSinkReset(pSink);

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);
}

/**
 * Returns the audio format of a mixer sink.
 *
 * @param   pSink               Sink to retrieve audio format for.
 * @param   pPCMProps           Where to the returned audio format.
 */
void AudioMixerSinkGetFormat(PAUDMIXSINK pSink, PPDMAUDIOPCMPROPS pPCMProps)
{
    AssertPtrReturnVoid(pSink);
    AssertPtrReturnVoid(pPCMProps);

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc2))
        return;

    memcpy(pPCMProps, &pSink->PCMProps, sizeof(PDMAUDIOPCMPROPS));

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);
}

/**
 * Sets the audio format of a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink       The sink to set audio format for.
 * @param   pPCMProps   Audio format (PCM properties) to set.
 */
int AudioMixerSinkSetFormat(PAUDMIXSINK pSink, PCPDMAUDIOPCMPROPS pPCMProps)
{
    AssertPtrReturn(pSink,     VERR_INVALID_POINTER);
    AssertPtrReturn(pPCMProps, VERR_INVALID_POINTER);
    AssertReturn(AudioHlpPcmPropsAreValid(pPCMProps), VERR_INVALID_PARAMETER);

    int rc = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    if (PDMAudioPropsAreEqual(&pSink->PCMProps, pPCMProps)) /* Bail out early if PCM properties are equal. */
    {
        rc = RTCritSectLeave(&pSink->CritSect);
        AssertRC(rc);

        return rc;
    }

    if (pSink->PCMProps.uHz)
        LogFlowFunc(("[%s] Old format: %u bit, %RU8 channels, %RU32Hz\n", pSink->pszName,
                     PDMAudioPropsSampleBits(&pSink->PCMProps), PDMAudioPropsChannels(&pSink->PCMProps), pSink->PCMProps.uHz));

    memcpy(&pSink->PCMProps, pPCMProps, sizeof(PDMAUDIOPCMPROPS));

    LogFlowFunc(("[%s] New format %u bit, %RU8 channels, %RU32Hz\n", pSink->pszName, PDMAudioPropsSampleBits(&pSink->PCMProps),
                 PDMAudioPropsChannels(&pSink->PCMProps), pSink->PCMProps.uHz));

    /* Also update the sink's mixing buffer format. */
    AudioMixBufDestroy(&pSink->MixBuf);
    rc = AudioMixBufInit(&pSink->MixBuf, pSink->pszName, &pSink->PCMProps,
                         PDMAudioPropsMilliToFrames(&pSink->PCMProps, 100 /*ms*/)); /** @todo Make this configurable? */
    if (RT_SUCCESS(rc))
    {
        PAUDMIXSTREAM pStream;
        RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
        {
            /** @todo Invalidate mix buffers! */
        }
    }

    if (   RT_SUCCESS(rc)
        && (pSink->pParent->fFlags & AUDMIXER_FLAGS_DEBUG))
    {
        AudioHlpFileClose(pSink->Dbg.pFile);

        char szName[64];
        RTStrPrintf(szName, sizeof(szName), "MixerSink-%s", pSink->pszName);

        char szFile[RTPATH_MAX];
        int rc2 = AudioHlpFileNameGet(szFile, RT_ELEMENTS(szFile), NULL /* Use temporary directory */, szName,
                                      0 /* Instance */, AUDIOHLPFILETYPE_WAV, AUDIOHLPFILENAME_FLAGS_NONE);
        if (RT_SUCCESS(rc2))
        {
            rc2 = AudioHlpFileCreate(AUDIOHLPFILETYPE_WAV, szFile, AUDIOHLPFILE_FLAGS_NONE, &pSink->Dbg.pFile);
            if (RT_SUCCESS(rc2))
                rc2 = AudioHlpFileOpen(pSink->Dbg.pFile, AUDIOHLPFILE_DEFAULT_OPEN_FLAGS, &pSink->PCMProps);
        }
    }

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Set the current recording source of an input mixer sink, internal version.
 *
 * @returns VBox status code.
 * @param   pSink               Input mixer sink to set recording source for.
 * @param   pStream             Mixer stream to set as current recording source. Must be an input stream.
 *                              Specify NULL to un-set the current recording source.
 */
static int audioMixerSinkSetRecSourceInternal(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream)
{
    AssertMsg(pSink->enmDir == AUDMIXSINKDIR_INPUT, ("Specified sink is not an input sink\n"));

    int rc;

    /*
     * Warning: Do *not* use pfnConn->pfnEnable() for enabling/disabling streams here, as this will unconditionally (re-)enable
     *          streams, which would violate / run against the (global) VM settings. See @bugref{9882}.
     */

    /* Get pointers of current recording source to make code easier to read below. */
    PAUDMIXSTREAM       pCurRecSrc       = pSink->In.pStreamRecSource; /* Can be NULL. */
    PPDMIAUDIOCONNECTOR pCurRecSrcConn   = NULL;
    PPDMAUDIOSTREAM     pCurRecSrcStream = NULL;

    if (pCurRecSrc) /* First, disable old recording source, if any is set. */
    {
        pCurRecSrcConn   = pSink->In.pStreamRecSource->pConn;
        AssertPtrReturn(pCurRecSrcConn, VERR_INVALID_POINTER);
        pCurRecSrcStream = pCurRecSrc->pStream;
        AssertPtrReturn(pCurRecSrcStream, VERR_INVALID_POINTER);

        rc = pCurRecSrcConn->pfnStreamControl(pCurRecSrcConn, pCurRecSrcStream, PDMAUDIOSTREAMCMD_DISABLE);
    }
    else
        rc = VINF_SUCCESS;

    if (RT_SUCCESS(rc))
    {
        if (pStream)
        {
            AssertPtr(pStream->pStream);
            AssertMsg(pStream->pStream->enmDir == PDMAUDIODIR_IN, ("Specified stream is not an input stream\n"));
            AssertPtr(pStream->pConn);
            rc = pStream->pConn->pfnStreamControl(pStream->pConn, pStream->pStream, PDMAUDIOSTREAMCMD_ENABLE);
            if (RT_SUCCESS(rc))
            {
                pCurRecSrc = pStream;
            }
            else if (pCurRecSrc) /* Stay with the current recording source (if any) and re-enable it. */
            {
                rc = pCurRecSrcConn->pfnStreamControl(pCurRecSrcConn, pCurRecSrcStream, PDMAUDIOSTREAMCMD_ENABLE);
            }
        }
        else
            pCurRecSrc = NULL; /* Unsetting, see audioMixerSinkRemoveStreamInternal. */
    }

    /* Invalidate pointers. */
    pSink->In.pStreamRecSource = pCurRecSrc;

    LogFunc(("[%s] Recording source is now '%s', rc=%Rrc\n",
             pSink->pszName, pSink->In.pStreamRecSource ? pSink->In.pStreamRecSource->pszName : "<None>", rc));

    if (RT_SUCCESS(rc))
        LogRel(("Audio Mixer: Setting recording source of sink '%s' to '%s'\n",
                pSink->pszName, pSink->In.pStreamRecSource ? pSink->In.pStreamRecSource->pszName : "<None>"));
    else if (rc != VERR_AUDIO_STREAM_NOT_READY)
        LogRel(("Audio Mixer: Setting recording source of sink '%s' to '%s' failed with %Rrc\n",
                pSink->pszName, pSink->In.pStreamRecSource ? pSink->In.pStreamRecSource->pszName : "<None>", rc));

    return rc;
}

/**
 * Set the current recording source of an input mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink               Input mixer sink to set recording source for.
 * @param   pStream             Mixer stream to set as current recording source. Must be an input stream.
 *                              Set to NULL to un-set the current recording source.
 */
int AudioMixerSinkSetRecordingSource(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    rc = audioMixerSinkSetRecSourceInternal(pSink, pStream);

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return rc;
}

/**
 * Sets the volume of an individual sink.
 *
 * @returns VBox status code.
 * @param   pSink               Sink to set volume for.
 * @param   pVol                Volume to set.
 */
int AudioMixerSinkSetVolume(PAUDMIXSINK pSink, PPDMAUDIOVOLUME pVol)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    AssertPtrReturn(pVol,  VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pSink->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    memcpy(&pSink->Volume, pVol, sizeof(PDMAUDIOVOLUME));

    LogRel2(("Audio Mixer: Setting volume of sink '%s' to %RU8/%RU8 (%s)\n",
             pSink->pszName, pVol->uLeft, pVol->uRight, pVol->fMuted ? "Muted" : "Unmuted"));

    AssertPtr(pSink->pParent);
    rc = audioMixerSinkUpdateVolume(pSink, &pSink->pParent->VolMaster);

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return rc;
}

/**
 * Updates an input mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink               Mixer sink to update.
 */
static int audioMixerSinkUpdateInput(PAUDMIXSINK pSink)
{
    /*
     * Warning!  We currently do _not_ use the mixing buffer for input streams!
     * Warning!  We currently do _not_ use the mixing buffer for input streams!
     * Warning!  We currently do _not_ use the mixing buffer for input streams!
     */

    /*
     * Skip input sinks without a recoring source.
     */
    if (pSink->In.pStreamRecSource == NULL)
        return VINF_SUCCESS;

    /*
     * Update each mixing sink stream's status.
     */
    PAUDMIXSTREAM pMixStream;
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
    {
        int rc2 = audioMixerStreamUpdateStatus(pMixStream);
        AssertRC(rc2);
    }

    /*
     * Iterate and do capture on the recording source.  We ignore all other streams.
     */
    int rc = VINF_SUCCESS; /* not sure if error propagation is worth it... */
#if 1
    pMixStream = pSink->In.pStreamRecSource;
#else
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
#endif
    {
        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
        {
            uint32_t cFramesCaptured = 0;
            int rc2 = pMixStream->pConn->pfnStreamIterate(pMixStream->pConn, pMixStream->pStream);
            if (RT_SUCCESS(rc2))
            {
                rc2 = pMixStream->pConn->pfnStreamCapture(pMixStream->pConn, pMixStream->pStream, &cFramesCaptured);
                if (RT_SUCCESS(rc2))
                {
                    if (cFramesCaptured)
                        pSink->fStatus |= AUDMIXSINK_STS_DIRTY;
                }
                else
                {
                    LogFunc(("%s: Failed capturing stream '%s', rc=%Rrc\n", pSink->pszName, pMixStream->pStream->szName, rc2));
                    if (RT_SUCCESS(rc))
                        rc = rc2;
                }
            }
            else if (RT_SUCCESS(rc))
                rc = rc2;
            Log3Func(("%s: cFramesCaptured=%RU32 (rc2=%Rrc)\n", pMixStream->pStream->szName, cFramesCaptured, rc2));
        }
    }

    /* Update last updated timestamp. */
    pSink->tsLastUpdatedMs = RTTimeMilliTS();

    /*
     * Deal with pending disable.  The general case is that we reset
     * the sink when all streams have been disabled, however input is
     * currently a special case where we only care about the one
     * recording source...
     */
    if (pSink->fStatus & AUDMIXSINK_STS_PENDING_DISABLE)
    {
#if 1
        uint32_t const  cStreams         = 1;
        uint32_t        cStreamsDisabled = 1;
        pMixStream = pSink->In.pStreamRecSource;
#else
        uint32_t const  cStreams         = pSink->cStreams;
        uint32_t        cStreamsDisabled = pSink->cStreams;
        RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
#endif
        {
            if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
            {
                PDMAUDIOSTREAMSTS const fSts = pMixStream->pConn->pfnStreamGetStatus(pMixStream->pConn, pMixStream->pStream);
                if (fSts & (PDMAUDIOSTREAMSTS_FLAGS_ENABLED | PDMAUDIOSTREAMSTS_FLAGS_PENDING_DISABLE))
                    cStreamsDisabled--;
            }
        }
        Log3Func(("[%s] pending disable: %u of %u disabled\n", pSink->pszName, cStreamsDisabled, cStreams));
        if (cStreamsDisabled == cStreams)
            audioMixerSinkReset(pSink);
    }

    return rc;
}

/**
 * Updates an output mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink               Mixer sink to update.
 */
static int audioMixerSinkUpdateOutput(PAUDMIXSINK pSink)
{
    /*
     * Update each mixing sink stream's status and check how much we can
     * write into them.
     *
     * We're currently using the minimum size of all streams, however this
     * isn't a smart approach as it means one disfunctional stream can block
     * working ones.
     */
    /** @todo rework this so a broken stream cannot hold up everyone. */
    uint32_t      cFramesToRead    = AudioMixBufLive(&pSink->MixBuf); /* (to read from the mixing buffer) */
    uint32_t      cWritableStreams = 0;
    PAUDMIXSTREAM pMixStream;
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
    {
#if 0 /** @todo this conceptually makes sense, but may mess up the pending-disable logic ... */
        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
            pConn->pfnStreamIterate(pConn, pStream);
#endif

        int rc2 = audioMixerStreamUpdateStatus(pMixStream);
        AssertRC(rc2);

        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
        {
            uint32_t const cbWritable = pMixStream->pConn->pfnStreamGetWritable(pMixStream->pConn, pMixStream->pStream);
            uint32_t cFrames = PDMAudioPropsBytesToFrames(&pMixStream->pStream->Props, cbWritable);
            if (PDMAudioPropsHz(&pMixStream->pStream->Props) == PDMAudioPropsHz(&pSink->MixBuf.Props))
            { /* likely */ }
            else
            {
                cFrames = cFrames * PDMAudioPropsHz(&pSink->MixBuf.Props) / PDMAudioPropsHz(&pMixStream->pStream->Props);
                cFrames = cFrames > 2 ? cFrames - 2 : 0; /* rounding safety fudge */
            }
            if (cFramesToRead > cFrames)
            {
                Log4Func(("%s: cFramesToRead %u -> %u; %s (%u bytes writable)\n",
                          pSink->pszName, cFramesToRead, cFrames, pMixStream->pszName, cbWritable));
                cFramesToRead = cFrames;
            }
            cWritableStreams++;
        }
    }
    Log3Func(("%s: cLiveFrames=%#x cFramesToRead=%#x cWritableStreams=%#x\n", pSink->pszName,
              AudioMixBufLive(&pSink->MixBuf), cFramesToRead, cWritableStreams));

    if (cWritableStreams > 0)
    {
        if (cFramesToRead > 0)
        {
            /*
             * For each of the enabled streams, convert cFramesToRead frames from
             * the mixing buffer and write that to the downstream driver.
             */
            RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
            {
                if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
                {
                    uint32_t offSrcFrame = 0;
                    do
                    {
                        /* Convert a chunk from the mixer buffer.  */
                        union
                        {
                            uint8_t  ab[8192];
                            uint64_t au64[8192 / sizeof(uint64_t)]; /* Use uint64_t to ensure good alignment. */
                        } Buf;
                        uint32_t cbDstPeeked      = sizeof(Buf);
                        uint32_t cSrcFramesPeeked = cFramesToRead - offSrcFrame;
                        AudioMixBufPeek(&pSink->MixBuf, offSrcFrame, cSrcFramesPeeked, &cSrcFramesPeeked,
                                        &pMixStream->PeekState, &Buf, sizeof(Buf), &cbDstPeeked);
                        offSrcFrame += cSrcFramesPeeked;

                        /* Write it to the backend.  Since've checked that there is buffer
                           space available, this should always write the whole buffer. */
                        uint32_t cbDstWritten = 0;
                        int rc2 = pMixStream->pConn->pfnStreamWrite(pMixStream->pConn, pMixStream->pStream,
                                                                    &Buf, cbDstPeeked, &cbDstWritten);
                        Log3Func(("%s: %#x L %#x => %#x bytes; wrote %#x rc2=%Rrc %s\n", pSink->pszName, offSrcFrame,
                                  cSrcFramesPeeked - cSrcFramesPeeked, cbDstPeeked, cbDstWritten, rc2, pMixStream->pszName));
                        if (RT_SUCCESS(rc2))
                            AssertLogRelMsg(cbDstWritten == cbDstPeeked,
                                            ("cbDstWritten=%#x cbDstPeeked=%#x - (sink '%s')\n",
                                             cbDstWritten, cbDstPeeked, pSink->pszName));
                        else if (rc2 == VERR_AUDIO_STREAM_NOT_READY)
                        {
                            LogRel2(("Audio Mixer: '%s' (sink '%s'): Stream not ready - skipping.\n",
                                     pMixStream->pszName, pSink->pszName));
                            break; /* must've changed status, stop processing */
                        }
                        else
                        {
                            Assert(rc2 != VERR_BUFFER_OVERFLOW);
                            LogRel2(("Audio Mixer: Writing to mixer stream '%s' (sink '%s') failed, rc=%Rrc\n",
                                     pMixStream->pszName, pSink->pszName, rc2));
                            break;
                        }
                    } while (offSrcFrame < cFramesToRead);
                }
            }

            AudioMixBufAdvance(&pSink->MixBuf, cFramesToRead);
        }

        /*
         * Update the dirty flag for what it's worth.
         */
        if (AudioMixBufUsed(&pSink->MixBuf))
            pSink->fStatus |= AUDMIXSINK_STS_DIRTY;
        else
            pSink->fStatus &= ~AUDMIXSINK_STS_DIRTY;
    }
    else
    {
        /*
         * If no writable streams, just drop the mixer buffer content.
         */
        AudioMixBufDrop(&pSink->MixBuf);
        pSink->fStatus &= ~AUDMIXSINK_STS_DIRTY;
    }

    /*
     * Iterate buffers (pfnStreamPlay is not used any more).
     */
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
    {
        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
            pMixStream->pConn->pfnStreamIterate(pMixStream->pConn, pMixStream->pStream);
    }

    /* Update last updated timestamp. */
    pSink->tsLastUpdatedMs = RTTimeMilliTS();

    /*
     * Deal with pending disable.
     * We reset the sink when all streams have been disabled.
     */
    if (pSink->fStatus & AUDMIXSINK_STS_PENDING_DISABLE)
    {
        uint32_t const  cStreams         = pSink->cStreams;
        uint32_t        cStreamsDisabled = pSink->cStreams;
        RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
        {
            if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
            {
                PDMAUDIOSTREAMSTS const fSts = pMixStream->pConn->pfnStreamGetStatus(pMixStream->pConn, pMixStream->pStream);
                if (fSts & (PDMAUDIOSTREAMSTS_FLAGS_ENABLED | PDMAUDIOSTREAMSTS_FLAGS_PENDING_DISABLE))
                    cStreamsDisabled--;
            }
        }
        Log3Func(("[%s] pending disable: %u of %u disabled\n", pSink->pszName, cStreamsDisabled, cStreams));
        if (cStreamsDisabled == cStreams)
            audioMixerSinkReset(pSink);
    }

    return VINF_SUCCESS;
}

/**
 * Updates (invalidates) a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink               Mixer sink to update.
 */
int AudioMixerSinkUpdate(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

#ifdef LOG_ENABLED
    char szStatus[AUDIOMIXERSINK_STATUS_STR_MAX];
#endif
    Log3Func(("[%s] fStatus=%s\n", pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus)));

    /* Only process running sinks. */
    if (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
    {
        /* Do separate processing for input and output sinks. */
        if (pSink->enmDir == AUDMIXSINKDIR_OUTPUT)
            rc = audioMixerSinkUpdateOutput(pSink);
        else if (pSink->enmDir == AUDMIXSINKDIR_INPUT)
            rc = audioMixerSinkUpdateInput(pSink);
        else
            AssertFailed();
    }
    else
        rc = VINF_SUCCESS; /* disabled */

    RTCritSectLeave(&pSink->CritSect);
    return rc;
}

/**
 * Updates the (master) volume of a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink               Mixer sink to update volume for.
 * @param   pVolMaster          Master volume to set.
 */
static int audioMixerSinkUpdateVolume(PAUDMIXSINK pSink, const PPDMAUDIOVOLUME pVolMaster)
{
    AssertPtrReturn(pSink,      VERR_INVALID_POINTER);
    AssertPtrReturn(pVolMaster, VERR_INVALID_POINTER);

    LogFlowFunc(("[%s] Master fMuted=%RTbool, lVol=%RU32, rVol=%RU32\n",
                  pSink->pszName, pVolMaster->fMuted, pVolMaster->uLeft, pVolMaster->uRight));
    LogFlowFunc(("[%s] fMuted=%RTbool, lVol=%RU32, rVol=%RU32 ",
                  pSink->pszName, pSink->Volume.fMuted, pSink->Volume.uLeft, pSink->Volume.uRight));

    /** @todo Very crude implementation for now -- needs more work! */

    pSink->VolumeCombined.fMuted  = pVolMaster->fMuted || pSink->Volume.fMuted;

    pSink->VolumeCombined.uLeft   = (  (pSink->Volume.uLeft ? pSink->Volume.uLeft : 1)
                                     * (pVolMaster->uLeft   ? pVolMaster->uLeft   : 1)) / PDMAUDIO_VOLUME_MAX;

    pSink->VolumeCombined.uRight  = (  (pSink->Volume.uRight ? pSink->Volume.uRight : 1)
                                     * (pVolMaster->uRight   ? pVolMaster->uRight   : 1)) / PDMAUDIO_VOLUME_MAX;

    LogFlow(("-> fMuted=%RTbool, lVol=%RU32, rVol=%RU32\n",
             pSink->VolumeCombined.fMuted, pSink->VolumeCombined.uLeft, pSink->VolumeCombined.uRight));

    /*
     * Input sinks must currently propagate the new volume settings to
     * all the streams.  (For output sinks we do the volume control here.)
     */
    if (pSink->enmDir != AUDMIXSINKDIR_OUTPUT)
    {
        PAUDMIXSTREAM pMixStream;
        RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
        {
            int rc2 = pMixStream->pConn->pfnStreamSetVolume(pMixStream->pConn, pMixStream->pStream, &pSink->VolumeCombined);
            AssertRC(rc2);
        }
    }

    return VINF_SUCCESS;
}

/**
 * Writes data to a mixer output sink.
 *
 * @returns VBox status code.
 * @param   pSink               Sink to write data to.
 * @param   enmOp               Mixer operation to use when writing data to the sink.
 * @param   pvBuf               Buffer containing the audio data to write.
 * @param   cbBuf               Size (in bytes) of the buffer containing the audio data.
 * @param   pcbWritten          Number of bytes written. Optional.
 */
int AudioMixerSinkWrite(PAUDMIXSINK pSink, AUDMIXOP enmOp, const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    RT_NOREF(enmOp);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn   (cbBuf, VERR_INVALID_PARAMETER);
    /* pcbWritten is optional. */

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    AssertMsg(pSink->fStatus & AUDMIXSINK_STS_RUNNING,
              ("%s: Can't write to a sink which is not running (anymore) (status 0x%x)\n", pSink->pszName, pSink->fStatus));
    AssertMsg(pSink->enmDir == AUDMIXSINKDIR_OUTPUT,
              ("%s: Can't write to a sink which is not an output sink\n", pSink->pszName));

    uint32_t cbWritten = 0;
    uint32_t cbToWrite = RT_MIN(AudioMixBufFreeBytes(&pSink->MixBuf), cbBuf);
    while (cbToWrite)
    {
        /* Write the data to the mixer sink's own mixing buffer.
           Here the audio data is transformed into the mixer sink's format. */
        uint32_t cFramesWritten = 0;
        rc = AudioMixBufWriteCirc(&pSink->MixBuf, (uint8_t const*)pvBuf + cbWritten, cbToWrite, &cFramesWritten);
        if (RT_SUCCESS(rc))
        {
            const uint32_t cbWrittenChunk = PDMAudioPropsFramesToBytes(&pSink->PCMProps, cFramesWritten);
            Assert(cbToWrite >= cbWrittenChunk);
            cbToWrite -= cbWrittenChunk;
            cbWritten += cbWrittenChunk;
        }
        else
            break;
    }

    Log3Func(("[%s] cbBuf=%RU32 -> cbWritten=%RU32\n", pSink->pszName, cbBuf, cbWritten));

    /* Update the sink's last written time stamp. */
    pSink->tsLastReadWrittenNs = RTTimeNanoTS();

    if (pcbWritten)
        *pcbWritten = cbWritten;

    RTCritSectLeave(&pSink->CritSect);
    return rc;
}


/*********************************************************************************************************************************
 * Mixer Stream implementation.
 ********************************************************************************************************************************/

/**
 * Controls a mixer stream, internal version.
 *
 * @returns VBox status code.
 * @param   pMixStream          Mixer stream to control.
 * @param   enmCmd              Mixer stream command to use.
 * @param   fCtl                Additional control flags. Pass 0.
 */
static int audioMixerStreamCtlInternal(PAUDMIXSTREAM pMixStream, PDMAUDIOSTREAMCMD enmCmd, uint32_t fCtl)
{
    AssertPtr(pMixStream->pConn);
    AssertPtr(pMixStream->pStream);

    RT_NOREF(fCtl);

    int rc = pMixStream->pConn->pfnStreamControl(pMixStream->pConn, pMixStream->pStream, enmCmd);

    LogFlowFunc(("[%s] enmCmd=%ld, rc=%Rrc\n", pMixStream->pszName, enmCmd, rc));

    return rc;
}

/**
 * Updates a mixer stream's internal status.
 *
 * @returns VBox status code.
 * @param   pMixStream          Mixer stream to to update internal status for.
 */
static int audioMixerStreamUpdateStatus(PAUDMIXSTREAM pMixStream)
{
    pMixStream->fStatus = AUDMIXSTREAM_STATUS_NONE;

    if (pMixStream->pConn) /* Audio connector available? */
    {
        const uint32_t fStreamStatus = pMixStream->pConn->pfnStreamGetStatus(pMixStream->pConn, pMixStream->pStream);

        if (PDMAudioStrmStatusIsReady(fStreamStatus))
            pMixStream->fStatus |= AUDMIXSTREAM_STATUS_ENABLED;

        AssertPtr(pMixStream->pSink);
        switch (pMixStream->pSink->enmDir)
        {
            case AUDMIXSINKDIR_INPUT:
                if (PDMAudioStrmStatusCanRead(fStreamStatus))
                   pMixStream->fStatus |= AUDMIXSTREAM_STATUS_CAN_READ;
                break;

            case AUDMIXSINKDIR_OUTPUT:
                if (PDMAudioStrmStatusCanWrite(fStreamStatus))
                   pMixStream->fStatus |= AUDMIXSTREAM_STATUS_CAN_WRITE;
                break;

            default:
                AssertFailedReturn(VERR_NOT_IMPLEMENTED);
                break;
        }
    }

    LogFlowFunc(("[%s] -> 0x%x\n", pMixStream->pszName, pMixStream->fStatus));
    return VINF_SUCCESS;
}

/**
 * Controls a mixer stream.
 *
 * @returns VBox status code.
 * @param   pMixStream          Mixer stream to control.
 * @param   enmCmd              Mixer stream command to use.
 * @param   fCtl                Additional control flags. Pass 0.
 */
int AudioMixerStreamCtl(PAUDMIXSTREAM pMixStream, PDMAUDIOSTREAMCMD enmCmd, uint32_t fCtl)
{
    RT_NOREF(fCtl);
    AssertPtrReturn(pMixStream, VERR_INVALID_POINTER);
    /** @todo Validate fCtl. */

    int rc = RTCritSectEnter(&pMixStream->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    rc = audioMixerStreamCtlInternal(pMixStream, enmCmd, fCtl);

    int rc2 = RTCritSectLeave(&pMixStream->CritSect);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}

/**
 * Destroys a mixer stream, internal version.
 *
 * @param   pMixStream  Mixer stream to destroy.
 * @param   pDevIns     The device instance the statistics are registered with.
 */
static void audioMixerStreamDestroyInternal(PAUDMIXSTREAM pMixStream, PPDMDEVINS pDevIns)
{
    AssertPtrReturnVoid(pMixStream);

    LogFunc(("%s\n", pMixStream->pszName));

    if (pMixStream->pConn) /* Stream has a connector interface present? */
    {
        if (pMixStream->pStream)
        {
            pMixStream->pConn->pfnStreamRelease(pMixStream->pConn, pMixStream->pStream);
            pMixStream->pConn->pfnStreamDestroy(pMixStream->pConn, pMixStream->pStream);

            pMixStream->pStream = NULL;
        }

        pMixStream->pConn = NULL;
    }

    if (pMixStream->pszStatPrefix)
    {
        PDMDevHlpSTAMDeregisterByPrefix(pDevIns, pMixStream->pszStatPrefix);
        RTStrFree(pMixStream->pszStatPrefix);
        pMixStream->pszStatPrefix = NULL;
    }

    RTStrFree(pMixStream->pszName);
    pMixStream->pszName = NULL;

    int rc2 = RTCritSectDelete(&pMixStream->CritSect);
    AssertRC(rc2);

    RTMemFree(pMixStream);
    pMixStream = NULL;
}

/**
 * Destroys a mixer stream.
 *
 * @param   pMixStream      Mixer stream to destroy.
 * @param   pDevIns         The device instance statistics are registered with.
 */
void AudioMixerStreamDestroy(PAUDMIXSTREAM pMixStream, PPDMDEVINS pDevIns)
{
    if (!pMixStream)
        return;

    int rc2 = RTCritSectEnter(&pMixStream->CritSect);
    AssertRC(rc2);

    LogFunc(("%s\n", pMixStream->pszName));

    if (pMixStream->pSink) /* Is the stream part of a sink? */
    {
        /* Save sink pointer, as after audioMixerSinkRemoveStreamInternal() the
         * pointer will be gone from the stream. */
        PAUDMIXSINK pSink = pMixStream->pSink;

        rc2 = audioMixerSinkRemoveStreamInternal(pSink, pMixStream);
        if (RT_SUCCESS(rc2))
        {
            Assert(pSink->cStreams);
            pSink->cStreams--;
        }
    }
    else
        rc2 = VINF_SUCCESS;

    int rc3 = RTCritSectLeave(&pMixStream->CritSect);
    AssertRC(rc3);

    if (RT_SUCCESS(rc2))
    {
        audioMixerStreamDestroyInternal(pMixStream, pDevIns);
        pMixStream = NULL;
    }

    LogFlowFunc(("Returning %Rrc\n", rc2));
}

/**
 * Returns whether a mixer stream currently is active (playing/recording) or not.
 *
 * @returns @c true if playing/recording, @c false if not.
 * @param   pMixStream          Mixer stream to return status for.
 */
bool AudioMixerStreamIsActive(PAUDMIXSTREAM pMixStream)
{
    int rc2 = RTCritSectEnter(&pMixStream->CritSect);
    if (RT_FAILURE(rc2))
        return false;

    AssertPtr(pMixStream->pConn);
    AssertPtr(pMixStream->pStream);

    bool fIsActive;

    if (   pMixStream->pConn
        && pMixStream->pStream
        && RT_BOOL(pMixStream->pConn->pfnStreamGetStatus(pMixStream->pConn, pMixStream->pStream) & PDMAUDIOSTREAMSTS_FLAGS_ENABLED))
    {
        fIsActive = true;
    }
    else
        fIsActive = false;

    rc2 = RTCritSectLeave(&pMixStream->CritSect);
    AssertRC(rc2);

    return fIsActive;
}

/**
 * Returns whether a mixer stream is valid (e.g. initialized and in a working state) or not.
 *
 * @returns @c true if valid, @c false if not.
 * @param   pMixStream          Mixer stream to return status for.
 */
bool AudioMixerStreamIsValid(PAUDMIXSTREAM pMixStream)
{
    if (!pMixStream)
        return false;

    int rc2 = RTCritSectEnter(&pMixStream->CritSect);
    if (RT_FAILURE(rc2))
        return false;

    bool fIsValid;

    if (   pMixStream->pConn
        && pMixStream->pStream
        && RT_BOOL(pMixStream->pConn->pfnStreamGetStatus(pMixStream->pConn, pMixStream->pStream) & PDMAUDIOSTREAMSTS_FLAGS_INITIALIZED))
    {
        fIsValid = true;
    }
    else
        fIsValid = false;

    rc2 = RTCritSectLeave(&pMixStream->CritSect);
    AssertRC(rc2);

    return fIsValid;
}

