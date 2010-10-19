/** @file
 * PCI - The PCI Controller And Devices. (DEV)
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

#ifndef ___VBox_pci_h
#define ___VBox_pci_h

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <iprt/assert.h>

/** @defgroup grp_pci       PCI - The PCI Controller.
 * @{
 */

/** Pointer to a PCI device. */
typedef struct PCIDevice *PPCIDEVICE;


/**
 * PCI configuration word 4 (command) and word 6 (status).
 */
typedef enum PCICONFIGCOMMAND
{
    /** Supports/uses memory accesses. */
    PCI_COMMAND_IOACCESS  = 0x0001,
    PCI_COMMAND_MEMACCESS = 0x0002,
    PCI_COMMAND_BUSMASTER = 0x0004
} PCICONFIGCOMMAND;


/**
 * PCI Address space specification.
 * This is used when registering a I/O region.
 */
/** Note: There are all sorts of dirty dependencies on the values in the
 *  pci device. Be careful when changing this.
 *  @todo we should introduce 32 & 64 bits physical address types
 */
typedef enum PCIADDRESSSPACE
{
    /** Memory. */
    PCI_ADDRESS_SPACE_MEM = 0x00,
    /** I/O space. */
    PCI_ADDRESS_SPACE_IO = 0x01,
    /** Prefetch memory. */
    PCI_ADDRESS_SPACE_MEM_PREFETCH = 0x08
} PCIADDRESSSPACE;


/**
 * Callback function for mapping an PCI I/O region.
 *
 * @return VBox status code.
 * @param   pPciDev         Pointer to PCI device. Use pPciDev->pDevIns to get the device instance.
 * @param   iRegion         The region number.
 * @param   GCPhysAddress   Physical address of the region. If enmType is PCI_ADDRESS_SPACE_IO, this
 *                          is an I/O port, otherwise it's a physical address.
 *
 *                          NIL_RTGCPHYS indicates that a MMIO2 mapping is about to be unmapped and
 *                          that the device deregister access handlers for it and update its internal
 *                          state to reflect this.
 *
 * @param   enmType         One of the PCI_ADDRESS_SPACE_* values.
 *
 * @remarks The address is *NOT* relative to pci_mem_base.
 */
typedef DECLCALLBACK(int) FNPCIIOREGIONMAP(PPCIDEVICE pPciDev, /*unsigned*/ int iRegion, RTGCPHYS GCPhysAddress, uint32_t cb, PCIADDRESSSPACE enmType);
/** Pointer to a FNPCIIOREGIONMAP() function. */
typedef FNPCIIOREGIONMAP *PFNPCIIOREGIONMAP;


/** @name PCI Configuration Space Registers
 * @{ */
/* Commented out values common for different header types */
/* Common part of the header */
#define VBOX_PCI_VENDOR_ID              0x00    /**< 16-bit  RO */
#define VBOX_PCI_DEVICE_ID              0x02    /**< 16-bit  RO */
#define VBOX_PCI_COMMAND                0x04    /**< 16-bit  RW, some bits RO */
#define VBOX_PCI_STATUS                 0x06    /**< 16-bit  RW, some bits RO */
#define VBOX_PCI_REVISION_ID            0x08    /**<  8-bit  RO  - - device revision */
#define VBOX_PCI_CLASS_PROG             0x09    /**<  8-bit  RO  - - register-level programming class code (device specific). */
#define VBOX_PCI_CLASS_SUB              0x0a    /**<  8-bit  RO  - - sub-class code. */
#define VBOX_PCI_CLASS_DEVICE           VBOX_PCI_CLASS_SUB
#define VBOX_PCI_CLASS_BASE             0x0b    /**<  8-bit  RO  - - base class code. */
#define VBOX_PCI_CACHE_LINE_SIZE        0x0c    /**<  8-bit  RW  - - system cache line size */
#define VBOX_PCI_LATENCY_TIMER          0x0d    /**<  8-bit  RW  - - master latency timer, hardwired to 0 for PCIe */
#define VBOX_PCI_HEADER_TYPE            0x0e    /**<  8-bit  RO  - - header type (0 - device, 1 - bridge, 2  - CardBus bridge)  */
#define VBOX_PCI_BIST                   0x0f    /**<  8-bit  RW  - - built-in self test control */
#define VBOX_PCI_CAPABILITY_LIST        0x34    /**<  8-bit  RO? - - linked list of new capabilities implemented by the device, 2 bottom bits reserved */
#define VBOX_PCI_INTERRUPT_LINE         0x3c    /**<  8-bit  RW  - - interrupt line. */
#define VBOX_PCI_INTERRUPT_PIN          0x3d    /**<  8-bit  RO  - - interrupt pin.  */

/* Type 0 header, device */
#define VBOX_PCI_BASE_ADDRESS_0         0x10    /**< 32-bit  RW */
#define VBOX_PCI_BASE_ADDRESS_1         0x14    /**< 32-bit  RW */
#define VBOX_PCI_BASE_ADDRESS_2         0x18    /**< 32-bit  RW */
#define VBOX_PCI_BASE_ADDRESS_3         0x1c    /**< 32-bit  RW */
#define VBOX_PCI_BASE_ADDRESS_4         0x20    /**< 32-bit  RW */
#define VBOX_PCI_BASE_ADDRESS_5         0x24    /**< 32-bit  RW */
#define VBOX_PCI_CARDBUS_CIS            0x28    /**< 32-bit  ?? */
#define VBOX_PCI_SUBSYSTEM_VENDOR_ID    0x2c    /**< 16-bit  RO */
#define VBOX_PCI_SUBSYSTEM_ID           0x2e    /**< 16-bit  RO */
#define VBOX_PCI_ROM_ADDRESS            0x30    /**< 32-bit  ?? */
/* #define VBOX_PCI_CAPABILITY_LIST        0x34 */  /**<  8-bit? ?? */
#define VBOX_PCI_RESERVED_35            0x35    /**<  8-bit  ?? - - reserved */
#define VBOX_PCI_RESERVED_36            0x36    /**<  8-bit  ?? - - reserved */
#define VBOX_PCI_RESERVED_37            0x37    /**<  8-bit  ?? - - reserved */
#define VBOX_PCI_RESERVED_38            0x38    /**<  32-bit ?? - - reserved */
/* #define VBOX_PCI_INTERRUPT_LINE         0x3c */   /**<  8-bit  RW  - - interrupt line. */
/* #define VBOX_PCI_INTERRUPT_PIN          0x3d */   /**<  8-bit  RO  - - interrupt pin.  */
#define VBOX_PCI_MIN_GNT                0x3e    /**<  8-bit  RO - - burst period length (in 1/4 microsecond units)  */
#define VBOX_PCI_MAX_LAT                0x3f    /**<  8-bit  RO - - how often the device needs access to the PCI bus (in 1/4 microsecond units) */

