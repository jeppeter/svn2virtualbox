/* $Id$ */
/** @file
 * Intermedia audio driver, common routines.
 *
 * These are also used in the drivers which are bound to Main, e.g. the VRDE
 * or the video audio recording drivers.
 */

/*
 * Copyright (C) 2006-2020 Oracle Corporation
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
#include <iprt/alloc.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/uuid.h>

#define LOG_GROUP LOG_GROUP_DRV_AUDIO
#include <VBox/log.h>

#include <VBox/err.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/mm.h>

#include <ctype.h>
#include <stdlib.h>

#include "DrvAudio.h"
#include "AudioMixBuffer.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Structure for building up a .WAV file header.
 */
typedef struct AUDIOWAVFILEHDR
{
    uint32_t u32RIFF;
    uint32_t u32Size;
    uint32_t u32WAVE;

    uint32_t u32Fmt;
    uint32_t u32Size1;
    uint16_t u16AudioFormat;
    uint16_t u16NumChannels;
    uint32_t u32SampleRate;
    uint32_t u32ByteRate;
    uint16_t u16BlockAlign;
    uint16_t u16BitsPerSample;

    uint32_t u32ID2;
    uint32_t u32Size2;
} AUDIOWAVFILEHDR, *PAUDIOWAVFILEHDR;
AssertCompileSize(AUDIOWAVFILEHDR, 11*4);

/**
 * Structure for keeeping the internal .WAV file data
 */
typedef struct AUDIOWAVFILEDATA
{
    /** The file header/footer. */
    AUDIOWAVFILEHDR Hdr;
} AUDIOWAVFILEDATA, *PAUDIOWAVFILEDATA;




/**
 * Retrieves the matching PDMAUDIOFMT for the given bits + signing flag.
 *
 * @return  Matching PDMAUDIOFMT value.
 * @retval  PDMAUDIOFMT_INVALID if unsupported @a cBits value.
 *
 * @param   cBits       The number of bits in the audio format.
 * @param   fSigned     Whether the audio format is signed @c true or not.
 */
PDMAUDIOFMT DrvAudioAudFmtBitsToFormat(uint8_t cBits, bool fSigned)
{
    if (fSigned)
    {
        switch (cBits)
        {
            case 8:  return PDMAUDIOFMT_S8;
            case 16: return PDMAUDIOFMT_S16;
            case 32: return PDMAUDIOFMT_S32;
            default: AssertMsgFailedReturn(("Bogus audio bits %RU8\n", cBits), PDMAUDIOFMT_INVALID);
        }
    }
    else
    {
        switch (cBits)
        {
            case 8:  return PDMAUDIOFMT_U8;
            case 16: return PDMAUDIOFMT_U16;
            case 32: return PDMAUDIOFMT_U32;
            default: AssertMsgFailedReturn(("Bogus audio bits %RU8\n", cBits), PDMAUDIOFMT_INVALID);
        }
    }
}

/**
 * Returns an unique file name for this given audio connector instance.
 *
 * @return  Allocated file name. Must be free'd using RTStrFree().
 * @param   uInstance           Driver / device instance.
 * @param   pszPath             Path name of the file to delete. The path must exist.
 * @param   pszSuffix           File name suffix to use.
 */
char *DrvAudioDbgGetFileNameA(uint8_t uInstance, const char *pszPath, const char *pszSuffix)
{
    char szFileName[64];
    RTStrPrintf(szFileName, sizeof(szFileName), "drvAudio%RU8-%s", uInstance, pszSuffix);

    char szFilePath[RTPATH_MAX];
    int rc2 = RTStrCopy(szFilePath, sizeof(szFilePath), pszPath);
    AssertRC(rc2);
    rc2 = RTPathAppend(szFilePath, sizeof(szFilePath), szFileName);
    AssertRC(rc2);

    return RTStrDup(szFilePath);
}

/**
 * Allocates an audio device.
 *
 * @returns Newly allocated audio device, or NULL if failed.
 * @param   cbData              How much additional data (in bytes) should be allocated to provide
 *                              a (backend) specific area to store additional data.
 *                              Optional, can be 0.
 */
PPDMAUDIODEVICE DrvAudioHlpDeviceAlloc(size_t cbData)
{
    PPDMAUDIODEVICE pDev = (PPDMAUDIODEVICE)RTMemAllocZ(sizeof(PDMAUDIODEVICE));
    if (!pDev)
        return NULL;

    if (cbData)
    {
        pDev->pvData = RTMemAllocZ(cbData);
        if (!pDev->pvData)
        {
            RTMemFree(pDev);
            return NULL;
        }
    }

    pDev->cbData = cbData;

    pDev->cMaxInputChannels  = 0;
    pDev->cMaxOutputChannels = 0;

    return pDev;
}

/**
 * Frees an audio device.
 *
 * @param pDev                  Device to free.
 */
void DrvAudioHlpDeviceFree(PPDMAUDIODEVICE pDev)
{
    if (!pDev)
        return;

    Assert(pDev->cRefCount == 0);

    if (pDev->pvData)
    {
        Assert(pDev->cbData);

        RTMemFree(pDev->pvData);
        pDev->pvData = NULL;
    }

    RTMemFree(pDev);
    pDev = NULL;
}

/**
 * Duplicates an audio device entry.
 *
 * @returns Duplicated audio device entry on success, or NULL on failure.
 * @param   pDev                Audio device entry to duplicate.
 * @param   fCopyUserData       Whether to also copy the user data portion or not.
 */
PPDMAUDIODEVICE DrvAudioHlpDeviceDup(const PPDMAUDIODEVICE pDev, bool fCopyUserData)
{
    AssertPtrReturn(pDev, NULL);

    PPDMAUDIODEVICE pDevDup = DrvAudioHlpDeviceAlloc(fCopyUserData ? pDev->cbData : 0);
    if (pDevDup)
    {
        memcpy(pDevDup, pDev, sizeof(PDMAUDIODEVICE));

        if (   fCopyUserData
            && pDevDup->cbData)
        {
            memcpy(pDevDup->pvData, pDev->pvData, pDevDup->cbData);
        }
        else
        {
            pDevDup->cbData = 0;
            pDevDup->pvData = NULL;
        }
    }

    return pDevDup;
}

/**
 * Initializes an audio device enumeration structure.
 *
 * @returns IPRT status code.
 * @param   pDevEnm             Device enumeration to initialize.
 */
int DrvAudioHlpDeviceEnumInit(PPDMAUDIODEVICEENUM pDevEnm)
{
    AssertPtrReturn(pDevEnm, VERR_INVALID_POINTER);

    RTListInit(&pDevEnm->lstDevices);
    pDevEnm->cDevices = 0;

    return VINF_SUCCESS;
}

/**
 * Frees audio device enumeration data.
 *
 * @param pDevEnm               Device enumeration to destroy.
 */
void DrvAudioHlpDeviceEnumFree(PPDMAUDIODEVICEENUM pDevEnm)
{
    if (!pDevEnm)
        return;

    PPDMAUDIODEVICE pDev, pDevNext;
    RTListForEachSafe(&pDevEnm->lstDevices, pDev, pDevNext, PDMAUDIODEVICE, Node)
    {
        RTListNodeRemove(&pDev->Node);

        DrvAudioHlpDeviceFree(pDev);

        pDevEnm->cDevices--;
    }

    /* Sanity. */
    Assert(RTListIsEmpty(&pDevEnm->lstDevices));
    Assert(pDevEnm->cDevices == 0);
}

/**
 * Adds an audio device to a device enumeration.
 *
 * @return IPRT status code.
 * @param  pDevEnm              Device enumeration to add device to.
 * @param  pDev                 Device to add. The pointer will be owned by the device enumeration  then.
 */
int DrvAudioHlpDeviceEnumAdd(PPDMAUDIODEVICEENUM pDevEnm, PPDMAUDIODEVICE pDev)
{
    AssertPtrReturn(pDevEnm, VERR_INVALID_POINTER);
    AssertPtrReturn(pDev,    VERR_INVALID_POINTER);

    RTListAppend(&pDevEnm->lstDevices, &pDev->Node);
    pDevEnm->cDevices++;

    return VINF_SUCCESS;
}

