/** @file
 * PDM - Pluggable Device Manager, Threads.
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */

#ifndef ___VBox_pdm_h
# include <VBox/pdm.h>
#endif

#ifndef ___VBox_pdmthread_h
#define ___VBox_pdmthread_h

__BEGIN_DECLS

/** @group grp_pdm_thread       Threads
 * @ingroup grp_pdm
 * @{
 */

/**
 * The thread state
 */
typedef enum PDMTHREADSTATE
{
    /** The usual invalid 0 entry. */
    PDMTHREADSTATE_INVALID = 0,
    /** The thread is initializing.
     * Prev state: none
     * Next state: suspended, terminating (error) */
    PDMTHREADSTATE_INITIALIZING,
    /** The thread has been asked to suspend.
     * Prev state: running
     * Next state: suspended */
    PDMTHREADSTATE_SUSPENDING,
    /** The thread is supended.
     * Prev state: suspending, initializing
     * Next state: resuming, terminated. */
    PDMTHREADSTATE_SUSPENDED,
    /** The thread is active.
     * Prev state: suspended
     * Next state: running, terminating. */
    PDMTHREADSTATE_RESUMING,
    /** The thread is active.
     * Prev state: resuming
     * Next state: suspending, terminating. */
    PDMTHREADSTATE_RUNNING,
    /** The thread has been asked to terminate.
     * Prev state: initializing, suspended, resuming, running
     * Next state: terminated. */
    PDMTHREADSTATE_TERMINATING,
    /** The thread is terminating / has terminated.
     * Prev state: terminating
     * Next state: none */
    PDMTHREADSTATE_TERMINATED,
    /** The usual 32-bit hack. */
    PDMTHREADSTATE_32BIT_HACK = 0x7fffffff
} PDMTHREADSTATE;

/** A pointer to a PDM thread. */
typedef R3PTRTYPE(struct PDMTHREAD *) PPDMTHREAD;
/** A pointer to a pointer to a PDM thread. */
typedef PPDMTHREAD *PPPDMTHREAD;

/**
 * PDM thread, device variation.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThread     The PDM thread data.
 */
typedef int FNPDMTHREADDEV(PPDMDEVINS pDevIns, PPDMTHREAD pThread);
/** Pointer to a FNPDMTHREADDEV(). */
typedef FNPDMTHREADDEV *PFNPDMTHREADDEV;

/**
 * PDM thread, driver variation.
 *
 * @returns VBox status code.
 * @param   pDrvIns     The driver instance.
 * @param   pThread     The PDM thread data.
 */
typedef int FNPDMTHREADDRV(PPDMDRVINS pDrvIns, PPDMTHREAD pThread);
/** Pointer to a FNPDMTHREADDRV(). */
typedef FNPDMTHREADDRV *PFNPDMTHREADDRV;

/**
 * PDM thread, driver variation.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   pThread     The PDM thread data.
 */
typedef int FNPDMTHREADINT(PVM pVM, PPDMTHREAD pThread);
/** Pointer to a FNPDMTHREADINT(). */
typedef FNPDMTHREADINT *PFNPDMTHREADINT;

/**
 * PDM thread, driver variation.
 *
 * @returns VBox status code.
 * @param   pThread     The PDM thread data.
 */
typedef int FNPDMTHREADEXT(PPDMTHREAD pThread);
/** Pointer to a FNPDMTHREADEXT(). */
typedef FNPDMTHREADEXT *PFNPDMTHREADEXT;



/**
 * PDM thread wakup call, device variation.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThread     The PDM thread data.
 */
typedef int FNPDMTHREADWAKEUPDEV(PPDMDEVINS pDevIns, PPDMTHREAD pThread);
/** Pointer to a FNPDMTHREADDEV(). */
typedef FNPDMTHREADWAKEUPDEV *PFNPDMTHREADWAKEUPDEV;

/**
 * PDM thread wakup call, driver variation.
 *
 * @returns VBox status code.
 * @param   pDrvIns     The driver instance.
 * @param   pThread     The PDM thread data.
 */
typedef int FNPDMTHREADWAKEUPDRV(PPDMDRVINS pDrvIns, PPDMTHREAD pThread);
/** Pointer to a FNPDMTHREADDRV(). */
typedef FNPDMTHREADWAKEUPDRV *PFNPDMTHREADWAKEUPDRV;

/**
 * PDM thread wakup call, internal variation.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   pThread     The PDM thread data.
 */
typedef int FNPDMTHREADWAKEUPINT(PVM pVM, PPDMTHREAD pThread);
/** Pointer to a FNPDMTHREADWAKEUPINT(). */
typedef FNPDMTHREADWAKEUPINT *PFNPDMTHREADWAKEUPINT;

/**
 * PDM thread wakup call, external variation.
 *
 * @returns VBox status code.
 * @param   pThread     The PDM thread data.
 */
typedef int FNPDMTHREADWAKEUPEXT(PPDMTHREAD pThread);
/** Pointer to a FNPDMTHREADEXT(). */
typedef FNPDMTHREADWAKEUPEXT *PFNPDMTHREADWAKEUPEXT;


/**
 * PDM Thread instance data.
 */
typedef struct PDMTHREAD
{
    /** PDMTHREAD_VERSION. */
    uint32_t                    u32Version;
    /** The thread state. */
    PDMTHREADSTATE volatile     enmState;
    /** The thread handle. */
    RTTHREAD                    Thread;
    /** The user parameter. */
    R3PTRTYPE(void *)           pvUser;
    /** Data specific to the kind of thread.
     * This should really be in PDMTHREADINT, but is placed here because of the
     * function pointer typedefs. So, don't touch these, please.
     */
    union
    {
        /** PDMTHREADTYPE_DEVICE data. */
        struct
        {
            /** The device instance. */
            PPDMDEVINSR3                        pDevIns;
            /** The thread function. */
            R3PTRTYPE(PFNPDMTHREADDEV)          pfnThread;
            /** Thread. */
            R3PTRTYPE(PFNPDMTHREADWAKEUPDEV)    pfnWakeup;
        } Dev;

        /** PDMTHREADTYPE_DRIVER data. */
        struct
        {
            /** The driver instance. */
            R3PTRTYPE(PPDMDRVINS)               pDrvIns;
            /** The thread function. */
            R3PTRTYPE(PFNPDMTHREADDRV)          pfnThread;
            /** Thread. */
            R3PTRTYPE(PFNPDMTHREADWAKEUPDRV)    pfnWakeup;
        } Drv;

        /** PDMTHREADTYPE_INTERNAL data. */
        struct
        {
            /** The thread function. */
            R3PTRTYPE(PFNPDMTHREADINT)          pfnThread;
            /** Thread. */
            R3PTRTYPE(PFNPDMTHREADWAKEUPINT)    pfnWakeup;
        } Int;

        /** PDMTHREADTYPE_EXTERNAL data. */
        struct
        {
            /** The thread function. */
            R3PTRTYPE(PFNPDMTHREADEXT)          pfnThread;
            /** Thread. */
            R3PTRTYPE(PFNPDMTHREADWAKEUPEXT)    pfnWakeup;
        } Ext;
    } u;

    /** Internal data. */
    union
    {
#ifdef PDMTHREADINT_DECLARED
        PDMTHREADINT            s;
#endif
        uint8_t                 padding[64];
    } Internal;
} PDMTHREAD;

/** PDMTHREAD::u32Version value. */
#define PDMTHREAD_VERSION   0xef010000


/** @} */

__END_DECLS

#endif