/* Type 1 header, PCI-to-PCI bridge */
/* #define VBOX_PCI_BASE_ADDRESS_0         0x10 */    /**< 32-bit RW */
/* #define VBOX_PCI_BASE_ADDRESS_1         0x14 */    /**< 32-bit RW */
#define VBOX_PCI_PRIMARY_BUS            0x18    /**<  8-bit  ?? - - primary bus number. */
#define VBOX_PCI_SECONDARY_BUS          0x19    /**<  8-bit  ?? - - secondary bus number. */
#define VBOX_PCI_SUBORDINATE_BUS        0x1a    /**<  8-bit  ?? - - highest subordinate bus number. (behind the bridge) */
#define VBOX_PCI_SEC_LATENCY_TIMER      0x1b    /**<  8-bit  ?? - - secondary latency timer. */
#define VBOX_PCI_IO_BASE                0x1c    /**<  8-bit  ?? - - I/O range base. */
#define VBOX_PCI_IO_LIMIT               0x1d    /**<  8-bit  ?? - - I/O range limit. */
#define VBOX_PCI_SEC_STATUS             0x1e    /**< 16-bit  ?? - - secondary status register. */
#define VBOX_PCI_MEMORY_BASE            0x20    /**< 16-bit  ?? - - memory range base. */
#define VBOX_PCI_MEMORY_LIMIT           0x22    /**< 16-bit  ?? - - memory range limit. */
#define VBOX_PCI_PREF_MEMORY_BASE       0x24    /**< 16-bit  ?? - - prefetchable memory range base. */
#define VBOX_PCI_PREF_MEMORY_LIMIT      0x26    /**< 16-bit  ?? - - prefetchable memory range limit. */
#define VBOX_PCI_PREF_BASE_UPPER32      0x28    /**< 32-bit  ?? - - prefetchable memory range high base.*/
#define VBOX_PCI_PREF_LIMIT_UPPER32     0x2c    /**< 32-bit  ?? - - prefetchable memory range high limit. */
#define VBOX_PCI_IO_BASE_UPPER16        0x30    /**< 16-bit  ?? - - memory range high base. */
#define VBOX_PCI_IO_LIMIT_UPPER16       0x32    /**< 16-bit  ?? - - memory range high limit. */
/* #define VBOX_PCI_CAPABILITY_LIST        0x34 */   /**<  8-bit? ?? */
/* #define VBOX_PCI_RESERVED_35            0x35 */   /**<  8-bit ?? - - reserved */
/* #define VBOX_PCI_RESERVED_36            0x36 */   /**<  8-bit ?? - - reserved */
/* #define VBOX_PCI_RESERVED_37            0x37 */   /**<  8-bit ?? - - reserved */
#define VBOX_PCI_ROM_ADDRESS_BR         0x38    /**< 32-bit  ?? - - expansion ROM base address  */
#define VBOX_PCI_BRIDGE_CONTROL         0x3e    /**< 16-bit?  ?? - - bridge control  */

/* Type 2 header, PCI-to-CardBus bridge */
#define VBOX_PCI_CARDBUS_BASE_ADDRESS   0x10    /**< 32-bit  RW  - - CardBus Socket/ExCa base address */
#define VBOX_PCI_CARDBUS_CAPLIST        0x14    /**<  8-bit  RO? - - offset of capabilities list */
#define VBOX_PCI_CARDBUS_RESERVED_15    0x15    /**<  8-bit  ??  - - reserved */
#define VBOX_PCI_CARDBUS_SEC_STATUS     0x16    /**< 16-bit  ??  - - secondary status  */
#define VBOX_PCI_CARDBUS_PCIBUS_NUMBER  0x18    /**<  8-bit  ??  - - PCI bus number */
#define VBOX_PCI_CARDBUS_CARDBUS_NUMBER 0x19    /**<  8-bit  ??  - - CardBus bus number */
/* #define VBOX_PCI_SUBORDINATE_BUS        0x1a */    /**<  8-bit  ?? - - highest subordinate bus number. (behind the bridge) */
/* #define VBOX_PCI_SEC_LATENCY_TIMER      0x1b */    /**<  8-bit  ?? - - secondary latency timer. */
#define VBOX_PCI_CARDBUS_MEMORY_BASE0   0x1c     /**< 32-bit  RW  - - memory base address 0 */
#define VBOX_PCI_CARDBUS_MEMORY_LIMIT0  0x20     /**< 32-bit  RW  - - memory limit 0 */
#define VBOX_PCI_CARDBUS_MEMORY_BASE1   0x24     /**< 32-bit  RW  - - memory base address 1 */
#define VBOX_PCI_CARDBUS_MEMORY_LIMIT1  0x28     /**< 32-bit  RW  - - memory limit 1 */
#define VBOX_PCI_CARDBUS_IO_BASE0       0x2c     /**< 32-bit  RW  - - IO base address 0 */
#define VBOX_PCI_CARDBUS_IO_LIMIT0      0x30     /**< 32-bit  RW  - - IO limit 0 */
#define VBOX_PCI_CARDBUS_IO_BASE1       0x34     /**< 32-bit  RW  - - IO base address 1 */
#define VBOX_PCI_CARDBUS_IO_LIMIT1      0x38     /**< 32-bit  RW  - - IO limit 1 */
/* #define VBOX_PCI_INTERRUPT_LINE         0x3c */   /**<  8-bit  RW  - - interrupt line. */
/* #define VBOX_PCI_INTERRUPT_PIN          0x3d */   /**<  8-bit  RO  - - interrupt pin.  */
/* #define VBOX_PCI_BRIDGE_CONTROL         0x3e */   /**< 16-bit?  ?? - - bridge control  */
/** @} */


/* Possible values in status bitmask */
#define  VBOX_PCI_STATUS_CAP_LIST    0x10    /* Support Capability List */
#define  VBOX_PCI_STATUS_66MHZ       0x20    /* Support 66 Mhz PCI 2.1 bus */
#define  VBOX_PCI_STATUS_UDF         0x40    /* Support User Definable Features [obsolete] */
#define  VBOX_PCI_STATUS_FAST_BACK   0x80    /* Accept fast-back to back */
#define  VBOX_PCI_STATUS_PARITY      0x100   /* Detected parity error */
#define  VBOX_PCI_STATUS_DEVSEL_MASK 0x600   /* DEVSEL timing */
#define  VBOX_PCI_STATUS_DEVSEL_FAST         0x000
#define  VBOX_PCI_STATUS_DEVSEL_MEDIUM       0x200
#define  VBOX_PCI_STATUS_DEVSEL_SLOW         0x400
#define  VBOX_PCI_STATUS_SIG_TARGET_ABORT    0x800 /* Set on target abort */
#define  VBOX_PCI_STATUS_REC_TARGET_ABORT    0x1000 /* Master ack of " */
#define  VBOX_PCI_STATUS_REC_MASTER_ABORT    0x2000 /* Set on master abort */
#define  VBOX_PCI_STATUS_SIG_SYSTEM_ERROR    0x4000 /* Set when we drive SERR */
#define  VBOX_PCI_STATUS_DETECTED_PARITY     0x8000 /* Set on parity error */