/**
 * Duplicates a device enumeration.
 *
 * @returns Duplicated device enumeration, or NULL on failure.
 *          Must be free'd with DrvAudioHlpDeviceEnumFree().
 * @param   pDevEnm             Device enumeration to duplicate.
 */
PPDMAUDIODEVICEENUM DrvAudioHlpDeviceEnumDup(const PPDMAUDIODEVICEENUM pDevEnm)
{
    AssertPtrReturn(pDevEnm, NULL);

    PPDMAUDIODEVICEENUM pDevEnmDup = (PPDMAUDIODEVICEENUM)RTMemAlloc(sizeof(PDMAUDIODEVICEENUM));
    if (!pDevEnmDup)
        return NULL;

    int rc2 = DrvAudioHlpDeviceEnumInit(pDevEnmDup);
    AssertRC(rc2);

    PPDMAUDIODEVICE pDev;
    RTListForEach(&pDevEnm->lstDevices, pDev, PDMAUDIODEVICE, Node)
    {
        PPDMAUDIODEVICE pDevDup = DrvAudioHlpDeviceDup(pDev, true /* fCopyUserData */);
        if (!pDevDup)
        {
            rc2 = VERR_NO_MEMORY;
            break;
        }

        rc2 = DrvAudioHlpDeviceEnumAdd(pDevEnmDup, pDevDup);
        if (RT_FAILURE(rc2))
        {
            DrvAudioHlpDeviceFree(pDevDup);
            break;
        }
    }

    if (RT_FAILURE(rc2))
    {
        DrvAudioHlpDeviceEnumFree(pDevEnmDup);
        pDevEnmDup = NULL;
    }

    return pDevEnmDup;
}

/**
 * Copies device enumeration entries from the source to the destination enumeration.
 *
 * @returns IPRT status code.
 * @param   pDstDevEnm          Destination enumeration to store enumeration entries into.
 * @param   pSrcDevEnm          Source enumeration to use.
 * @param   enmUsage            Which entries to copy. Specify PDMAUDIODIR_ANY to copy all entries.
 * @param   fCopyUserData       Whether to also copy the user data portion or not.
 */
