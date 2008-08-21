/*
 * Copyright © 2000 Compaq Computer Corporation, Inc.
 * Copyright © 2002 Hewlett-Packard Company, Inc.
 * Copyright © 2006 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 * Author:  Jim Gettys, HP Labs, Hewlett-Packard, Inc.
 *	    Keith Packard, Intel Corporation
 */

#ifndef _XRANDR_H_
#define _XRANDR_H_

#include <X11/extensions/randr.h>

#include <X11/Xfuncproto.h>

_XFUNCPROTOBEGIN

typedef XID RROutput;
typedef XID RRCrtc;
typedef XID RRMode;

typedef struct {
    int	width, height;
    int	mwidth, mheight;
} XRRScreenSize;

/*
 *  Events.
 */

typedef struct {
    int type;			/* event base */
    unsigned long serial;	/* # of last request processed by server */
    Bool send_event;		/* true if this came from a SendEvent request */
    Display *display;		/* Display the event was read from */
    Window window;		/* window which selected for this event */
    Window root;		/* Root window for changed screen */
    Time timestamp;		/* when the screen change occurred */
    Time config_timestamp;	/* when the last configuration change */
    SizeID size_index;
    SubpixelOrder subpixel_order;
    Rotation rotation;
    int width;
    int height;
    int mwidth;
    int mheight;
} XRRScreenChangeNotifyEvent;

typedef struct {
    int type;			/* event base */
    unsigned long serial;	/* # of last request processed by server */
    Bool send_event;		/* true if this came from a SendEvent request */
    Display *display;		/* Display the event was read from */
    Window window;		/* window which selected for this event */
    int subtype;		/* RRNotify_ subtype */
} XRRNotifyEvent;

typedef struct {
    int type;			/* event base */
    unsigned long serial;	/* # of last request processed by server */
    Bool send_event;		/* true if this came from a SendEvent request */
    Display *display;		/* Display the event was read from */
    Window window;		/* window which selected for this event */
    int subtype;		/* RRNotify_OutputChange */
    RROutput output;		/* affected output */
    RRCrtc crtc;	    	/* current crtc (or None) */
    RRMode mode;	    	/* current mode (or None) */
    Rotation rotation;		/* current rotation of associated crtc */
    Connection connection;	/* current connection status */
    SubpixelOrder subpixel_order;
} XRROutputChangeNotifyEvent;

typedef struct {
    int type;			/* event base */
    unsigned long serial;	/* # of last request processed by server */
    Bool send_event;		/* true if this came from a SendEvent request */
    Display *display;		/* Display the event was read from */
    Window window;		/* window which selected for this event */
    int subtype;		/* RRNotify_CrtcChange */
    RRCrtc crtc;    		/* current crtc (or None) */
    RRMode mode;	    	/* current mode (or None) */
    Rotation rotation;		/* current rotation of associated crtc */
    int x, y;			/* position */
    unsigned int width, height;	/* size */
} XRRCrtcChangeNotifyEvent;

typedef struct {
    int type;			/* event base */
    unsigned long serial;	/* # of last request processed by server */
    Bool send_event;		/* true if this came from a SendEvent request */
    Display *display;		/* Display the event was read from */
    Window window;		/* window which selected for this event */
    int subtype;		/* RRNotify_OutputProperty */
    RROutput output;		/* related output */
    Atom property;		/* changed property */
    Time timestamp;		/* time of change */
    int state;			/* NewValue, Deleted */
} XRROutputPropertyNotifyEvent;

/* internal representation is private to the library */
typedef struct _XRRScreenConfiguration XRRScreenConfiguration;	

Bool XRRQueryExtension (Display *dpy, int *event_basep, int *error_basep);
Status XRRQueryVersion (Display *dpy,
			    int     *major_versionp,
			    int     *minor_versionp);

XRRScreenConfiguration *XRRGetScreenInfo (Display *dpy,
					  Window window);
    