/* Command bitmask */
#define  VBOX_PCI_COMMAND_IO           0x1     /* Enable response in I/O space */
#define  VBOX_PCI_COMMAND_MEMORY       0x2     /* Enable response in Memory space */
#define  VBOX_PCI_COMMAND_MASTER       0x4     /* Enable bus mastering */
#define  VBOX_PCI_COMMAND_SPECIAL      0x8     /* Enable response to special cycles */
#define  VBOX_PCI_COMMAND_INVALIDATE   0x10    /* Use memory write and invalidate */
#define  VBOX_PCI_COMMAND_VGA_PALETTE  0x20    /* Enable palette snooping */
#define  VBOX_PCI_COMMAND_PARITY       0x40    /* Enable parity checking */
#define  VBOX_PCI_COMMAND_WAIT         0x80    /* Enable address/data stepping */
#define  VBOX_PCI_COMMAND_SERR         0x100   /* Enable SERR */
#define  VBOX_PCI_COMMAND_FAST_BACK    0x200   /* Enable back-to-back writes */
#define  VBOX_PCI_COMMAND_INTX_DISABLE 0x400   /* INTx Emulation Disable */


/* Capability list values (capability offset 0) */
/* Next  value pointer in offset 1, or 0 if none */
#define  VBOX_PCI_CAP_ID_PM          0x01    /* Power Management */
#define  VBOX_PCI_CAP_ID_AGP         0x02    /* Accelerated Graphics Port */
#define  VBOX_PCI_CAP_ID_VPD         0x03    /* Vital Product Data */
#define  VBOX_PCI_CAP_ID_SLOTID      0x04    /* Slot Identification */
#define  VBOX_PCI_CAP_ID_MSI         0x05    /* Message Signalled Interrupts */
#define  VBOX_PCI_CAP_ID_CHSWP       0x06    /* CompactPCI HotSwap */
#define  VBOX_PCI_CAP_ID_PCIX        0x07    /* PCI-X */
#define  VBOX_PCI_CAP_ID_HT          0x08    /* HyperTransport */
#define  VBOX_PCI_CAP_ID_VNDR        0x09    /* Vendor specific */
#define  VBOX_PCI_CAP_ID_DBG         0x0A    /* Debug port */
#define  VBOX_PCI_CAP_ID_CCRC        0x0B    /* CompactPCI Central Resource Control */
#define  VBOX_PCI_CAP_ID_SHPC        0x0C    /* PCI Standard Hot-Plug Controller */
#define  VBOX_PCI_CAP_ID_SSVID       0x0D    /* Bridge subsystem vendor/device ID */
#define  VBOX_PCI_CAP_ID_AGP3        0x0E    /* AGP Target PCI-PCI bridge */
#define  VBOX_PCI_CAP_ID_SECURE      0x0F    /* Secure device (?) */
#define  VBOX_PCI_CAP_ID_EXP         0x10    /* PCI Express */
#define  VBOX_PCI_CAP_ID_MSIX        0x11    /* MSI-X */
#define  VBOX_PCI_CAP_ID_SATA        0x12    /* Serial-ATA HBA */
#define  VBOX_PCI_CAP_ID_AF          0x13    /* PCI Advanced Features */

/* Extended Capabilities (PCI-X 2.0 and Express), start at 0x100, next - bits [20..32] */
#define  VBOX_PCI_EXT_CAP_ID_ERR     0x01    /* Advanced Error Reporting */
#define  VBOX_PCI_EXT_CAP_ID_VC      0x02    /* Virtual Channel */
#define  VBOX_PCI_EXT_CAP_ID_DSN     0x03    /* Device Serial Number */
#define  VBOX_PCI_EXT_CAP_ID_PWR     0x04    /* Power Budgeting */
#define  VBOX_PCI_EXT_CAP_ID_RCLINK  0x05    /* Root Complex Link Declaration */
#define  VBOX_PCI_EXT_CAP_ID_RCILINK 0x06    /* Root Complex Internal Link Declaration */
#define  VBOX_PCI_EXT_CAP_ID_RCECOLL 0x07    /* Root Complex Event Collector */
#define  VBOX_PCI_EXT_CAP_ID_MFVC    0x08    /* Multi-Function Virtual Channel */
#define  VBOX_PCI_EXT_CAP_ID_RBCB    0x0a    /* Root Bridge Control Block */
#define  VBOX_PCI_EXT_CAP_ID_VNDR    0x0b    /* Vendor specific */
#define  VBOX_PCI_EXT_CAP_ID_ACS     0x0d    /* Access Controls */
#define  VBOX_PCI_EXT_CAP_ID_ARI     0x0e
#define  VBOX_PCI_EXT_CAP_ID_ATS     0x0f
#define  VBOX_PCI_EXT_CAP_ID_SRIOV   0x10


/* MSI flags, aka Message Control (2 bytes, capability offset 2) */
#define  VBOX_PCI_MSI_FLAGS_ENABLE   0x0001  /* MSI feature enabled */
#define  VBOX_PCI_MSI_FLAGS_64BIT    0x0080  /* 64-bit addresses allowed */
#define  VBOX_PCI_MSI_FLAGS_MASKBIT  0x0100  /* Per-vector masking support */
/* Encoding for 3-bit patterns for message queue (per chapter 6.8.1 of PCI spec),
   someone very similar to log_2().
   000 1
   001 2
   010 4
   011 8
   100 16
   101 32
   110 Reserved
   111 Reserved */
#define  VBOX_PCI_MSI_FLAGS_QSIZE    0x0070  /* Message queue size configured (i.e. vectors per device allocated) */
#define  VBOX_PCI_MSI_FLAGS_QMASK    0x000e  /* Maximum queue size available (i.e. vectors per device possible) */

/* MSI-X flags (2 bytes, capability offset 2) */
#define  VBOX_PCI_MSIX_FLAGS_ENABLE   0x8000  /* MSI-X enable */
#define  VBOX_PCI_MSIX_FLAGS_FUNCMASK 0x4000  /* Function mask */

/* Power management flags (2 bytes, capability offset 2) */
#define  VBOX_PCI_PM_CAP_VER_MASK    0x0007  /* Version mask */
#define  VBOX_PCI_PM_CAP_PME_CLOCK   0x0008  /* PME clock required */
#define  VBOX_PCI_PM_CAP_RESERVED    0x0010  /* Reserved field */
#define  VBOX_PCI_PM_CAP_DSI         0x0020  /* Device specific initialization */
#define  VBOX_PCI_PM_CAP_AUX_POWER   0x01C0  /* Auxilliary power support mask */
#define  VBOX_PCI_PM_CAP_D1          0x0200  /* D1 power state support */
#define  VBOX_PCI_PM_CAP_D2          0x0400  /* D2 power state support */
#define  VBOX_PCI_PM_CAP_PME         0x0800  /* PME pin supported */
#define  VBOX_PCI_PM_CAP_PME_MASK    0xF800  /* PME Mask of all supported states */
#define  VBOX_PCI_PM_CAP_PME_D0      0x0800  /* PME# from D0 */
#define  VBOX_PCI_PM_CAP_PME_D1      0x1000  /* PME# from D1 */
#define  VBOX_PCI_PM_CAP_PME_D2      0x2000  /* PME# from D2 */
#define  VBOX_PCI_PM_CAP_PME_D3      0x4000  /* PME# from D3 (hot) */
#define  VBOX_PCI_PM_CAP_PME_D3cold  0x8000  /* PME# from D3 (cold) */