int DrvAudioHlpDeviceEnumCopyEx(PPDMAUDIODEVICEENUM pDstDevEnm, const PPDMAUDIODEVICEENUM pSrcDevEnm,
                                PDMAUDIODIR enmUsage, bool fCopyUserData)
{
    AssertPtrReturn(pDstDevEnm, VERR_INVALID_POINTER);
    AssertPtrReturn(pSrcDevEnm, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    PPDMAUDIODEVICE pSrcDev;
    RTListForEach(&pSrcDevEnm->lstDevices, pSrcDev, PDMAUDIODEVICE, Node)
    {
        if (   enmUsage != PDMAUDIODIR_ANY
            && enmUsage != pSrcDev->enmUsage)
        {
            continue;
        }

        PPDMAUDIODEVICE pDstDev = DrvAudioHlpDeviceDup(pSrcDev, fCopyUserData);
        if (!pDstDev)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        rc = DrvAudioHlpDeviceEnumAdd(pDstDevEnm, pDstDev);
        if (RT_FAILURE(rc))
            break;
    }

    return rc;
}

/**
 * Copies all device enumeration entries from the source to the destination enumeration.
 *
 * Note: Does *not* copy the user-specific data assigned to a device enumeration entry.
 *       To do so, use DrvAudioHlpDeviceEnumCopyEx().
 *
 * @returns IPRT status code.
 * @param   pDstDevEnm          Destination enumeration to store enumeration entries into.
 * @param   pSrcDevEnm          Source enumeration to use.
 */
int DrvAudioHlpDeviceEnumCopy(PPDMAUDIODEVICEENUM pDstDevEnm, const PPDMAUDIODEVICEENUM pSrcDevEnm)
{
    return DrvAudioHlpDeviceEnumCopyEx(pDstDevEnm, pSrcDevEnm, PDMAUDIODIR_ANY, false /* fCopyUserData */);
}

/**
 * Returns the default device of a given device enumeration.
 * This assumes that only one default device per usage is set.
 *
 * @returns Default device if found, or NULL if none found.
 * @param   pDevEnm             Device enumeration to get default device for.
 * @param   enmUsage            Usage to get default device for.
 */
PPDMAUDIODEVICE DrvAudioHlpDeviceEnumGetDefaultDevice(const PPDMAUDIODEVICEENUM pDevEnm, PDMAUDIODIR enmUsage)
{
    AssertPtrReturn(pDevEnm, NULL);

    PPDMAUDIODEVICE pDev;
    RTListForEach(&pDevEnm->lstDevices, pDev, PDMAUDIODEVICE, Node)
    {
        if (enmUsage != PDMAUDIODIR_ANY)
        {
            if (enmUsage != pDev->enmUsage) /* Wrong usage? Skip. */
                continue;
        }

        if (pDev->fFlags & PDMAUDIODEV_FLAGS_DEFAULT)
            return pDev;
    }

    return NULL;
}

/**
 * Returns the number of enumerated devices of a given device enumeration.
 *
 * @returns Number of devices if found, or 0 if none found.
 * @param   pDevEnm             Device enumeration to get default device for.
 * @param   enmUsage            Usage to get default device for.
 */
uint16_t DrvAudioHlpDeviceEnumGetDeviceCount(const PPDMAUDIODEVICEENUM pDevEnm, PDMAUDIODIR enmUsage)
{
    AssertPtrReturn(pDevEnm, 0);

    if (enmUsage == PDMAUDIODIR_ANY)
        return pDevEnm->cDevices;

    uint32_t cDevs = 0;

    PPDMAUDIODEVICE pDev;
    RTListForEach(&pDevEnm->lstDevices, pDev, PDMAUDIODEVICE, Node)
    {
        if (enmUsage == pDev->enmUsage)
            cDevs++;
    }

    return cDevs;
}

/**
 * Logs an audio device enumeration.
 *
 * @param  pszDesc              Logging description.
 * @param  pDevEnm              Device enumeration to log.
 */
void DrvAudioHlpDeviceEnumPrint(const char *pszDesc, const PPDMAUDIODEVICEENUM pDevEnm)
{
    AssertPtrReturnVoid(pszDesc);
    AssertPtrReturnVoid(pDevEnm);

    LogFunc(("%s: %RU16 devices\n", pszDesc, pDevEnm->cDevices));

    PPDMAUDIODEVICE pDev;
    RTListForEach(&pDevEnm->lstDevices, pDev, PDMAUDIODEVICE, Node)
    {
        char *pszFlags = DrvAudioHlpAudDevFlagsToStrA(pDev->fFlags);

        LogFunc(("Device '%s':\n", pDev->szName));
        LogFunc(("\tUsage           = %s\n",             DrvAudioHlpAudDirToStr(pDev->enmUsage)));
        LogFunc(("\tFlags           = %s\n",             pszFlags ? pszFlags : "<NONE>"));
        LogFunc(("\tInput channels  = %RU8\n",           pDev->cMaxInputChannels));
        LogFunc(("\tOutput channels = %RU8\n",           pDev->cMaxOutputChannels));
        LogFunc(("\tData            = %p (%zu bytes)\n", pDev->pvData, pDev->cbData));

        if (pszFlags)
            RTStrFree(pszFlags);
    }
}

/**
 * Converts an audio direction to a string.
 *
 * @returns Stringified audio direction, or "Unknown", if not found.
 * @param   enmDir              Audio direction to convert.
 */
const char *DrvAudioHlpAudDirToStr(PDMAUDIODIR enmDir)
{
    switch (enmDir)
    {
        case PDMAUDIODIR_UNKNOWN: return "Unknown";
        case PDMAUDIODIR_IN:      return "Input";
        case PDMAUDIODIR_OUT:     return "Output";
        case PDMAUDIODIR_ANY:     return "Duplex";
        default:                  break;
    }

    AssertMsgFailed(("Invalid audio direction %ld\n", enmDir));
    return "Unknown";
}

/**
 * Converts an audio mixer control to a string.
 *
 * @returns Stringified audio mixer control or "Unknown", if not found.
 * @param   enmMixerCtl         Audio mixer control to convert.
 */
const char *DrvAudioHlpAudMixerCtlToStr(PDMAUDIOMIXERCTL enmMixerCtl)
{
    switch (enmMixerCtl)
    {
        case PDMAUDIOMIXERCTL_VOLUME_MASTER: return "Master Volume";
        case PDMAUDIOMIXERCTL_FRONT:         return "Front";
        case PDMAUDIOMIXERCTL_CENTER_LFE:    return "Center / LFE";
        case PDMAUDIOMIXERCTL_REAR:          return "Rear";
        case PDMAUDIOMIXERCTL_LINE_IN:       return "Line-In";
        case PDMAUDIOMIXERCTL_MIC_IN:        return "Microphone-In";
        default:                             break;
    }

    AssertMsgFailed(("Invalid mixer control %ld\n", enmMixerCtl));
    return "Unknown";
}

/**
 * Converts an audio device flags to a string.
 *
 * @returns Stringified audio flags. Must be free'd with RTStrFree().
 *          NULL if no flags set.
 * @param   fFlags      Audio flags (PDMAUDIODEV_FLAGS_XXX) to convert.
 */
char *DrvAudioHlpAudDevFlagsToStrA(uint32_t fFlags)
{
#define APPEND_FLAG_TO_STR(_aFlag)              \
    if (fFlags & PDMAUDIODEV_FLAGS_##_aFlag)    \
    {                                           \
        if (pszFlags)                           \
        {                                       \
            rc2 = RTStrAAppend(&pszFlags, " "); \
            if (RT_FAILURE(rc2))                \
                break;                          \
        }                                       \
                                                \
        rc2 = RTStrAAppend(&pszFlags, #_aFlag); \
        if (RT_FAILURE(rc2))                    \
            break;                              \
    }                                           \

    char *pszFlags = NULL;
    int rc2 = VINF_SUCCESS;

    do
    {
        APPEND_FLAG_TO_STR(DEFAULT);
        APPEND_FLAG_TO_STR(HOTPLUG);
        APPEND_FLAG_TO_STR(BUGGY);
        APPEND_FLAG_TO_STR(IGNORE);
        APPEND_FLAG_TO_STR(LOCKED);
        APPEND_FLAG_TO_STR(DEAD);

    } while (0);

    if (!pszFlags)
        rc2 = RTStrAAppend(&pszFlags, "NONE");

    if (   RT_FAILURE(rc2)
        && pszFlags)
    {
        RTStrFree(pszFlags);
        pszFlags = NULL;
    }

#undef APPEND_FLAG_TO_STR

    return pszFlags;
}

/**
 * Converts a playback destination enumeration to a string.
 *
 * @returns Stringified playback destination, or "Unknown", if not found.
 * @param   enmPlaybackDst      Playback destination to convert.
 */
const char *DrvAudioHlpPlaybackDstToStr(const PDMAUDIOPLAYBACKDST enmPlaybackDst)
{
    switch (enmPlaybackDst)
    {
        case PDMAUDIOPLAYBACKDST_UNKNOWN:    return "Unknown";
        case PDMAUDIOPLAYBACKDST_FRONT:      return "Front";
        case PDMAUDIOPLAYBACKDST_CENTER_LFE: return "Center / LFE";
        case PDMAUDIOPLAYBACKDST_REAR:       return "Rear";
        default:
            break;
    }

    AssertMsgFailed(("Invalid playback destination %ld\n", enmPlaybackDst));
    return "Unknown";
}

/**
 * Converts a recording source enumeration to a string.
 *
 * @returns Stringified recording source, or "Unknown", if not found.
 * @param   enmRecSrc           Recording source to convert.
 */
const char *DrvAudioHlpRecSrcToStr(const PDMAUDIORECSRC enmRecSrc)
{
    switch (enmRecSrc)
    {
        case PDMAUDIORECSRC_UNKNOWN: return "Unknown";
        case PDMAUDIORECSRC_MIC:     return "Microphone In";
        case PDMAUDIORECSRC_CD:      return "CD";
        case PDMAUDIORECSRC_VIDEO:   return "Video";
        case PDMAUDIORECSRC_AUX:     return "AUX";
        case PDMAUDIORECSRC_LINE:    return "Line In";
        case PDMAUDIORECSRC_PHONE:   return "Phone";
        default:
            break;
    }

    AssertMsgFailed(("Invalid recording source %ld\n", enmRecSrc));
    return "Unknown";
}

/**
 * Returns wether the given audio format has signed bits or not.
 *
 * @return  IPRT status code.
 * @return  bool                @c true for signed bits, @c false for unsigned.
 * @param   enmFmt              Audio format to retrieve value for.
 */
bool DrvAudioHlpAudFmtIsSigned(PDMAUDIOFMT enmFmt)
{
    switch (enmFmt)
    {
        case PDMAUDIOFMT_S8:
        case PDMAUDIOFMT_S16:
        case PDMAUDIOFMT_S32:
            return true;

        case PDMAUDIOFMT_U8:
        case PDMAUDIOFMT_U16:
        case PDMAUDIOFMT_U32:
            return false;

        default:
            break;
    }

    AssertMsgFailed(("Bogus audio format %ld\n", enmFmt));
    return false;
}

/**
 * Returns the bits of a given audio format.
 *
 * @return  IPRT status code.
 * @return  uint8_t             Bits of audio format.
 * @param   enmFmt              Audio format to retrieve value for.
 */
uint8_t DrvAudioHlpAudFmtToBits(PDMAUDIOFMT enmFmt)
{
    switch (enmFmt)
    {
        case PDMAUDIOFMT_S8:
        case PDMAUDIOFMT_U8:
            return 8;

        case PDMAUDIOFMT_U16:
        case PDMAUDIOFMT_S16:
            return 16;

        case PDMAUDIOFMT_U32:
        case PDMAUDIOFMT_S32:
            return 32;

        default:
            break;
    }

    AssertMsgFailed(("Bogus audio format %ld\n", enmFmt));
    return 0;
}

/**
 * Converts an audio format to a string.
 *
 * @returns Stringified audio format, or "Unknown", if not found.
 * @param   enmFmt              Audio format to convert.
 */
const char *DrvAudioHlpAudFmtToStr(PDMAUDIOFMT enmFmt)
{
    switch (enmFmt)
    {
        case PDMAUDIOFMT_U8:
            return "U8";

        case PDMAUDIOFMT_U16:
            return "U16";

        case PDMAUDIOFMT_U32:
            return "U32";

        case PDMAUDIOFMT_S8:
            return "S8";

        case PDMAUDIOFMT_S16:
            return "S16";

        case PDMAUDIOFMT_S32:
            return "S32";

        default:
            break;
    }

    AssertMsgFailed(("Bogus audio format %ld\n", enmFmt));
    return "Unknown";
}

/**
 * Converts a given string to an audio format.
 *
 * @returns Audio format for the given string, or PDMAUDIOFMT_INVALID if not found.
 * @param   pszFmt              String to convert to an audio format.
 */
PDMAUDIOFMT DrvAudioHlpStrToAudFmt(const char *pszFmt)
{
    AssertPtrReturn(pszFmt, PDMAUDIOFMT_INVALID);

    if (!RTStrICmp(pszFmt, "u8"))
        return PDMAUDIOFMT_U8;
    if (!RTStrICmp(pszFmt, "u16"))
        return PDMAUDIOFMT_U16;
    if (!RTStrICmp(pszFmt, "u32"))
        return PDMAUDIOFMT_U32;
    if (!RTStrICmp(pszFmt, "s8"))
        return PDMAUDIOFMT_S8;
    if (!RTStrICmp(pszFmt, "s16"))
        return PDMAUDIOFMT_S16;
    if (!RTStrICmp(pszFmt, "s32"))
        return PDMAUDIOFMT_S32;

    AssertMsgFailed(("Invalid audio format '%s'\n", pszFmt));
    return PDMAUDIOFMT_INVALID;
}

/**
 * Initializes a stream configuration with its default values.
 *
 * @param   pCfg                Stream configuration to initialize.
 */
void DrvAudioHlpStreamCfgInit(PPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturnVoid(pCfg);

    RT_ZERO(*pCfg);

    pCfg->Backend.cFramesPreBuffering = UINT32_MAX; /* Explicitly set to "undefined". */
}

/**
 * Initializes a stream configuration from PCM properties.
 *
 * @return  IPRT status code.
 * @param   pCfg        Stream configuration to initialize.
 * @param   pProps      PCM properties to use.
 */
int DrvAudioHlpStreamCfgInitFromPcmProps(PPDMAUDIOSTREAMCFG pCfg, PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtrReturn(pProps, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfg,   VERR_INVALID_POINTER);

    DrvAudioHlpStreamCfgInit(pCfg);

    memcpy(&pCfg->Props, pProps, sizeof(PDMAUDIOPCMPROPS));
    return VINF_SUCCESS;
}

/**
 * Checks whether a given stream configuration is valid or not.
 *
 * Returns @c true if configuration is valid, @c false if not.
 * @param   pCfg                Stream configuration to check.
 */
bool DrvAudioHlpStreamCfgIsValid(PCPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pCfg, false);

    bool fValid = (   pCfg->enmDir == PDMAUDIODIR_IN
                   || pCfg->enmDir == PDMAUDIODIR_OUT);

    fValid &= (   pCfg->enmLayout == PDMAUDIOSTREAMLAYOUT_NON_INTERLEAVED
               || pCfg->enmLayout == PDMAUDIOSTREAMLAYOUT_RAW);

    if (fValid)
        fValid = DrvAudioHlpPCMPropsAreValid(&pCfg->Props);

    return fValid;
}

/**
 * Frees an allocated audio stream configuration.
 *
 * @param   pCfg                Audio stream configuration to free.
 */
void DrvAudioHlpStreamCfgFree(PPDMAUDIOSTREAMCFG pCfg)
{
    if (pCfg)
    {
        RTMemFree(pCfg);
        pCfg = NULL;
    }
}

/**
 * Copies a source stream configuration to a destination stream configuration.
 *
 * @returns IPRT status code.
 * @param   pDstCfg             Destination stream configuration to copy source to.
 * @param   pSrcCfg             Source stream configuration to copy to destination.
 */
int DrvAudioHlpStreamCfgCopy(PPDMAUDIOSTREAMCFG pDstCfg, PCPDMAUDIOSTREAMCFG pSrcCfg)
{
    AssertPtrReturn(pDstCfg, VERR_INVALID_POINTER);
    AssertPtrReturn(pSrcCfg, VERR_INVALID_POINTER);

#ifdef VBOX_STRICT
    if (!DrvAudioHlpStreamCfgIsValid(pSrcCfg))
    {
        AssertMsgFailed(("Stream config '%s' (%p) is invalid\n", pSrcCfg->szName, pSrcCfg));
        return VERR_INVALID_PARAMETER;
    }
#endif

    memcpy(pDstCfg, pSrcCfg, sizeof(PDMAUDIOSTREAMCFG));

    return VINF_SUCCESS;
}

/**
 * Duplicates an audio stream configuration.
 * Must be free'd with DrvAudioHlpStreamCfgFree().
 *
 * @return  Duplicates audio stream configuration on success, or NULL on failure.
 * @param   pCfg                    Audio stream configuration to duplicate.
 */
PPDMAUDIOSTREAMCFG DrvAudioHlpStreamCfgDup(PCPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pCfg, NULL);

#ifdef VBOX_STRICT
    if (!DrvAudioHlpStreamCfgIsValid(pCfg))
    {
        AssertMsgFailed(("Stream config '%s' (%p) is invalid\n", pCfg->szName, pCfg));
        return NULL;
    }
#endif

    PPDMAUDIOSTREAMCFG pDst = (PPDMAUDIOSTREAMCFG)RTMemAllocZ(sizeof(PDMAUDIOSTREAMCFG));
    if (!pDst)
        return NULL;

    int rc2 = DrvAudioHlpStreamCfgCopy(pDst, pCfg);
    if (RT_FAILURE(rc2))
    {
        DrvAudioHlpStreamCfgFree(pDst);
        pDst = NULL;
    }

    AssertPtr(pDst);
    return pDst;
}

/**
 * Prints an audio stream configuration to the debug log.
 *
 * @param   pCfg                Stream configuration to log.
 */
void DrvAudioHlpStreamCfgPrint(PCPDMAUDIOSTREAMCFG pCfg)
{
    if (!pCfg)
        return;

    LogFunc(("szName=%s, enmDir=%RU32 (uHz=%RU32, cBits=%RU8%s, cChannels=%RU8)\n",
             pCfg->szName, pCfg->enmDir,
             pCfg->Props.uHz, pCfg->Props.cbSample * 8, pCfg->Props.fSigned ? "S" : "U", pCfg->Props.cChannels));
}

/**
 * Converts a stream command to a string.
 *
 * @returns Stringified stream command, or "Unknown", if not found.
 * @param   enmCmd              Stream command to convert.
 */
const char *DrvAudioHlpStreamCmdToStr(PDMAUDIOSTREAMCMD enmCmd)
{
    switch (enmCmd)
    {
        case PDMAUDIOSTREAMCMD_INVALID: return "Invalid";
        case PDMAUDIOSTREAMCMD_UNKNOWN: return "Unknown";
        case PDMAUDIOSTREAMCMD_ENABLE:  return "Enable";
        case PDMAUDIOSTREAMCMD_DISABLE: return "Disable";
        case PDMAUDIOSTREAMCMD_PAUSE:   return "Pause";
        case PDMAUDIOSTREAMCMD_RESUME:  return "Resume";
        case PDMAUDIOSTREAMCMD_DRAIN:   return "Drain";
        case PDMAUDIOSTREAMCMD_DROP:    return "Drop";
        case PDMAUDIOSTREAMCMD_32BIT_HACK:
            break;
    }
    AssertMsgFailed(("Invalid stream command %d\n", enmCmd));
    return "Unknown";
}

/**
 * Returns @c true if the given stream status indicates a can-be-read-from stream,
 * @c false if not.
 *
 * @returns @c true if ready to be read from, @c if not.
 * @param   fStatus     Stream status to evaluate, PDMAUDIOSTREAMSTS_FLAGS_XXX.
 */
bool DrvAudioHlpStreamStatusCanRead(PDMAUDIOSTREAMSTS fStatus)
{
    AssertReturn(fStatus & PDMAUDIOSTREAMSTS_VALID_MASK, false);

    return      fStatus & PDMAUDIOSTREAMSTS_FLAGS_INITIALIZED
           &&   fStatus & PDMAUDIOSTREAMSTS_FLAGS_ENABLED
           && !(fStatus & PDMAUDIOSTREAMSTS_FLAGS_PAUSED)
           && !(fStatus & PDMAUDIOSTREAMSTS_FLAGS_PENDING_REINIT);
}

/**
 * Returns @c true if the given stream status indicates a can-be-written-to stream,
 * @c false if not.
 *
 * @returns @c true if ready to be written to, @c if not.
 * @param   fStatus     Stream status to evaluate, PDMAUDIOSTREAMSTS_FLAGS_XXX.
 */
bool DrvAudioHlpStreamStatusCanWrite(PDMAUDIOSTREAMSTS fStatus)
{
    AssertReturn(fStatus & PDMAUDIOSTREAMSTS_VALID_MASK, false);

    return      fStatus & PDMAUDIOSTREAMSTS_FLAGS_INITIALIZED
           &&   fStatus & PDMAUDIOSTREAMSTS_FLAGS_ENABLED
           && !(fStatus & PDMAUDIOSTREAMSTS_FLAGS_PAUSED)
           && !(fStatus & PDMAUDIOSTREAMSTS_FLAGS_PENDING_DISABLE)
           && !(fStatus & PDMAUDIOSTREAMSTS_FLAGS_PENDING_REINIT);
}

/**
 * Returns @c true if the given stream status indicates a ready-to-operate stream,
 * @c false if not.
 *
 * @returns @c true if ready to operate, @c if not.
 * @param   fStatus Stream status to evaluate.
 */
bool DrvAudioHlpStreamStatusIsReady(PDMAUDIOSTREAMSTS fStatus)
{
    AssertReturn(fStatus & PDMAUDIOSTREAMSTS_VALID_MASK, false);

    return      fStatus & PDMAUDIOSTREAMSTS_FLAGS_INITIALIZED
           &&   fStatus & PDMAUDIOSTREAMSTS_FLAGS_ENABLED
           && !(fStatus & PDMAUDIOSTREAMSTS_FLAGS_PENDING_REINIT);
}

/**
 * Calculates the audio bit rate of the given bits per sample, the Hz and the number
 * of audio channels.
 *
 * Divide the result by 8 to get the byte rate.
 *
 * @returns Bitrate.
 * @param   cBits               Number of bits per sample.
 * @param   uHz                 Hz (Hertz) rate.
 * @param   cChannels           Number of audio channels.
 */
uint32_t DrvAudioHlpCalcBitrate(uint8_t cBits, uint32_t uHz, uint8_t cChannels)
{
    return cBits * uHz * cChannels;
}


/*********************************************************************************************************************************
*   PCM Property Helpers                                                                                                         *
*********************************************************************************************************************************/

/**
 * Gets the bitrate.
 *
 * Divide the result by 8 to get the byte rate.
 *
 * @returns Bit rate.
 * @param   pProps              PCM properties to calculate bitrate for.
 */
uint32_t DrvAudioHlpGetBitrate(PCPDMAUDIOPCMPROPS pProps)
{
    return DrvAudioHlpCalcBitrate(pProps->cbSample * 8, pProps->uHz, pProps->cChannels);
}

/**
 * Rounds down the given byte amount to the nearest frame boundrary.
 *
 * @returns Rounded byte amount.
 * @param   pProps      PCM properties to use.
 * @param   cb          The size (in bytes) to round.
 */
uint32_t DrvAudioHlpFloorBytesToFrame(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, 0);
    return PDMAUDIOPCMPROPS_F2B(pProps, PDMAUDIOPCMPROPS_B2F(pProps, cb));
}

