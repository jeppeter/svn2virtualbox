/*
 * $XFree86: xc/programs/Xserver/miext/shadow/shadow.h,v 1.6tsi Exp $
 *
 * Copyright © 2000 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SHADOW_H_
#define _SHADOW_H_

#include "scrnintstr.h"

#ifdef RENDER
#include "picturestr.h"
#endif

typedef struct _shadowBuf   *shadowBufPtr;

typedef void (*ShadowUpdateProc) (ScreenPtr pScreen,
				  shadowBufPtr pBuf);

#define SHADOW_WINDOW_RELOCATE 1
#define SHADOW_WINDOW_READ 2
#define SHADOW_WINDOW_WRITE 4

typedef void *(*ShadowWindowProc) (ScreenPtr	pScreen,
				   CARD32	row,
				   CARD32	offset,
				   int		mode,
				   CARD32	*size,
				   void		*closure);

typedef struct _shadowBuf {
    shadowBufPtr	pNext;
    ShadowUpdateProc	update;
    ShadowWindowProc	window;
    RegionRec		damage;
    PixmapPtr		pPixmap;
    void		*closure;
    int			randr;
} shadowBufRec;

/* Match defines from randr extension */
#define SHADOW_ROTATE_0	    1
#define SHADOW_ROTATE_90    2
#define SHADOW_ROTATE_180   4
#define SHADOW_ROTATE_270   8
#define SHADOW_ROTATE_ALL   (SHADOW_ROTATE_0|SHADOW_ROTATE_90|\
			     SHADOW_ROTATE_180|SHADOW_ROTATE_270)
#define SHADOW_REFLECT_X    16
#define SHADOW_REFLECT_Y    32
#define SHADOW_REFLECT_ALL  (SHADOW_REFLECT_X|SHADOW_REFLECT_Y)

typedef struct _shadowScrPriv {
    PaintWindowBackgroundProcPtr PaintWindowBackground;
    PaintWindowBorderProcPtr	PaintWindowBorder;
    CopyWindowProcPtr		CopyWindow;
    CloseScreenProcPtr		CloseScreen;
    CreateGCProcPtr		CreateGC;
    GetImageProcPtr		GetImage;
#ifdef RENDER
    CompositeProcPtr		Composite;
    GlyphsProcPtr		Glyphs;
#endif
    shadowBufPtr		pBuf;
    BSFuncRec			BackingStoreFuncs;
} shadowScrPrivRec, *shadowScrPrivPtr;

extern int shadowScrPrivateIndex;

#define shadowGetScrPriv(pScr)  ((shadowScrPrivPtr) (pScr)->devPrivates[shadowScrPrivateIndex].ptr)
#define shadowScrPriv(pScr)	shadowScrPrivPtr    pScrPriv = shadowGetScrPriv(pScr)

Bool
shadowSetup (ScreenPtr pScreen);

Bool
shadowAdd (ScreenPtr	    pScreen,
	   PixmapPtr	    pPixmap,
	   ShadowUpdateProc update,
	   ShadowWindowProc window,
	   int		    randr,
	   void		    *closure);

void
shadowRemove (ScreenPtr pScreen, PixmapPtr pPixmap);

shadowBufPtr
shadowFindBuf (WindowPtr pWindow);

Bool
shadowInit (ScreenPtr pScreen, ShadowUpdateProc update, ShadowWindowProc window);

void *
shadowAlloc (int width, int height, int bpp);

void
shadowUpdatePacked (ScreenPtr	    pScreen,
		    shadowBufPtr    pBuf);

void
shadowUpdatePlanar4 (ScreenPtr	    pScreen,
		     shadowBufPtr   pBuf);

void
shadowUpdatePlanar4x8 (ScreenPtr    pScreen,
		       shadowBufPtr pBuf);

void
shadowUpdateRotatePacked (ScreenPtr    pScreen,
			  shadowBufPtr pBuf);

void
shadowUpdateRotate8_90 (ScreenPtr    pScreen,
			shadowBufPtr pBuf);

void
shadowUpdateRotate16_90 (ScreenPtr    pScreen,
			 shadowBufPtr pBuf);

void
shadowUpdateRotate32_90 (ScreenPtr    pScreen,
			 shadowBufPtr pBuf);

void
shadowUpdateRotate8_180 (ScreenPtr    pScreen,
			 shadowBufPtr pBuf);

void
shadowUpdateRotate16_180 (ScreenPtr    pScreen,
			  shadowBufPtr pBuf);

void
shadowUpdateRotate32_180 (ScreenPtr    pScreen,
			  shadowBufPtr pBuf);

void
shadowUpdateRotate8_270 (ScreenPtr    pScreen,
			 shadowBufPtr pBuf);

void
shadowUpdateRotate16_270 (ScreenPtr    pScreen,
			  shadowBufPtr pBuf);

void
shadowUpdateRotate32_270 (ScreenPtr    pScreen,
			  shadowBufPtr pBuf);

typedef void (* shadowUpdateProc)(ScreenPtr, shadowBufPtr);

shadowUpdateProc shadowUpdatePackedWeak(void);
shadowUpdateProc shadowUpdatePlanar4Weak(void);
shadowUpdateProc shadowUpdatePlanar4x8Weak(void);
shadowUpdateProc shadowUpdateRotatePackedWeak(void);

void
shadowWrapGC (GCPtr pGC);

void
shadowUnwrapGC (GCPtr pGC);

#endif /* _SHADOW_H_ */