/* Power management control flags (2 bytes, capability offset 4) */
#define  VBOX_PCI_PM_CTRL_STATE_MASK         0x0003  /* Current power state (D0 to D3) */
#define  VBOX_PCI_PM_CTRL_NO_SOFT_RESET      0x0008  /* No reset for D3hot->D0 */
#define  VBOX_PCI_PM_CTRL_PME_ENABLE         0x0100  /* PME pin enable */
#define  VBOX_PCI_PM_CTRL_DATA_SEL_MASK      0x1e00  /* Data select (??) */
#define  VBOX_PCI_PM_CTRL_DATA_SCALE_MASK    0x6000  /* Data scale (??) */
#define  VBOX_PCI_PM_CTRL_PME_STATUS         0x8000  /* PME pin status */

/* PCI-X config flags (2 bytes, capability offset 2) */
#define  VBOX_PCI_X_CMD_DPERR_E      0x0001  /* Data Parity Error Recovery Enable */
#define  VBOX_PCI_X_CMD_ERO          0x0002  /* Enable Relaxed Ordering */
#define  VBOX_PCI_X_CMD_MAX_OUTSTANDING_SPLIT_TRANS          0x0070
#define  VBOX_PCI_X_CMD_READ_512     0x0000  /* 512 byte maximum read byte count */
#define  VBOX_PCI_X_CMD_READ_1K      0x0004  /* 1Kbyte maximum read byte count */
#define  VBOX_PCI_X_CMD_READ_2K      0x0008  /* 2Kbyte maximum read byte count */
#define  VBOX_PCI_X_CMD_READ_4K      0x000c  /* 4Kbyte maximum read byte count */
#define  VBOX_PCI_X_CMD_MAX_READ     0x000c  /* Max Memory Read Byte Count */

/* PCI-X config flags (4 bytes, capability offset 4) */
#define  VBOX_PCI_X_STATUS_DEVFN     0x000000ff      /* A copy of devfn */
#define  VBOX_PCI_X_STATUS_BUS       0x0000ff00      /* A copy of bus nr */
#define  VBOX_PCI_X_STATUS_64BIT     0x00010000      /* 64-bit device */
#define  VBOX_PCI_X_STATUS_133MHZ    0x00020000      /* 133 MHz capable */
#define  VBOX_PCI_X_STATUS_SPL_DISC  0x00040000      /* Split Completion Discarded */
#define  VBOX_PCI_X_STATUS_UNX_SPL   0x00080000      /* Unexpected Split Completion */
#define  VBOX_PCI_X_STATUS_COMPLEX   0x00100000      /* Device Complexity, 0 = simple device, 1 = bridge device */
#define  VBOX_PCI_X_STATUS_MAX_READ  0x00600000      /* Designed Max Memory Read Count, 0 = 512 bytes, 1 = 1024, 2 = 2048, 3 = 4096 */
#define  VBOX_PCI_X_STATUS_MAX_SPLIT 0x03800000      /* Designed Max Outstanding Split Transactions */
#define  VBOX_PCI_X_STATUS_MAX_CUM   0x1c000000      /* Designed Max Cumulative Read Size */
#define  VBOX_PCI_X_STATUS_SPL_ERR   0x20000000      /* Rcvd Split Completion Error Msg */
#define  VBOX_PCI_X_STATUS_266MHZ    0x40000000      /* 266 MHz capable */
#define  VBOX_PCI_X_STATUS_533MHZ    0x80000000      /* 533 MHz capable */

/* PCI Express config flags (2 bytes, capability offset 2) */
#define  VBOX_PCI_EXP_FLAGS_VERS        0x000f  /* Capability version */
#define  VBOX_PCI_EXP_FLAGS_TYPE        0x00f0  /* Device/Port type */
#define  VBOX_PCI_EXP_TYPE_ENDPOINT     0x0     /* Express Endpoint */
#define  VBOX_PCI_EXP_TYPE_LEG_END      0x1     /* Legacy Endpoint */
#define  VBOX_PCI_EXP_TYPE_ROOT_PORT    0x4     /* Root Port */
#define  VBOX_PCI_EXP_TYPE_UPSTREAM     0x5     /* Upstream Port */
#define  VBOX_PCI_EXP_TYPE_DOWNSTREAM   0x6     /* Downstream Port */
#define  VBOX_PCI_EXP_TYPE_PCI_BRIDGE   0x7     /* PCI/PCI-X Bridge */
#define  VBOX_PCI_EXP_TYPE_PCIE_BRIDGE  0x8     /* PCI/PCI-X to PCIE Bridge */
#define  VBOX_PCI_EXP_TYPE_ROOT_INT_EP  0x9     /* Root Complex Integrated Endpoint */
#define  VBOX_PCI_EXP_TYPE_ROOT_EC      0xa     /* Root Complex Event Collector */
#define  VBOX_PCI_EXP_FLAGS_SLOT        0x0100  /* Slot implemented */
#define  VBOX_PCI_EXP_FLAGS_IRQ         0x3e00  /* Interrupt message number */

/* PCI Express device capabilities (4 bytes, capability offset 4) */
#define  VBOX_PCI_EXP_DEVCAP_PAYLOAD 0x07        /* Max_Payload_Size */
#define  VBOX_PCI_EXP_DEVCAP_PHANTOM 0x18        /* Phantom functions */
#define  VBOX_PCI_EXP_DEVCAP_EXT_TAG 0x20        /* Extended tags */
#define  VBOX_PCI_EXP_DEVCAP_L0S     0x1c0       /* L0s Acceptable Latency */
#define  VBOX_PCI_EXP_DEVCAP_L1      0xe00       /* L1 Acceptable Latency */
#define  VBOX_PCI_EXP_DEVCAP_ATN_BUT 0x1000      /* Attention Button Present */
#define  VBOX_PCI_EXP_DEVCAP_ATN_IND 0x2000      /* Attention Indicator Present */
#define  VBOX_PCI_EXP_DEVCAP_PWR_IND 0x4000      /* Power Indicator Present */
#define  VBOX_PCI_EXP_DEVCAP_RBE     0x8000      /* Role-Based Error Reporting */
#define  VBOX_PCI_EXP_DEVCAP_PWR_VAL 0x3fc0000   /* Slot Power Limit Value */
#define  VBOX_PCI_EXP_DEVCAP_PWR_SCL 0xc000000   /* Slot Power Limit Scale */
#define  VBOX_PCI_EXP_DEVCAP_FLRESET 0x10000000  /* Function-Level Reset */

/* PCI Express device control (2 bytes, capability offset 8) */
#define  VBOX_PCI_EXP_DEVCTL_CERE    0x0001      /* Correctable Error Reporting En. */
#define  VBOX_PCI_EXP_DEVCTL_NFERE   0x0002      /* Non-Fatal Error Reporting Enable */
#define  VBOX_PCI_EXP_DEVCTL_FERE    0x0004      /* Fatal Error Reporting Enable */
#define  VBOX_PCI_EXP_DEVCTL_URRE    0x0008      /* Unsupported Request Reporting En. */
#define  VBOX_PCI_EXP_DEVCTL_RELAXED 0x0010      /* Enable Relaxed Ordering */
#define  VBOX_PCI_EXP_DEVCTL_PAYLOAD 0x00e0      /* Max_Payload_Size */
#define  VBOX_PCI_EXP_DEVCTL_EXT_TAG 0x0100      /* Extended Tag Field Enable */
#define  VBOX_PCI_EXP_DEVCTL_PHANTOM 0x0200      /* Phantom Functions Enable */
#define  VBOX_PCI_EXP_DEVCTL_AUX_PME 0x0400      /* Auxiliary Power PM Enable */
#define  VBOX_PCI_EXP_DEVCTL_NOSNOOP 0x0800      /* Enable No Snoop */
#define  VBOX_PCI_EXP_DEVCTL_READRQ  0x7000      /* Max_Read_Request_Size */
#define  VBOX_PCI_EXP_DEVCTL_BCRE    0x8000      /* Bridge Configuration Retry Enable */
#define  VBOX_PCI_EXP_DEVCTL_FLRESET 0x8000      /* Function-Level Reset [bit shared with BCRE] */