/**
 * Checks if the given size is aligned on a frame boundrary.
 *
 * @returns @c true if properly aligned, @c false if not.
 * @param   pProps      PCM properties to use.
 * @param   cb          The size (in bytes) to check.
 */
bool DrvAudioHlpIsBytesAligned(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, false);
    uint32_t const cbFrame = PDMAUDIOPCMPROPS_F2B(pProps, 1 /* Frame */);
    AssertReturn(cbFrame, false);
    return cb % cbFrame == 0;
}

/**
 * Returns the bytes per second for given PCM properties.
 *
 * @returns Bytes per second.
 * @param   pProps              PCM properties to retrieve size for.
 */
DECLINLINE(uint64_t) drvAudioHlpBytesPerSec(PCPDMAUDIOPCMPROPS pProps)
{
    return PDMAUDIOPCMPROPS_F2B(pProps, 1 /* Frame */) * pProps->uHz;
}

/**
 * Converts bytes to frames (rounding down of course).
 *
 * @returns Number of frames.
 * @param   pProps      PCM properties to use.
 * @param   cb          The number of bytes to convert.
 */
uint32_t DrvAudioHlpBytesToFrames(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, 0);
    return PDMAUDIOPCMPROPS_B2F(pProps, cb);
}

/**
 * Converts bytes to milliseconds.
 *
 * @return  Number milliseconds @a cb takes to play or record.
 * @param   pProps      PCM properties to use.
 * @param   cb          The number of bytes to convert.
 *
 * @note    Rounds up the result.
 */