void XRRFreeScreenConfigInfo (XRRScreenConfiguration *config);

/* 
 * Note that screen configuration changes are only permitted if the client can
 * prove it has up to date configuration information.  We are trying to
 * insist that it become possible for screens to change dynamically, so
 * we want to ensure the client knows what it is talking about when requesting
 * changes.
 */
Status XRRSetScreenConfig (Display *dpy, 
			   XRRScreenConfiguration *config,
			   Drawable draw,
			   int size_index,
			   Rotation rotation,
			   Time timestamp);

/* added in v1.1, sorry for the lame name */
Status XRRSetScreenConfigAndRate (Display *dpy, 
				  XRRScreenConfiguration *config,
				  Drawable draw,
				  int size_index,
				  Rotation rotation,
				  short rate,
				  Time timestamp);


Rotation XRRConfigRotations(XRRScreenConfiguration *config, Rotation *current_rotation);

Time XRRConfigTimes (XRRScreenConfiguration *config, Time *config_timestamp);

XRRScreenSize *XRRConfigSizes(XRRScreenConfiguration *config, int *nsizes);

short *XRRConfigRates (XRRScreenConfiguration *config, int sizeID, int *nrates);

SizeID XRRConfigCurrentConfiguration (XRRScreenConfiguration *config, 
			      Rotation *rotation);
    
short XRRConfigCurrentRate (XRRScreenConfiguration *config);

int XRRRootToScreen(Display *dpy, Window root);

/* 
 * returns the screen configuration for the specified screen; does a lazy
 * evalution to delay getting the information, and caches the result.
 * These routines should be used in preference to XRRGetScreenInfo
 * to avoid unneeded round trips to the X server.  These are new
 * in protocol version 0.1.
 */


XRRScreenConfiguration *XRRScreenConfig(Display *dpy, int screen);
XRRScreenConfiguration *XRRConfig(Screen *screen);
void XRRSelectInput(Display *dpy, Window window, int mask);

/* 
 * the following are always safe to call, even if RandR is not implemented 
 * on a screen 
 */


Rotation XRRRotations(Display *dpy, int screen, Rotation *current_rotation);
XRRScreenSize *XRRSizes(Display *dpy, int screen, int *nsizes);
short *XRRRates (Display *dpy, int screen, int sizeID, int *nrates);
Time XRRTimes (Display *dpy, int screen, Time *config_timestamp);


/* Version 1.2 additions */

Status
XRRGetScreenSizeRange (Display *dpy, Window window,
		       int *minWidth, int *minHeight,
		       int *maxWidth, int *maxHeight);

void
XRRSetScreenSize (Display *dpy, Window window,
		  int width, int height,
		  int mmWidth, int mmHeight);

typedef unsigned long XRRModeFlags;

typedef struct _XRRModeInfo {
    RRMode		id;
    unsigned int	width;
    unsigned int	height;
    unsigned long	dotClock;
    unsigned int	hSyncStart;
    unsigned int	hSyncEnd;
    unsigned int	hTotal;
    unsigned int	hSkew;
    unsigned int	vSyncStart;
    unsigned int	vSyncEnd;
    unsigned int	vTotal;
    char		*name;
    unsigned int	nameLength;
    XRRModeFlags	modeFlags;
} XRRModeInfo;

typedef struct _XRRScreenResources {
    Time	timestamp;
    Time	configTimestamp;
    int		ncrtc;
    RRCrtc	*crtcs;
    int		noutput;
    RROutput	*outputs;
    int		nmode;
    XRRModeInfo	*modes;
} XRRScreenResources;
    
XRRScreenResources *
XRRGetScreenResources (Display *dpy, Window window);

void
XRRFreeScreenResources (XRRScreenResources *resources);