/* PCI Express device status (2 bytes, capability offset 10) */
#define  VBOX_PCI_EXP_DEVSTA_CED     0x01         /* Correctable Error Detected */
#define  VBOX_PCI_EXP_DEVSTA_NFED    0x02         /* Non-Fatal Error Detected */
#define  VBOX_PCI_EXP_DEVSTA_FED     0x04         /* Fatal Error Detected */
#define  VBOX_PCI_EXP_DEVSTA_URD     0x08         /* Unsupported Request Detected */
#define  VBOX_PCI_EXP_DEVSTA_AUXPD   0x10         /* AUX Power Detected */
#define  VBOX_PCI_EXP_DEVSTA_TRPND   0x20         /* Transactions Pending */

/* PCI Express link capabilities (4 bytes, capability offset 12) */
#define  VBOX_PCI_EXP_LNKCAP_SPEED   0x0000f       /* Maximum Link Speed */
#define  VBOX_PCI_EXP_LNKCAP_WIDTH   0x003f0       /* Maximum Link Width */
#define  VBOX_PCI_EXP_LNKCAP_ASPM    0x00c00       /* Active State Power Management */
#define  VBOX_PCI_EXP_LNKCAP_L0S     0x07000       /* L0s Acceptable Latency */
#define  VBOX_PCI_EXP_LNKCAP_L1      0x38000       /* L1 Acceptable Latency */
#define  VBOX_PCI_EXP_LNKCAP_CLOCKPM 0x40000       /* Clock Power Management */
#define  VBOX_PCI_EXP_LNKCAP_SURPRISE 0x80000      /* Surprise Down Error Reporting */
#define  VBOX_PCI_EXP_LNKCAP_DLLA    0x100000      /* Data Link Layer Active Reporting */
#define  VBOX_PCI_EXP_LNKCAP_LBNC    0x200000      /* Link Bandwidth Notification Capability */
#define  VBOX_PCI_EXP_LNKCAP_PORT    0xff000000    /* Port Number */

/* PCI Express link control (2 bytes, capability offset 16) */
#define  VBOX_PCI_EXP_LNKCTL_ASPM    0x0003        /* ASPM Control */
#define  VBOX_PCI_EXP_LNKCTL_RCB     0x0008        /* Read Completion Boundary */
#define  VBOX_PCI_EXP_LNKCTL_DISABLE 0x0010        /* Link Disable */
#define  VBOX_PCI_EXP_LNKCTL_RETRAIN 0x0020        /* Retrain Link */
#define  VBOX_PCI_EXP_LNKCTL_CLOCK   0x0040        /* Common Clock Configuration */
#define  VBOX_PCI_EXP_LNKCTL_XSYNCH  0x0080        /* Extended Synch */
#define  VBOX_PCI_EXP_LNKCTL_CLOCKPM 0x0100        /* Clock Power Management */
#define  VBOX_PCI_EXP_LNKCTL_HWAUTWD 0x0200        /* Hardware Autonomous Width Disable */
#define  VBOX_PCI_EXP_LNKCTL_BWMIE   0x0400        /* Bandwidth Mgmt Interrupt Enable */
#define  VBOX_PCI_EXP_LNKCTL_AUTBWIE 0x0800        /* Autonomous Bandwidth Mgmt Interrupt Enable */

/* PCI Express link status (2 bytes, capability offset 18) */
#define  VBOX_PCI_EXP_LNKSTA_SPEED   0x000f        /* Negotiated Link Speed */
#define  VBOX_PCI_EXP_LNKSTA_WIDTH   0x03f0        /* Negotiated Link Width */
#define  VBOX_PCI_EXP_LNKSTA_TR_ERR  0x0400        /* Training Error (obsolete) */
#define  VBOX_PCI_EXP_LNKSTA_TRAIN   0x0800        /* Link Training */
#define  VBOX_PCI_EXP_LNKSTA_SL_CLK  0x1000        /* Slot Clock Configuration */
#define  VBOX_PCI_EXP_LNKSTA_DL_ACT  0x2000        /* Data Link Layer in DL_Active State */
#define  VBOX_PCI_EXP_LNKSTA_BWMGMT  0x4000        /* Bandwidth Mgmt Status */
#define  VBOX_PCI_EXP_LNKSTA_AUTBW   0x8000        /* Autonomous Bandwidth Mgmt Status */

/* PCI Express slot capabilities (4 bytes, capability offset 20) */
#define  VBOX_PCI_EXP_SLTCAP_ATNB    0x0001        /* Attention Button Present */
#define  VBOX_PCI_EXP_SLTCAP_PWRC    0x0002        /* Power Controller Present */
#define  VBOX_PCI_EXP_SLTCAP_MRL     0x0004        /* MRL Sensor Present */
#define  VBOX_PCI_EXP_SLTCAP_ATNI    0x0008        /* Attention Indicator Present */
#define  VBOX_PCI_EXP_SLTCAP_PWRI    0x0010        /* Power Indicator Present */
#define  VBOX_PCI_EXP_SLTCAP_HPS     0x0020        /* Hot-Plug Surprise */
#define  VBOX_PCI_EXP_SLTCAP_HPC     0x0040        /* Hot-Plug Capable */
#define  VBOX_PCI_EXP_SLTCAP_PWR_VAL 0x00007f80    /* Slot Power Limit Value */
#define  VBOX_PCI_EXP_SLTCAP_PWR_SCL 0x00018000    /* Slot Power Limit Scale */
#define  VBOX_PCI_EXP_SLTCAP_INTERLOCK 0x020000    /* Electromechanical Interlock Present */
#define  VBOX_PCI_EXP_SLTCAP_NOCMDCOMP 0x040000    /* No Command Completed Support */
#define  VBOX_PCI_EXP_SLTCAP_PSN     0xfff80000    /* Physical Slot Number */