uint64_t DrvAudioHlpBytesToMilli(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, 0);

    /* Check parameters to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
    {
        const unsigned cbFrame = PDMAUDIOPCMPROPS_F2B(pProps, 1 /* Frame */);
        if (cbFrame)
        {
            /* Round cb up to closest frame size: */
            cb = (cb + cbFrame - 1) / cbFrame;

            /* Convert to milliseconds. */
            return (cb * (uint64_t)RT_MS_1SEC + uHz - 1) / uHz;
        }
    }
    return 0;
}

/**
 * Converts bytes to microseconds.
 *
 * @return  Number microseconds @a cb takes to play or record.
 * @param   pProps      PCM properties to use.
 * @param   cb          The number of bytes to convert.
 *
 * @note    Rounds up the result.
 */
uint64_t DrvAudioHlpBytesToMicro(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, 0);

    /* Check parameters to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
    {
        const unsigned cbFrame = PDMAUDIOPCMPROPS_F2B(pProps, 1 /* Frame */);
        if (cbFrame)
        {
            /* Round cb up to closest frame size: */
            cb = (cb + cbFrame - 1) / cbFrame;

            /* Convert to microseconds. */
            return (cb * (uint64_t)RT_US_1SEC + uHz - 1) / uHz;
        }
    }
    return 0;
}

/**
 * Converts bytes to nanoseconds.
 *
 * @return  Number nanoseconds @a cb takes to play or record.
 * @param   pProps      PCM properties to use.
 * @param   cb          The number of bytes to convert.
 *
 * @note    Rounds up the result.
 */
uint64_t DrvAudioHlpBytesToNano(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, 0);

    /* Check parameters to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
    {
        const unsigned cbFrame = PDMAUDIOPCMPROPS_F2B(pProps, 1 /* Frame */);
        if (cbFrame)
        {
            /* Round cb up to closest frame size: */
            cb = (cb + cbFrame - 1) / cbFrame;

            /* Convert to nanoseconds. */
            return (cb * (uint64_t)RT_NS_1SEC + uHz - 1) / uHz;
        }
    }
    return 0;
}

/**
 * Converts frames to bytes.
 *
 * @returns Number of bytes.
 * @param   pProps      The PCM properties to use.
 * @param   cFrames     Number of audio frames to convert.
 * @sa      PDMAUDIOPCMPROPS_F2B
 */
uint32_t DrvAudioHlpFramesToBytes(PCPDMAUDIOPCMPROPS pProps, uint32_t cFrames)
{
    AssertPtrReturn(pProps, 0);
    return PDMAUDIOPCMPROPS_F2B(pProps, cFrames);
}

/**
 * Converts frames to milliseconds.
 *
 * @returns milliseconds.
 * @param   pProps      The PCM properties to use.
 * @param   cFrames     Number of audio frames to convert.
 * @note    No rounding here, result is floored.
 */
