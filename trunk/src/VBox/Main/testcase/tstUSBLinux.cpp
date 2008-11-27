/* $Id$ */

/** @file
 *
 * Test executable for quickly excercising/debugging the hal-based Linux USB
 * bits.
 */

/*
 * Copyright (C) 2008 Sun Microsystems, Inc.
 *
 * Sun Microsystems, Inc. confidential
 * All rights reserved
 */

#include "tstUSBLinux.h"

#include <VBox/err.h>

#include <iprt/initterm.h>
#include <iprt/stream.h>

#include <iprt/cdefs.h>
#include <iprt/types.h>

int main()
{
    RTR3Init();
    USBProxyServiceLinux service;
    service.initSysfs();
    if (RT_FAILURE(service.getLastError()))
    {
        RTPrintf("Failed to initialise USBProxyServiceLinux, error %Rrc\n",
                 service.getLastError());
        return 1;
    }
    PUSBDEVICE pChain = service.getDevicesFromSysfs();
    if (pChain == NULL)
        RTPrintf("Failed to get any devices from sysfs\n.");
    else
    {
        PUSBDEVICE pNext = pChain;
        while (pNext != NULL)
        {
            RTPrintf("Device: %s (product string)\n", pNext->pszProduct);
            RTPrintf("  Manufacturer: %s\n", pNext->pszManufacturer);
            RTPrintf("  Serial number: %s\n", pNext->pszSerialNumber);
            RTPrintf("  Address: %s\n", pNext->pszAddress);
            RTPrintf("  Vendor ID: %d\n", pNext->idVendor);
            RTPrintf("  Product ID: %d\n", pNext->idProduct);
            RTPrintf("  Revision: %d.%d\n", pNext->bcdDevice >> 8, pNext->bcdDevice & 255);
            RTPrintf("  USB Version: %d.%d\n", pNext->bcdUSB >> 8, pNext->bcdUSB & 255);
            RTPrintf("  Device class: %d\n", pNext->bDeviceClass);
            RTPrintf("  Device subclass: %d\n", pNext->bDeviceSubClass);
            RTPrintf("  Device protocol: %d\n", pNext->bDeviceProtocol);
            RTPrintf("  Number of configurations: %d\n", pNext->bNumConfigurations);
            RTPrintf("  Device state: %s\n",
                     pNext->enmState == USBDEVICESTATE_UNUSED ? "unused"
                         : pNext->enmState == USBDEVICESTATE_USED_BY_HOST_CAPTURABLE ? "used by host"
                         : "unknown"
                    );
            RTPrintf("  Device speed: %s\n",
                     pNext->enmSpeed == USBDEVICESPEED_LOW ? "low"
                         : pNext->enmSpeed == USBDEVICESPEED_FULL ? "full"
                         : pNext->enmSpeed == USBDEVICESPEED_HIGH ? "high"
                         : "unknown"
                    );
            RTPrintf("  Serial hash: 0x%llx\n", pNext->u64SerialHash);
            RTPrintf("  Bus number: %d\n", pNext->bBus);
            RTPrintf("  Port number: %d\n", pNext->bPort);
            RTPrintf("  Device number: %d\n", pNext->bDevNum);
            RTPrintf("\n");
            pNext = pNext->pNext;
        }
    }
    return 0;
}