/* PCI Express slot control (2 bytes, capability offset 24) */
#define  VBOX_PCI_EXP_SLTCTL_ATNB    0x0001        /* Attention Button Pressed Enable */
#define  VBOX_PCI_EXP_SLTCTL_PWRF    0x0002        /* Power Fault Detected Enable */
#define  VBOX_PCI_EXP_SLTCTL_MRLS    0x0004        /* MRL Sensor Changed Enable */
#define  VBOX_PCI_EXP_SLTCTL_PRSD    0x0008        /* Presence Detect Changed Enable */
#define  VBOX_PCI_EXP_SLTCTL_CMDC    0x0010        /* Command Completed Interrupt Enable */
#define  VBOX_PCI_EXP_SLTCTL_HPIE    0x0020        /* Hot-Plug Interrupt Enable */
#define  VBOX_PCI_EXP_SLTCTL_ATNI    0x00c0        /* Attention Indicator Control */
#define  VBOX_PCI_EXP_SLTCTL_PWRI    0x0300        /* Power Indicator Control */
#define  VBOX_PCI_EXP_SLTCTL_PWRC    0x0400        /* Power Controller Control */
#define  VBOX_PCI_EXP_SLTCTL_INTERLOCK 0x0800      /* Electromechanical Interlock Control */
#define  VBOX_PCI_EXP_SLTCTL_LLCHG   0x1000        /* Data Link Layer State Changed Enable */

/* PCI Express slot status (2 bytes, capability offset 26) */
#define  VBOX_PCI_EXP_SLTSTA_ATNB    0x0001        /* Attention Button Pressed */
#define  VBOX_PCI_EXP_SLTSTA_PWRF    0x0002        /* Power Fault Detected */
#define  VBOX_PCI_EXP_SLTSTA_MRLS    0x0004        /* MRL Sensor Changed */
#define  VBOX_PCI_EXP_SLTSTA_PRSD    0x0008        /* Presence Detect Changed */
#define  VBOX_PCI_EXP_SLTSTA_CMDC    0x0010        /* Command Completed */
#define  VBOX_PCI_EXP_SLTSTA_MRL_ST  0x0020        /* MRL Sensor State */
#define  VBOX_PCI_EXP_SLTSTA_PRES    0x0040        /* Presence Detect State */
#define  VBOX_PCI_EXP_SLTSTA_INTERLOCK 0x0080      /* Electromechanical Interlock Status */
#define  VBOX_PCI_EXP_SLTSTA_LLCHG   0x0100        /* Data Link Layer State Changed */

/* PCI Express root control (2 bytes, capability offset 28) */
#define  VBOX_PCI_EXP_RTCTL_SECEE    0x0001        /* System Error on Correctable Error */
#define  VBOX_PCI_EXP_RTCTL_SENFEE   0x0002        /* System Error on Non-Fatal Error */
#define  VBOX_PCI_EXP_RTCTL_SEFEE    0x0004        /* System Error on Fatal Error */
#define  VBOX_PCI_EXP_RTCTL_PMEIE    0x0008        /* PME Interrupt Enable */
#define  VBOX_PCI_EXP_RTCTL_CRSVIS   0x0010        /* Configuration Request Retry Status Visible to SW */

/* PCI Express root capabilities (2 bytes, capability offset 30) */
#define  VBOX_PCI_EXP_RTCAP_CRSVIS   0x0010        /* Configuration Request Retry Status Visible to SW */

/* PCI Express root status (4 bytes, capability offset 32) */
#define  VBOX_PCI_EXP_RTSTA_PME_REQID   0x0000ffff /* PME Requester ID */
#define  VBOX_PCI_EXP_RTSTA_PME_STATUS  0x00010000 /* PME Status */
#define  VBOX_PCI_EXP_RTSTA_PME_PENDING 0x00020000 /* PME is Pending */


/**
 * Callback function for reading from the PCI configuration space.
 *
 * @returns The register value.
 * @param   pPciDev         Pointer to PCI device. Use pPciDev->pDevIns to get the device instance.
 * @param   Address         The configuration space register address. [0..4096]
 * @param   cb              The register size. [1,2,4]
 */
typedef DECLCALLBACK(uint32_t) FNPCICONFIGREAD(PPCIDEVICE pPciDev, uint32_t Address, unsigned cb);
/** Pointer to a FNPCICONFIGREAD() function. */
typedef FNPCICONFIGREAD *PFNPCICONFIGREAD;
/** Pointer to a PFNPCICONFIGREAD. */
typedef PFNPCICONFIGREAD *PPFNPCICONFIGREAD;

/**
 * Callback function for writing to the PCI configuration space.
 *
 * @param   pPciDev         Pointer to PCI device. Use pPciDev->pDevIns to get the device instance.
 * @param   Address         The configuration space register address. [0..4096]
 * @param   u32Value        The value that's being written. The number of bits actually used from
 *                          this value is determined by the cb parameter.
 * @param   cb              The register size. [1,2,4]
 */
typedef DECLCALLBACK(void) FNPCICONFIGWRITE(PPCIDEVICE pPciDev, uint32_t Address, uint32_t u32Value, unsigned cb);
/** Pointer to a FNPCICONFIGWRITE() function. */
typedef FNPCICONFIGWRITE *PFNPCICONFIGWRITE;
/** Pointer to a PFNPCICONFIGWRITE. */
typedef PFNPCICONFIGWRITE *PPFNPCICONFIGWRITE;

/** Fixed I/O region number for ROM. */
#define PCI_ROM_SLOT 6
/** Max number of I/O regions. */
#define PCI_NUM_REGIONS 7

/*
 * Hack to include the PCIDEVICEINT structure at the right place
 * to avoid duplications of FNPCIIOREGIONMAP and PCI_NUM_REGIONS.
 */
#ifdef PCI_INCLUDE_PRIVATE
# include "PCIInternal.h"
#endif

/**
 * PCI Device structure.
 */
typedef struct PCIDevice
{
    /** PCI config space. */
    uint8_t                 config[256];

    /** Internal data. */
    union
    {
#ifdef PCIDEVICEINT_DECLARED
        PCIDEVICEINT        s;
#endif
        char                padding[256];
    } Int;

    /** Read only data.
     * @{
     */
    /** PCI device number on the pci bus. */
    int32_t                 devfn;
    uint32_t                Alignment0; /**< Alignment. */
    /** Device name. */
    R3PTRTYPE(const char *) name;
    /** Pointer to the device instance which registered the device. */
    PPDMDEVINSR3            pDevIns;
    /**  @} */
} PCIDEVICE;

/* @todo: handle extended space access */
DECLINLINE(void)     PCIDevSetByte(PPCIDEVICE pPciDev, uint32_t uOffset, uint8_t u8Value)
{
    pPciDev->config[uOffset]   = u8Value;
}

DECLINLINE(uint8_t)  PCIDevGetByte(PPCIDEVICE pPciDev, uint32_t uOffset)
{
    return pPciDev->config[uOffset];
}

DECLINLINE(void)     PCIDevSetWord(PPCIDEVICE pPciDev, uint32_t uOffset, uint16_t u16Value)
{
    *(uint16_t*)&pPciDev->config[uOffset] = RT_H2LE_U16(u16Value);
}

DECLINLINE(uint16_t) PCIDevGetWord(PPCIDEVICE pPciDev, uint32_t uOffset)
{
    uint16_t u16Value = *(uint16_t*)&pPciDev->config[uOffset];
    return RT_H2LE_U16(u16Value);
}

DECLINLINE(void)     PCIDevSetDWord(PPCIDEVICE pPciDev, uint32_t uOffset, uint32_t u32Value)
{
    *(uint32_t*)&pPciDev->config[uOffset] = RT_H2LE_U32(u32Value);
}

