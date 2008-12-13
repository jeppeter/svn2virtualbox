/** @file
 *
 * VirtualBox interface to host's power notification service
 */

/*
 * Copyright (C) 2006-2008 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ____H_HOSTPOWER
#define ____H_HOSTPOWER

#include "VirtualBoxBase.h"
#include "MachineImpl.h"

#include <vector>

#ifdef RT_OS_DARWIN
# include <IOKit/pwr_mgt/IOPMLib.h>
# include <Carbon/Carbon.h>
#endif /* RT_OS_DARWIN */

class VirtualBox;

typedef enum
{
    HostPowerEvent_Suspend,
    HostPowerEvent_Resume,
    HostPowerEvent_BatteryLow
} HostPowerEvent;

class HostPowerService
{
public:

    HostPowerService (VirtualBox *aVirtualBox);
    virtual ~HostPowerService();

    void    notify (HostPowerEvent aEvent);

protected:

    ComObjPtr <VirtualBox, ComWeakRef> mVirtualBox;

    std::vector <ComPtr <IConsole> > mConsoles;
};

# ifdef RT_OS_WINDOWS
/**
 * The Windows hosted Power Service.
 */
class HostPowerServiceWin : public HostPowerService
{
public:

    HostPowerServiceWin(VirtualBox *aVirtualBox);
    virtual ~HostPowerServiceWin();

private:

    static DECLCALLBACK(int) NotificationThread (RTTHREAD ThreadSelf, void *pInstance);
    static LRESULT CALLBACK  WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND        mHwnd;
    RTTHREAD    mThread;
};
# elif defined(RT_OS_DARWIN) /* RT_OS_WINDOWS */
/**
 * The Darwin hosted Power Service.
 */
class HostPowerServiceDarwin : public HostPowerService
{
public:

    HostPowerServiceDarwin (VirtualBox *aVirtualBox);
    virtual ~HostPowerServiceDarwin();

private:

    static DECLCALLBACK(int) powerChangeNotificationThread (RTTHREAD ThreadSelf, void *pInstance);
    static void powerChangeNotificationHandler (void *pData, io_service_t service, natural_t messageType, void *pMessageArgument);
    static OSErr lowPowerEventHandler (const AppleEvent * theAppleEvent, AppleEvent * replyAppleEvent, long refCon);

    /* Private member vars */
    RTTHREAD mThread; /* Our message thread. */

    io_connect_t mRootPort; /* A reference to the Root Power Domain IOService */
    IONotificationPortRef mNotifyPort; /* Notification port allocated by IORegisterForSystemPower */
    io_object_t mNotifierObject; /* Notifier object, used to deregister later */
    CFRunLoopRef mRunLoop; /* A reference to the local thread run loop */
};
# endif /* RT_OS_DARWIN */

#endif /* !____H_HOSTPOWER */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
