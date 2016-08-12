/* $Id$ */
/** @file
 * VBox audio - Mixing buffer to convert audio samples to/from different rates / formats.
 */

/*
 * Copyright (C) 2014-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef AUDIO_MIXBUF_H
#define AUDIO_MIXBUF_H

#include <iprt/cdefs.h>
#include <VBox/vmm/pdmaudioifs.h>

/** Constructs 32 bit value for given frequency, number of channels, bits per sample and signed bit.
 *  Note: This currently matches 1:1 the VRDE encoding -- this might change in the future, so better don't rely on this fact! */
#define AUDMIXBUF_AUDIO_FMT_MAKE(freq, c, bps, s) ((((s) & 0x1) << 28) + (((bps) & 0xFF) << 20) + (((c) & 0xF) << 16) + ((freq) & 0xFFFF))

/** Decodes frequency (Hz). */
#define AUDMIXBUF_FMT_SAMPLE_FREQ(a) ((a) & 0xFFFF)
/** Decodes number of channels. */
#define AUDMIXBUF_FMT_CHANNELS(a) (((a) >> 16) & 0xF)
/** Decodes signed bit. */
#define AUDMIXBUF_FMT_SIGNED(a) (((a) >> 28) & 0x1)
/** Decodes number of bits per sample. */
#define AUDMIXBUF_FMT_BITS_PER_SAMPLE(a) (((a) >> 20) & 0xFF)
/** Decodes number of bytes per sample. */
#define AUDMIXBUF_FMT_BYTES_PER_SAMPLE(a) ((AUDMIXBUF_AUDIO_FMT_BITS_PER_SAMPLE(a) + 7) / 8)

/** Converts samples to bytes. */
#define AUDIOMIXBUF_S2B(pBuf, samples) ((samples) << (pBuf)->cShift)
/** Converts samples to bytes, respecting the conversion ratio to
 *  a linked buffer. */
#define AUDIOMIXBUF_S2B_RATIO(pBuf, samples) ((((int64_t) samples << 32) / (pBuf)->iFreqRatio) << (pBuf)->cShift)
/** Converts bytes to samples, *not* taking the conversion ratio
 *  into account. */
#define AUDIOMIXBUF_B2S(pBuf, cb)  (cb >> (pBuf)->cShift)
/** Converts number of samples according to the buffer's ratio. */
#define AUDIOMIXBUF_S2S_RATIO(pBuf, samples)  (((int64_t) samples << 32) / (pBuf)->iFreqRatio)


int AudioMixBufAcquire(PPDMAUDIOMIXBUF pMixBuf, uint32_t cSamplesToRead, PPDMAUDIOSAMPLE *ppvSamples, uint32_t *pcSamplesRead);
inline uint32_t AudioMixBufBytesToSamples(PPDMAUDIOMIXBUF pMixBuf);
void AudioMixBufClear(PPDMAUDIOMIXBUF pMixBuf);
void AudioMixBufDestroy(PPDMAUDIOMIXBUF pMixBuf);
void AudioMixBufFinish(PPDMAUDIOMIXBUF pMixBuf, uint32_t cSamplesToClear);
uint32_t AudioMixBufFree(PPDMAUDIOMIXBUF pMixBuf);
uint32_t AudioMixBufFreeBytes(PPDMAUDIOMIXBUF pMixBuf);
int AudioMixBufInit(PPDMAUDIOMIXBUF pMixBuf, const char *pszName, PPDMAUDIOPCMPROPS pProps, uint32_t cSamples);
bool AudioMixBufIsEmpty(PPDMAUDIOMIXBUF pMixBuf);
int AudioMixBufLinkTo(PPDMAUDIOMIXBUF pMixBuf, PPDMAUDIOMIXBUF pParent);
uint32_t AudioMixBufLive(PPDMAUDIOMIXBUF pMixBuf);
int AudioMixBufMixToParent(PPDMAUDIOMIXBUF pMixBuf, uint32_t cSamples, uint32_t *pcProcessed);
uint32_t AudioMixBufUsed(PPDMAUDIOMIXBUF pMixBuf);
int AudioMixBufReadAt(PPDMAUDIOMIXBUF pMixBuf, uint32_t offSamples, void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead);
int AudioMixBufReadAtEx(PPDMAUDIOMIXBUF pMixBuf, PDMAUDIOMIXBUFFMT enmFmt, uint32_t offSamples, void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead);
int AudioMixBufReadCirc(PPDMAUDIOMIXBUF pMixBuf, void *pvBuf, uint32_t cbBuf, uint32_t *pcRead);
int AudioMixBufReadCircEx(PPDMAUDIOMIXBUF pMixBuf, PDMAUDIOMIXBUFFMT enmFmt, void *pvBuf, uint32_t cbBuf, uint32_t *pcRead);
void AudioMixBufReset(PPDMAUDIOMIXBUF pMixBuf);
void AudioMixBufSetVolume(PPDMAUDIOMIXBUF pMixBuf, PPDMAUDIOVOLUME pVol);
uint32_t AudioMixBufSize(PPDMAUDIOMIXBUF pMixBuf);
uint32_t AudioMixBufSizeBytes(PPDMAUDIOMIXBUF pMixBuf);
void AudioMixBufUnlink(PPDMAUDIOMIXBUF pMixBuf);
int AudioMixBufWriteAt(PPDMAUDIOMIXBUF pMixBuf, uint32_t offSamples, const void *pvBuf, uint32_t cbBuf, uint32_t *pcWritten);
int AudioMixBufWriteAtEx(PPDMAUDIOMIXBUF pMixBuf, PDMAUDIOMIXBUFFMT enmFmt, uint32_t offSamples, const void *pvBuf, uint32_t cbBuf, uint32_t *pcWritten);
int AudioMixBufWriteCirc(PPDMAUDIOMIXBUF pMixBuf, const void *pvBuf, uint32_t cbBuf, uint32_t *pcWritten);
int AudioMixBufWriteCircEx(PPDMAUDIOMIXBUF pMixBuf, PDMAUDIOMIXBUFFMT enmFmt, const void *pvBuf, uint32_t cbBuf, uint32_t *pcWritten);

#ifdef DEBUG
void AudioMixBufDbgPrint(PPDMAUDIOMIXBUF pMixBuf);
void AudioMixBufDbgPrintChain(PPDMAUDIOMIXBUF pMixBuf);
#endif

#endif