DECLINLINE(uint32_t) PCIDevGetDWord(PPCIDEVICE pPciDev, uint32_t uOffset)
{
    uint32_t u32Value = *(uint32_t*)&pPciDev->config[uOffset];
    return RT_H2LE_U32(u32Value);
}

DECLINLINE(void)     PCIDevSetQWord(PPCIDEVICE pPciDev, uint32_t uOffset, uint64_t u64Value)
{
    *(uint64_t*)&pPciDev->config[uOffset] = RT_H2LE_U64(u64Value);
}

DECLINLINE(uint64_t) PCIDevGetQWord(PPCIDEVICE pPciDev, uint32_t uOffset)
{
    uint64_t u64Value = *(uint64_t*)&pPciDev->config[uOffset];
    return RT_H2LE_U64(u64Value);
}

/**
 * Sets the vendor id config register.
 * @param   pPciDev         The PCI device.
 * @param   u16VendorId     The vendor id.
 */
DECLINLINE(void) PCIDevSetVendorId(PPCIDEVICE pPciDev, uint16_t u16VendorId)
{
    PCIDevSetWord(pPciDev, VBOX_PCI_VENDOR_ID, u16VendorId);
}

/**
 * Gets the vendor id config register.
 * @returns the vendor id.
 * @param   pPciDev         The PCI device.
 */
DECLINLINE(uint16_t) PCIDevGetVendorId(PPCIDEVICE pPciDev)
{
    return PCIDevGetWord(pPciDev, VBOX_PCI_VENDOR_ID);
}


/**
 * Sets the device id config register.
 * @param   pPciDev         The PCI device.
 * @param   u16DeviceId     The device id.
 */
DECLINLINE(void) PCIDevSetDeviceId(PPCIDEVICE pPciDev, uint16_t u16DeviceId)
{
    PCIDevSetWord(pPciDev, VBOX_PCI_DEVICE_ID, u16DeviceId);
}

/**
 * Gets the device id config register.
 * @returns the device id.
 * @param   pPciDev         The PCI device.
 */
DECLINLINE(uint16_t) PCIDevGetDeviceId(PPCIDEVICE pPciDev)
{
    return PCIDevGetWord(pPciDev, VBOX_PCI_DEVICE_ID);
}

/**
 * Sets the command config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   u16Command      The command register value.
 */
DECLINLINE(void) PCIDevSetCommand(PPCIDEVICE pPciDev, uint16_t u16Command)
{
    PCIDevSetWord(pPciDev, VBOX_PCI_COMMAND, u16Command);
}


/**
 * Gets the command config register.
 * @returns The command register value.
 * @param   pPciDev         The PCI device.
 */
DECLINLINE(uint16_t) PCIDevGetCommand(PPCIDEVICE pPciDev)
{
    return PCIDevGetWord(pPciDev, VBOX_PCI_COMMAND);
}

/**
 * Checks if INTx interrupts disabled in the command config register.
 * @returns true if disabled.
 * @param   pPciDev         The PCI device.
 */
DECLINLINE(bool) PCIDevIsIntxDisabled(PPCIDEVICE pPciDev)
{
    return (PCIDevGetCommand(pPciDev) & VBOX_PCI_COMMAND_INTX_DISABLE) != 0;
}

/**
 * Sets the status config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   u16Status       The status register value.
 */
DECLINLINE(void) PCIDevSetStatus(PPCIDEVICE pPciDev, uint16_t u16Status)
{
    PCIDevSetWord(pPciDev, VBOX_PCI_STATUS, u16Status);
}


/**
 * Sets the revision id config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   u8RevisionId    The revision id.
 */
DECLINLINE(void) PCIDevSetRevisionId(PPCIDEVICE pPciDev, uint8_t u8RevisionId)
{
    PCIDevSetByte(pPciDev, VBOX_PCI_REVISION_ID, u8RevisionId);
}


/**
 * Sets the register level programming class config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   u8ClassProg     The new value.
 */
DECLINLINE(void) PCIDevSetClassProg(PPCIDEVICE pPciDev, uint8_t u8ClassProg)
{
    PCIDevSetByte(pPciDev, VBOX_PCI_CLASS_PROG, u8ClassProg);
}


/**
 * Sets the sub-class (aka device class) config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   u8SubClass      The sub-class.
 */
DECLINLINE(void) PCIDevSetClassSub(PPCIDEVICE pPciDev, uint8_t u8SubClass)
{
    PCIDevSetByte(pPciDev, VBOX_PCI_CLASS_SUB, u8SubClass);
}


/**
 * Sets the base class config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   u8BaseClass     The base class.
 */
DECLINLINE(void) PCIDevSetClassBase(PPCIDEVICE pPciDev, uint8_t u8BaseClass)
{
    PCIDevSetByte(pPciDev, VBOX_PCI_CLASS_BASE, u8BaseClass);
}

/**
 * Sets the header type config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   u8HdrType       The header type.
 */
DECLINLINE(void) PCIDevSetHeaderType(PPCIDEVICE pPciDev, uint8_t u8HdrType)
{
    PCIDevSetByte(pPciDev, VBOX_PCI_HEADER_TYPE, u8HdrType);
}

/**
 * Gets the header type config register.
 *
 * @param   pPciDev         The PCI device.
 * @returns u8HdrType       The header type.
 */
DECLINLINE(uint8_t) PCIDevGetHeaderType(PPCIDEVICE pPciDev)
{
    return PCIDevGetByte(pPciDev, VBOX_PCI_HEADER_TYPE);
}

/**
 * Sets the BIST (built-in self-test)config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   u8Bist          The BIST value.
 */
DECLINLINE(void) PCIDevSetBIST(PPCIDEVICE pPciDev, uint8_t u8Bist)
{
    PCIDevSetByte(pPciDev, VBOX_PCI_BIST, u8Bist);
}

/**
 * Gets the BIST (built-in self-test)config register.
 *
 * @param   pPciDev         The PCI device.
 * @returns u8Bist          The BIST.
 */
DECLINLINE(uint8_t) PCIDevGetBIST(PPCIDEVICE pPciDev)
{
    return PCIDevGetByte(pPciDev, VBOX_PCI_BIST);
}


/**
 * Sets a base address config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   fIOSpace        Whether it's I/O (true) or memory (false) space.
 * @param   fPrefetchable   Whether the memory is prefetachable. Must be false if fIOSpace == true.
 * @param   f64Bit          Whether the memory can be mapped anywhere in the 64-bit address space. Otherwise restrict to 32-bit.
 * @param   u32Addr         The address value.
 */
DECLINLINE(void) PCIDevSetBaseAddress(PPCIDEVICE pPciDev, uint8_t iReg, bool fIOSpace, bool fPrefetchable, bool f64Bit, uint32_t u32Addr)
{
    if (fIOSpace)
    {
        Assert(!(u32Addr & 0x3)); Assert(!fPrefetchable); Assert(!f64Bit);
        u32Addr |= RT_BIT_32(0);
    }
    else
    {
        Assert(!(u32Addr & 0xf));
        if (fPrefetchable)
            u32Addr |= RT_BIT_32(3);
        if (f64Bit)
            u32Addr |= 0x2 << 1;
    }
    switch (iReg)
    {
        case 0: iReg = VBOX_PCI_BASE_ADDRESS_0; break;
        case 1: iReg = VBOX_PCI_BASE_ADDRESS_1; break;
        case 2: iReg = VBOX_PCI_BASE_ADDRESS_2; break;
        case 3: iReg = VBOX_PCI_BASE_ADDRESS_3; break;
        case 4: iReg = VBOX_PCI_BASE_ADDRESS_4; break;
        case 5: iReg = VBOX_PCI_BASE_ADDRESS_5; break;
        default: AssertFailedReturnVoid();
    }

    PCIDevSetDWord(pPciDev, iReg, u32Addr);
}