typedef struct _XRROutputInfo {
    Time	    timestamp;
    RRCrtc	    crtc;
    char	    *name;
    int		    nameLen;
    unsigned long   mm_width;
    unsigned long   mm_height;
    Connection	    connection;
    SubpixelOrder   subpixel_order;
    int		    ncrtc;
    RRCrtc	    *crtcs;
    int		    nclone;
    RROutput	    *clones;
    int		    nmode;
    int		    npreferred;
    RRMode	    *modes;
} XRROutputInfo;

XRROutputInfo *
XRRGetOutputInfo (Display *dpy, XRRScreenResources *resources, RROutput output);

void
XRRFreeOutputInfo (XRROutputInfo *outputInfo);

Atom *
XRRListOutputProperties (Display *dpy, RROutput output, int *nprop);

typedef struct {
    Bool    pending;
    Bool    range;
    Bool    immutable;
    int	    num_values;
    long    *values;
} XRRPropertyInfo;

XRRPropertyInfo *
XRRQueryOutputProperty (Display *dpy, RROutput output, Atom property);

void
XRRConfigureOutputProperty (Display *dpy, RROutput output, Atom property,
			    Bool pending, Bool range, int num_values,
			    long *values);
			
void
XRRChangeOutputProperty (Display *dpy, RROutput output,
			 Atom property, Atom type,
			 int format, int mode,
			 _Xconst unsigned char *data, int nelements);

void
XRRDeleteOutputProperty (Display *dpy, RROutput output, Atom property);

int
XRRGetOutputProperty (Display *dpy, RROutput output,
		      Atom property, long offset, long length,
		      Bool _delete, Bool pending, Atom req_type,
		      Atom *actual_type, int *actual_format,
		      unsigned long *nitems, unsigned long *bytes_after,
		      unsigned char **prop);

XRRModeInfo *
XRRAllocModeInfo (char *name, int nameLength);

RRMode
XRRCreateMode (Display *dpy, Window window, XRRModeInfo *modeInfo);

void
XRRDestroyMode (Display *dpy, RRMode mode);

void
XRRAddOutputMode (Display *dpy, RROutput output, RRMode mode);

void
XRRDeleteOutputMode (Display *dpy, RROutput output, RRMode mode);

void
XRRFreeModeInfo (XRRModeInfo *modeInfo);
		      
typedef struct _XRRCrtcInfo {
    Time	    timestamp;
    int		    x, y;
    unsigned int    width, height;
    RRMode	    mode;
    Rotation	    rotation;
    int		    noutput;
    RROutput	    *outputs;
    Rotation	    rotations;
    int		    npossible;
    RROutput	    *possible;
} XRRCrtcInfo;

XRRCrtcInfo *
XRRGetCrtcInfo (Display *dpy, XRRScreenResources *resources, RRCrtc crtc);

void
XRRFreeCrtcInfo (XRRCrtcInfo *crtcInfo);

Status
XRRSetCrtcConfig (Display *dpy,
		  XRRScreenResources *resources,
		  RRCrtc crtc,
		  Time timestamp,
		  int x, int y,
		  RRMode mode,
		  Rotation rotation,
		  RROutput *outputs,
		  int noutputs);

int
XRRGetCrtcGammaSize (Display *dpy, RRCrtc crtc);

typedef struct _XRRCrtcGamma {
    int		    size;
    unsigned short  *red;
    unsigned short  *green;
    unsigned short  *blue;
} XRRCrtcGamma;

XRRCrtcGamma *
XRRGetCrtcGamma (Display *dpy, RRCrtc crtc);

XRRCrtcGamma *
XRRAllocGamma (int size);

void
XRRSetCrtcGamma (Display *dpy, RRCrtc crtc, XRRCrtcGamma *gamma);

void
XRRFreeGamma (XRRCrtcGamma *gamma);

/* 
 * intended to take RRScreenChangeNotify,  or 
 * ConfigureNotify (on the root window)
 * returns 1 if it is an event type it understands, 0 if not
 */
int XRRUpdateConfiguration(XEvent *event);

_XFUNCPROTOEND

#endif /* _XRANDR_H_ */