uint64_t DrvAudioHlpFramesToMilli(PCPDMAUDIOPCMPROPS pProps, uint32_t cFrames)
{
    AssertPtrReturn(pProps, 0);

    /* Check input to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
        return ASMMultU32ByU32DivByU32(cFrames, RT_MS_1SEC, uHz);
    return 0;
}

/**
 * Converts frames to nanoseconds.
 *
 * @returns Nanoseconds.
 * @param   pProps      The PCM properties to use.
 * @param   cFrames     Number of audio frames to convert.
 * @note    No rounding here, result is floored.
 */
uint64_t DrvAudioHlpFramesToNano(PCPDMAUDIOPCMPROPS pProps, uint32_t cFrames)
{
    AssertPtrReturn(pProps, 0);

    /* Check input to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
        return ASMMultU32ByU32DivByU32(cFrames, RT_NS_1SEC, uHz);
    return 0;
}

/**
 * Converts milliseconds to frames.
 *
 * @returns Number of frames
 * @param   pProps      The PCM properties to use.
 * @param   cMs         The number of milliseconds to convert.
 *
 * @note    The result is rounded rather than floored (hysterical raisins).
 */
uint32_t DrvAudioHlpMilliToFrames(PCPDMAUDIOPCMPROPS pProps, uint64_t cMs)
{
    AssertPtrReturn(pProps, 0);

    uint32_t const uHz = pProps->uHz;
    uint32_t cFrames;
    if (cMs < RT_MS_1SEC)
        cFrames = 0;
    else
    {
        cFrames = cMs / RT_MS_1SEC * uHz;
        cMs %= RT_MS_1SEC;
    }
    cFrames += (ASMMult2xU32RetU64(uHz, (uint32_t)cMs) + RT_MS_1SEC - 1) / RT_MS_1SEC;
    return cFrames;
}

/**
 * Converts milliseconds to bytes.
 *
 * @returns Number of bytes (frame aligned).
 * @param   pProps      The PCM properties to use.
 * @param   cMs         The number of milliseconds to convert.
 *
 * @note    The result is rounded rather than floored (hysterical raisins).
 */
uint32_t DrvAudioHlpMilliToBytes(PCPDMAUDIOPCMPROPS pProps, uint64_t cMs)
{
    return PDMAUDIOPCMPROPS_F2B(pProps, DrvAudioHlpMilliToFrames(pProps, cMs));
}

/**
 * Converts nanoseconds to frames.
 *
 * @returns Number of frames
 * @param   pProps      The PCM properties to use.
 * @param   cNs         The number of nanoseconds to convert.
 *
 * @note    The result is rounded rather than floored (hysterical raisins).
 */
uint32_t DrvAudioHlpNanoToFrames(PCPDMAUDIOPCMPROPS pProps, uint64_t cNs)
{
    AssertPtrReturn(pProps, 0);

    uint32_t const uHz = pProps->uHz;
    uint32_t cFrames;
    if (cNs < RT_NS_1SEC)
        cFrames = 0;
    else
    {
        cFrames = cNs / RT_NS_1SEC * uHz;
        cNs %= RT_NS_1SEC;
    }
    cFrames += (ASMMult2xU32RetU64(uHz, (uint32_t)cNs) + RT_NS_1SEC - 1) / RT_NS_1SEC;
    return cFrames;
}

/**
 * Converts nanoseconds to bytes.
 *
 * @returns Number of bytes (frame aligned).
 * @param   pProps      The PCM properties to use.
 * @param   cNs         The number of nanoseconds to convert.
 *
 * @note    The result is rounded rather than floored (hysterical raisins).
 */
uint32_t DrvAudioHlpNanoToBytes(PCPDMAUDIOPCMPROPS pProps, uint64_t cNs)
{
    return PDMAUDIOPCMPROPS_F2B(pProps, DrvAudioHlpNanoToFrames(pProps, cNs));
}

/**
 * Clears a sample buffer by the given amount of audio frames with silence (according to the format
 * given by the PCM properties).
 *
 * @param   pPCMProps               PCM properties to use for the buffer to clear.
 * @param   pvBuf                   Buffer to clear.
 * @param   cbBuf                   Size (in bytes) of the buffer.
 * @param   cFrames                 Number of audio frames to clear in the buffer.
 */
void DrvAudioHlpClearBuf(PCPDMAUDIOPCMPROPS pPCMProps, void *pvBuf, size_t cbBuf, uint32_t cFrames)
{
    /*
     * Validate input
     */
    AssertPtrReturnVoid(pPCMProps);
    Assert(pPCMProps->cbSample);
    if (!cbBuf || !cFrames)
        return;
    AssertPtrReturnVoid(pvBuf);

    Assert(pPCMProps->fSwapEndian == false); /** @todo Swapping Endianness is not supported yet. */

    /*
     * Decide how much needs clearing.
     */
    size_t cbToClear = DrvAudioHlpFramesToBytes(pPCMProps, cFrames);
    AssertStmt(cbToClear <= cbBuf, cbToClear = cbBuf);

    Log2Func(("pPCMProps=%p, pvBuf=%p, cFrames=%RU32, fSigned=%RTbool, cBytes=%RU8\n",
              pPCMProps, pvBuf, cFrames, pPCMProps->fSigned, pPCMProps->cbSample));

    /*
     * Do the job.
     */
    if (pPCMProps->fSigned)
        RT_BZERO(pvBuf, cbToClear);
    else /* Unsigned formats. */
    {
        switch (pPCMProps->cbSample)
        {
            case 1: /* 8 bit */
                memset(pvBuf, 0x80, cbToClear);
                break;

            case 2: /* 16 bit */
            {
                uint16_t *pu16Dst = (uint16_t *)pvBuf;
                size_t    cLeft   = cbToClear / sizeof(uint16_t);
                while (cLeft-- > 0)
                    *pu16Dst++ = 0x80;
                break;
            }

            /** @todo Add 24 bit? */

            case 4: /* 32 bit */
                ASMMemFill32(pvBuf, cbToClear & ~(size_t)3, 0x80);
                break;

            default:
                AssertMsgFailed(("Invalid bytes per sample: %RU8\n", pPCMProps->cbSample));
        }
    }
}

/**
 * Checks whether two given PCM properties are equal.
 *
 * @returns @c true if equal, @c false if not.
 * @param   pProps1             First properties to compare.
 * @param   pProps2             Second properties to compare.
 */
bool DrvAudioHlpPCMPropsAreEqual(PCPDMAUDIOPCMPROPS pProps1, PCPDMAUDIOPCMPROPS pProps2)
{
    AssertPtrReturn(pProps1, false);
    AssertPtrReturn(pProps2, false);

    if (pProps1 == pProps2) /* If the pointers match, take a shortcut. */
        return true;

    return    pProps1->uHz         == pProps2->uHz
           && pProps1->cChannels   == pProps2->cChannels
           && pProps1->cbSample    == pProps2->cbSample
           && pProps1->fSigned     == pProps2->fSigned
           && pProps1->fSwapEndian == pProps2->fSwapEndian;
}

/**
 * Checks whether given PCM properties are valid or not.
 *
 * Returns @c true if properties are valid, @c false if not.
 * @param   pProps              PCM properties to check.
 */
bool DrvAudioHlpPCMPropsAreValid(PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtrReturn(pProps, false);

    /* Minimum 1 channel (mono), maximum 7.1 (= 8) channels. */
    bool fValid = (   pProps->cChannels >= 1
                   && pProps->cChannels <= 8);

    if (fValid)
    {
        switch (pProps->cbSample)
        {
            case 1: /* 8 bit */
               if (pProps->fSigned)
                   fValid = false;
               break;
            case 2: /* 16 bit */
                if (!pProps->fSigned)
                    fValid = false;
                break;
            /** @todo Do we need support for 24 bit samples? */
            case 4: /* 32 bit */
                if (!pProps->fSigned)
                    fValid = false;
                break;
            default:
                fValid = false;
                break;
        }
    }

    if (!fValid)
        return false;

    fValid &= pProps->uHz > 0;
    fValid &= pProps->cShift == PDMAUDIOPCMPROPS_MAKE_SHIFT_PARMS(pProps->cbSample, pProps->cChannels);
    fValid &= pProps->fSwapEndian == false; /** @todo Handling Big Endian audio data is not supported yet. */

    return fValid;
}

/**
 * Checks whether the given PCM properties are equal with the given
 * stream configuration.
 *
 * @returns @c true if equal, @c false if not.
 * @param   pProps              PCM properties to compare.
 * @param   pCfg                Stream configuration to compare.
 */
bool DrvAudioHlpPCMPropsAreEqual(PCPDMAUDIOPCMPROPS pProps, PCPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pProps, false);
    AssertPtrReturn(pCfg,   false);

    return DrvAudioHlpPCMPropsAreEqual(pProps, &pCfg->Props);
}

/**
 * Get number of bytes per frame.
 *
 * @returns Number of bytes per audio frame.
 * @param   pProps  PCM properties to use.
 * @sa      PDMAUDIOPCMPROPS_F2B
 */
uint32_t DrvAudioHlpBytesPerFrame(PCPDMAUDIOPCMPROPS pProps)
{
    return PDMAUDIOPCMPROPS_F2B(pProps, 1 /*cFrames*/);
}

/**
 * Prints PCM properties to the debug log.
 *
 * @param   pProps              Stream configuration to log.
 */
void DrvAudioHlpPcmPropsLog(PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtrReturnVoid(pProps);

    Log(("uHz=%RU32, cChannels=%RU8, cBits=%RU8%s",
         pProps->uHz, pProps->cChannels, pProps->cbSample * 8, pProps->fSigned ? "S" : "U"));
}


/*********************************************************************************************************************************
*   Audio File Helpers                                                                                                           *
*********************************************************************************************************************************/

/**
 * Sanitizes the file name component so that unsupported characters
 * will be replaced by an underscore ("_").
 *
 * @return  IPRT status code.
 * @param   pszPath             Path to sanitize.
 * @param   cbPath              Size (in bytes) of path to sanitize.
 */
int DrvAudioHlpFileNameSanitize(char *pszPath, size_t cbPath)
{
    RT_NOREF(cbPath);
    int rc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    /* Filter out characters not allowed on Windows platforms, put in by
       RTTimeSpecToString(). */
    /** @todo Use something like RTPathSanitize() if available later some time. */
    static RTUNICP const s_uszValidRangePairs[] =
    {
        ' ', ' ',
        '(', ')',
        '-', '.',
        '0', '9',
        'A', 'Z',
        'a', 'z',
        '_', '_',
        0xa0, 0xd7af,
        '\0'
    };
    ssize_t cReplaced = RTStrPurgeComplementSet(pszPath, s_uszValidRangePairs, '_' /* Replacement */);
    if (cReplaced < 0)
        rc = VERR_INVALID_UTF8_ENCODING;
#else
    RT_NOREF(pszPath);
#endif
    return rc;
}

/**
 * Constructs an unique file name, based on the given path and the audio file type.
 *
 * @returns IPRT status code.
 * @param   pszFile             Where to store the constructed file name.
 * @param   cchFile             Size (in characters) of the file name buffer.
 * @param   pszPath             Base path to use.
 *                              If NULL or empty, the system's temporary directory will be used.
 * @param   pszName             A name for better identifying the file.
 * @param   uInstance           Device / driver instance which is using this file.
 * @param   enmType             Audio file type to construct file name for.
 * @param   fFlags              File naming flags, PDMAUDIOFILENAME_FLAGS_XXX.
 */
int DrvAudioHlpFileNameGet(char *pszFile, size_t cchFile, const char *pszPath, const char *pszName,
                           uint32_t uInstance, PDMAUDIOFILETYPE enmType, uint32_t fFlags)
{
    AssertPtrReturn(pszFile, VERR_INVALID_POINTER);
    AssertReturn(cchFile,    VERR_INVALID_PARAMETER);
    /* pszPath can be NULL. */
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    /** @todo Validate fFlags. */

    int rc;

    char *pszPathTmp = NULL;

    do
    {
        if (   pszPath == NULL
            || !strlen(pszPath))
        {
            char szTemp[RTPATH_MAX];
            rc = RTPathTemp(szTemp, sizeof(szTemp));
            if (RT_SUCCESS(rc))
            {
                pszPathTmp = RTStrDup(szTemp);
            }
            else
                break;
        }
        else
            pszPathTmp = RTStrDup(pszPath);

        AssertPtrBreakStmt(pszPathTmp, rc = VERR_NO_MEMORY);

        char szFilePath[RTPATH_MAX];
        rc = RTStrCopy(szFilePath, sizeof(szFilePath), pszPathTmp);
        AssertRCBreak(rc);

        /* Create it when necessary. */
        if (!RTDirExists(szFilePath))
        {
            rc = RTDirCreateFullPath(szFilePath, RTFS_UNIX_IRWXU);
            if (RT_FAILURE(rc))
                break;
        }

        char szFileName[RTPATH_MAX];
        szFileName[0] = '\0';

        if (fFlags & PDMAUDIOFILENAME_FLAGS_TS)
        {
            RTTIMESPEC time;
            if (!RTTimeSpecToString(RTTimeNow(&time), szFileName, sizeof(szFileName)))
            {
                rc = VERR_BUFFER_OVERFLOW;
                break;
            }

            rc = DrvAudioHlpFileNameSanitize(szFileName, sizeof(szFileName));
            if (RT_FAILURE(rc))
                break;

            rc = RTStrCat(szFileName, sizeof(szFileName), "-");
            if (RT_FAILURE(rc))
                break;
        }

        rc = RTStrCat(szFileName, sizeof(szFileName), pszName);
        if (RT_FAILURE(rc))
            break;

        rc = RTStrCat(szFileName, sizeof(szFileName), "-");
        if (RT_FAILURE(rc))
            break;

        char szInst[16];
        RTStrPrintf2(szInst, sizeof(szInst), "%RU32", uInstance);
        rc = RTStrCat(szFileName, sizeof(szFileName), szInst);
        if (RT_FAILURE(rc))
            break;

        switch (enmType)
        {
            case PDMAUDIOFILETYPE_RAW:
                rc = RTStrCat(szFileName, sizeof(szFileName), ".pcm");
                break;

            case PDMAUDIOFILETYPE_WAV:
                rc = RTStrCat(szFileName, sizeof(szFileName), ".wav");
                break;

            default:
                AssertFailedStmt(rc = VERR_NOT_IMPLEMENTED);
                break;
        }

        if (RT_FAILURE(rc))
            break;

        rc = RTPathAppend(szFilePath, sizeof(szFilePath), szFileName);
        if (RT_FAILURE(rc))
            break;

        rc = RTStrCopy(pszFile, cchFile, szFilePath);

    } while (0);

    RTStrFree(pszPathTmp);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates an audio file.
 *
 * @returns IPRT status code.
 * @param   enmType             Audio file type to open / create.
 * @param   pszFile             File path of file to open or create.
 * @param   fFlags              Audio file flags, PDMAUDIOFILE_FLAGS_XXX.
 * @param   ppFile              Where to store the created audio file handle.
 *                              Needs to be destroyed with DrvAudioHlpFileDestroy().
 */
int DrvAudioHlpFileCreate(PDMAUDIOFILETYPE enmType, const char *pszFile, uint32_t fFlags, PPDMAUDIOFILE *ppFile)
{
    AssertPtrReturn(pszFile, VERR_INVALID_POINTER);
    /** @todo Validate fFlags. */

    PPDMAUDIOFILE pFile = (PPDMAUDIOFILE)RTMemAlloc(sizeof(PDMAUDIOFILE));
    if (!pFile)
        return VERR_NO_MEMORY;

    int rc = VINF_SUCCESS;

    switch (enmType)
    {
        case PDMAUDIOFILETYPE_RAW:
        case PDMAUDIOFILETYPE_WAV:
            pFile->enmType = enmType;
            break;

        default:
            rc = VERR_INVALID_PARAMETER;
            break;
    }

    if (RT_SUCCESS(rc))
    {
        RTStrPrintf(pFile->szName, RT_ELEMENTS(pFile->szName), "%s", pszFile);
        pFile->hFile  = NIL_RTFILE;
        pFile->fFlags = fFlags;
        pFile->pvData = NULL;
        pFile->cbData = 0;
    }

    if (RT_FAILURE(rc))
    {
        RTMemFree(pFile);
        pFile = NULL;
    }
    else
        *ppFile = pFile;

    return rc;
}

/**
 * Destroys a formerly created audio file.
 *
 * @param   pFile               Audio file (object) to destroy.
 */
void DrvAudioHlpFileDestroy(PPDMAUDIOFILE pFile)
{
    if (!pFile)
        return;

    DrvAudioHlpFileClose(pFile);

    RTMemFree(pFile);
    pFile = NULL;
}

/**
 * Opens or creates an audio file.
 *
 * @returns IPRT status code.
 * @param   pFile               Pointer to audio file handle to use.
 * @param   fOpen               Open flags.
 *                              Use PDMAUDIOFILE_DEFAULT_OPEN_FLAGS for the default open flags.
 * @param   pProps              PCM properties to use.
 */
int DrvAudioHlpFileOpen(PPDMAUDIOFILE pFile, uint32_t fOpen, PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtrReturn(pFile,   VERR_INVALID_POINTER);
    /** @todo Validate fOpen flags. */
    AssertPtrReturn(pProps,  VERR_INVALID_POINTER);

    int rc;

    if (pFile->enmType == PDMAUDIOFILETYPE_RAW)
    {
        rc = RTFileOpen(&pFile->hFile, pFile->szName, fOpen);
    }
    else if (pFile->enmType == PDMAUDIOFILETYPE_WAV)
    {
        Assert(pProps->cChannels);
        Assert(pProps->uHz);
        Assert(pProps->cbSample);

        pFile->pvData = (PAUDIOWAVFILEDATA)RTMemAllocZ(sizeof(AUDIOWAVFILEDATA));
        if (pFile->pvData)
        {
            pFile->cbData = sizeof(PAUDIOWAVFILEDATA);

            PAUDIOWAVFILEDATA pData = (PAUDIOWAVFILEDATA)pFile->pvData;
            AssertPtr(pData);

            /* Header. */
            pData->Hdr.u32RIFF          = AUDIO_MAKE_FOURCC('R','I','F','F');
            pData->Hdr.u32Size          = 36;
            pData->Hdr.u32WAVE          = AUDIO_MAKE_FOURCC('W','A','V','E');

            pData->Hdr.u32Fmt           = AUDIO_MAKE_FOURCC('f','m','t',' ');
            pData->Hdr.u32Size1         = 16; /* Means PCM. */
            pData->Hdr.u16AudioFormat   = 1;  /* PCM, linear quantization. */
            pData->Hdr.u16NumChannels   = pProps->cChannels;
            pData->Hdr.u32SampleRate    = pProps->uHz;
            pData->Hdr.u32ByteRate      = DrvAudioHlpGetBitrate(pProps) / 8;
            pData->Hdr.u16BlockAlign    = pProps->cChannels * pProps->cbSample;
            pData->Hdr.u16BitsPerSample = pProps->cbSample * 8;

            /* Data chunk. */
            pData->Hdr.u32ID2           = AUDIO_MAKE_FOURCC('d','a','t','a');
            pData->Hdr.u32Size2         = 0;

            rc = RTFileOpen(&pFile->hFile, pFile->szName, fOpen);
            if (RT_SUCCESS(rc))
            {
                rc = RTFileWrite(pFile->hFile, &pData->Hdr, sizeof(pData->Hdr), NULL);
                if (RT_FAILURE(rc))
                {
                    RTFileClose(pFile->hFile);
                    pFile->hFile = NIL_RTFILE;
                }
            }

            if (RT_FAILURE(rc))
            {
                RTMemFree(pFile->pvData);
                pFile->pvData = NULL;
                pFile->cbData = 0;
            }
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    if (RT_SUCCESS(rc))
    {
        LogRel2(("Audio: Opened file '%s'\n", pFile->szName));
    }
    else
        LogRel(("Audio: Failed opening file '%s', rc=%Rrc\n", pFile->szName, rc));

    return rc;
}

/**
 * Closes an audio file.
 *
 * @returns IPRT status code.
 * @param   pFile               Audio file handle to close.
 */
int DrvAudioHlpFileClose(PPDMAUDIOFILE pFile)
{
    if (!pFile)
        return VINF_SUCCESS;

    size_t cbSize = DrvAudioHlpFileGetDataSize(pFile);

    int rc = VINF_SUCCESS;

    if (pFile->enmType == PDMAUDIOFILETYPE_RAW)
    {
        if (RTFileIsValid(pFile->hFile))
            rc = RTFileClose(pFile->hFile);
    }
    else if (pFile->enmType == PDMAUDIOFILETYPE_WAV)
    {
        if (RTFileIsValid(pFile->hFile))
        {
            PAUDIOWAVFILEDATA pData = (PAUDIOWAVFILEDATA)pFile->pvData;
            if (pData) /* The .WAV file data only is valid when a file actually has been created. */
            {
                /* Update the header with the current data size. */
                RTFileWriteAt(pFile->hFile, 0, &pData->Hdr, sizeof(pData->Hdr), NULL);
            }

            rc = RTFileClose(pFile->hFile);
        }

        if (pFile->pvData)
        {
            RTMemFree(pFile->pvData);
            pFile->pvData = NULL;
        }
    }
    else
        AssertFailedStmt(rc = VERR_NOT_IMPLEMENTED);

    if (   RT_SUCCESS(rc)
        && !cbSize
        && !(pFile->fFlags & PDMAUDIOFILE_FLAGS_KEEP_IF_EMPTY))
    {
        rc = DrvAudioHlpFileDelete(pFile);
    }

    pFile->cbData = 0;

    if (RT_SUCCESS(rc))
    {
        pFile->hFile = NIL_RTFILE;
        LogRel2(("Audio: Closed file '%s' (%zu bytes)\n", pFile->szName, cbSize));
    }
    else
        LogRel(("Audio: Failed closing file '%s', rc=%Rrc\n", pFile->szName, rc));

    return rc;
}

/**
 * Deletes an audio file.
 *
 * @returns IPRT status code.
 * @param   pFile               Audio file handle to delete.
 */
int DrvAudioHlpFileDelete(PPDMAUDIOFILE pFile)
{
    AssertPtrReturn(pFile, VERR_INVALID_POINTER);

    int rc = RTFileDelete(pFile->szName);
    if (RT_SUCCESS(rc))
    {
        LogRel2(("Audio: Deleted file '%s'\n", pFile->szName));
    }
    else if (rc == VERR_FILE_NOT_FOUND) /* Don't bitch if the file is not around (anymore). */
        rc = VINF_SUCCESS;

    if (RT_FAILURE(rc))
        LogRel(("Audio: Failed deleting file '%s', rc=%Rrc\n", pFile->szName, rc));

    return rc;
}

/**
 * Returns the raw audio data size of an audio file.
 *
 * Note: This does *not* include file headers and other data which does
 *       not belong to the actual PCM audio data.
 *
 * @returns Size (in bytes) of the raw PCM audio data.
 * @param   pFile               Audio file handle to retrieve the audio data size for.
 */
size_t DrvAudioHlpFileGetDataSize(PPDMAUDIOFILE pFile)
{
    AssertPtrReturn(pFile, 0);

    size_t cbSize = 0;

    if (pFile->enmType == PDMAUDIOFILETYPE_RAW)
    {
        cbSize = RTFileTell(pFile->hFile);
    }
    else if (pFile->enmType == PDMAUDIOFILETYPE_WAV)
    {
        PAUDIOWAVFILEDATA pData = (PAUDIOWAVFILEDATA)pFile->pvData;
        if (pData) /* The .WAV file data only is valid when a file actually has been created. */
            cbSize = pData->Hdr.u32Size2;
    }

    return cbSize;
}

/**
 * Returns whether the given audio file is open and in use or not.
 *
 * @return  bool                True if open, false if not.
 * @param   pFile               Audio file handle to check open status for.
 */
bool DrvAudioHlpFileIsOpen(PPDMAUDIOFILE pFile)
{
    if (!pFile)
        return false;

    return RTFileIsValid(pFile->hFile);
}

/**
 * Write PCM data to a wave (.WAV) file.
 *
 * @returns IPRT status code.
 * @param   pFile               Audio file handle to write PCM data to.
 * @param   pvBuf               Audio data to write.
 * @param   cbBuf               Size (in bytes) of audio data to write.
 * @param   fFlags              Additional write flags. Not being used at the moment and must be 0.
 */
int DrvAudioHlpFileWrite(PPDMAUDIOFILE pFile, const void *pvBuf, size_t cbBuf, uint32_t fFlags)
{
    AssertPtrReturn(pFile, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);

    AssertReturn(fFlags == 0, VERR_INVALID_PARAMETER); /** @todo fFlags are currently not implemented. */

    if (!cbBuf)
        return VINF_SUCCESS;

    AssertReturn(RTFileIsValid(pFile->hFile), VERR_WRONG_ORDER);

    int rc;

    if (pFile->enmType == PDMAUDIOFILETYPE_RAW)
    {
        rc = RTFileWrite(pFile->hFile, pvBuf, cbBuf, NULL);
    }
    else if (pFile->enmType == PDMAUDIOFILETYPE_WAV)
    {
        PAUDIOWAVFILEDATA pData = (PAUDIOWAVFILEDATA)pFile->pvData;
        AssertPtr(pData);

        rc = RTFileWrite(pFile->hFile, pvBuf, cbBuf, NULL);
        if (RT_SUCCESS(rc))
        {
            pData->Hdr.u32Size  += (uint32_t)cbBuf;
            pData->Hdr.u32Size2 += (uint32_t)cbBuf;
        }
    }
    else
        rc = VERR_NOT_SUPPORTED;

    return rc;
}