/**
 * Sets the sub-system vendor id config register.
 *
 * @param   pPciDev             The PCI device.
 * @param   u16SubSysVendorId   The sub-system vendor id.
 */
DECLINLINE(void) PCIDevSetSubSystemVendorId(PPCIDEVICE pPciDev, uint16_t u16SubSysVendorId)
{
    PCIDevSetWord(pPciDev, VBOX_PCI_SUBSYSTEM_VENDOR_ID, u16SubSysVendorId);
}

/**
 * Gets the sub-system vendor id config register.
 * @returns the sub-system vendor id.
 * @param   pPciDev         The PCI device.
 */
DECLINLINE(uint16_t) PCIDevGetSubSystemVendorId(PPCIDEVICE pPciDev)
{
    return PCIDevGetWord(pPciDev, VBOX_PCI_SUBSYSTEM_VENDOR_ID);
}


/**
 * Sets the sub-system id config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   u16SubSystemId  The sub-system id.
 */
DECLINLINE(void) PCIDevSetSubSystemId(PPCIDEVICE pPciDev, uint16_t u16SubSystemId)
{
    PCIDevSetWord(pPciDev, VBOX_PCI_SUBSYSTEM_ID, u16SubSystemId);
}

/**
 * Gets the sub-system id config register.
 * @returns the sub-system id.
 * @param   pPciDev         The PCI device.
 */
DECLINLINE(uint16_t) PCIDevGetSubSystemId(PPCIDEVICE pPciDev)
{
    return PCIDevGetWord(pPciDev, VBOX_PCI_SUBSYSTEM_ID);
}

/**
 * Sets offset to capability list.
 *
 * @param   pPciDev         The PCI device.
 * @param   u8Offset        The offset to capability list.
 */
DECLINLINE(void) PCIDevSetCapabilityList(PPCIDEVICE pPciDev, uint8_t u8Offset)
{
    PCIDevSetByte(pPciDev, VBOX_PCI_CAPABILITY_LIST, u8Offset);
}

/**
 * Returns offset to capability list.
 *
 * @returns offset to capability list.
 * @param   pPciDev         The PCI device.
 */
DECLINLINE(uint8_t) PCIDevGetCapabilityList(PPCIDEVICE pPciDev)
{
    return PCIDevGetByte(pPciDev, VBOX_PCI_CAPABILITY_LIST);
}

/**
 * Sets the interrupt line config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   u8Line          The interrupt line.
 */
DECLINLINE(void) PCIDevSetInterruptLine(PPCIDEVICE pPciDev, uint8_t u8Line)
{
    PCIDevSetByte(pPciDev, VBOX_PCI_INTERRUPT_LINE, u8Line);
}

/**
 * Gets the interrupt line config register.
 *
 * @returns The interrupt line.
 * @param   pPciDev         The PCI device.
 */
DECLINLINE(uint8_t) PCIDevGetInterruptLine(PPCIDEVICE pPciDev)
{
    return PCIDevGetByte(pPciDev, VBOX_PCI_INTERRUPT_LINE);
}

/**
 * Sets the interrupt pin config register.
 *
 * @param   pPciDev         The PCI device.
 * @param   u8Pin           The interrupt pin.
 */
DECLINLINE(void) PCIDevSetInterruptPin(PPCIDEVICE pPciDev, uint8_t u8Pin)
{
    PCIDevSetByte(pPciDev, VBOX_PCI_INTERRUPT_PIN, u8Pin);
}

/**
 * Gets the interrupt pin config register.
 *
 * @returns The interrupt pin.
 * @param   pPciDev         The PCI device.
 */
DECLINLINE(uint8_t) PCIDevGetInterruptPin(PPCIDEVICE pPciDev)
{
    return PCIDevGetByte(pPciDev, VBOX_PCI_INTERRUPT_PIN);
}

#ifdef PCIDEVICEINT_DECLARED
DECLINLINE(void) PCISetRequestedDevfunc(PPCIDEVICE pDev)
{
    pDev->Int.s.uFlags |= PCIDEV_FLAG_REQUESTED_DEVFUNC;
}

DECLINLINE(void) PCIClearRequestedDevfunc(PPCIDEVICE pDev)
{
    pDev->Int.s.uFlags &= ~PCIDEV_FLAG_REQUESTED_DEVFUNC;
}

DECLINLINE(bool) PCIIsRequestedDevfunc(PPCIDEVICE pDev)
{
    return (pDev->Int.s.uFlags & PCIDEV_FLAG_REQUESTED_DEVFUNC) != 0;
}

DECLINLINE(void) PCISetPci2PciBridge(PPCIDEVICE pDev)
{
    pDev->Int.s.uFlags |= PCIDEV_FLAG_PCI_TO_PCI_BRIDGE;
}

DECLINLINE(bool) PCIIsPci2PciBridge(PPCIDEVICE pDev)
{
    return (pDev->Int.s.uFlags & PCIDEV_FLAG_PCI_TO_PCI_BRIDGE) != 0;
}

DECLINLINE(void) PCISetPciExpress(PPCIDEVICE pDev)
{
    pDev->Int.s.uFlags |= PCIDEV_FLAG_PCI_EXPRESS_DEVICE;
}

DECLINLINE(bool) PCIIsPciExpress(PPCIDEVICE pDev)
{
    return (pDev->Int.s.uFlags & PCIDEV_FLAG_PCI_EXPRESS_DEVICE) != 0;
}

DECLINLINE(void) PCISetMsiCapable(PPCIDEVICE pDev)
{
    pDev->Int.s.uFlags |= PCIDEV_FLAG_MSI_CAPABLE;
}

DECLINLINE(void) PCIClearMsiCapable(PPCIDEVICE pDev)
{
    pDev->Int.s.uFlags &= ~PCIDEV_FLAG_MSI_CAPABLE;
}

DECLINLINE(bool) PCIIsMsiCapable(PPCIDEVICE pDev)
{
    return (pDev->Int.s.uFlags & PCIDEV_FLAG_MSI_CAPABLE) != 0;
}

DECLINLINE(void) PCISetMsixCapable(PPCIDEVICE pDev)
{
    pDev->Int.s.uFlags |= PCIDEV_FLAG_MSIX_CAPABLE;
}

DECLINLINE(void) PCIClearMsixCapable(PPCIDEVICE pDev)
{
    pDev->Int.s.uFlags &= ~PCIDEV_FLAG_MSIX_CAPABLE;
}

DECLINLINE(bool) PCIIsMsixCapable(PPCIDEVICE pDev)
{
    return (pDev->Int.s.uFlags & PCIDEV_FLAG_MSIX_CAPABLE) != 0;
}
#endif

/** @} */

#endif
