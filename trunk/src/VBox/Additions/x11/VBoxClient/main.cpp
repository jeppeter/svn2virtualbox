/** @file
 *
 * VirtualBox Guest Service:
 * Linux guest.
 */

/*
 * Copyright (C) 2006-2010 Sun Microsystems, Inc.
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

#include <sys/types.h>
#include <stdlib.h>       /* For exit */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <X11/Xlib.h>

#include <iprt/env.h>
#include <iprt/initterm.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/log.h>

#include "VBoxClient.h"

#define TRACE RTPrintf("%s: %d\n", __PRETTY_FUNCTION__, __LINE__); Log(("%s: %d\n", __PRETTY_FUNCTION__, __LINE__))

static int (*gpfnOldIOErrorHandler)(Display *) = NULL;

/** Object representing the service we are running.  This has to be global
 * so that the cleanup routine can access it. */
VBoxClient::Service *g_pService;
/** The name of our pidfile.  It is global for the benefit of the cleanup
 * routine. */
static char g_szPidFile[RTPATH_MAX];
/** The file handle of our pidfile.  It is global for the benefit of the
 * cleanup routine. */
static RTFILE g_hPidFile;

/** Clean up if we get a signal or something.  This is extern so that we
 * can call it from other compilation units. */
void VBoxClient::CleanUp()
{
    if (g_pService)
    {
        g_pService->cleanup();
        delete g_pService;
    }
    if (g_szPidFile && g_hPidFile)
        VbglR3ClosePidFile(g_szPidFile, g_hPidFile);
    VbglR3Term();
    exit(0);
}

/**
 * A standard signal handler which cleans up and exits.
 */
void vboxClientSignalHandler(int cSignal)
{
    Log(("VBoxClient: terminated with signal %d\n", cSignal));
    /** Disable seamless mode */
    RTPrintf(("VBoxClient: terminating...\n"));
    VBoxClient::CleanUp();
}

/**
 * Xlib error handler for certain errors that we can't avoid.
 */
int vboxClientXLibErrorHandler(Display *pDisplay, XErrorEvent *pError)
{
    char errorText[1024];

    XGetErrorText(pDisplay, pError->error_code, errorText, sizeof(errorText));
    LogRelFlow(("VBoxClient: an X Window protocol error occurred: %s (error code %d).  Request code: %d, minor code: %d, serial number: %d\n", errorText, pError->error_code, pError->request_code, pError->minor_code, pError->serial));
    return 0;  /* We should never reach this. */
}

/**
 * Xlib error handler for fatal errors.  This often means that the programme is still running
 * when X exits.
 */
static int vboxClientXLibIOErrorHandler(Display *pDisplay)
{
    Log(("VBoxClient: a fatal guest X Window error occurred.  This may just mean that the Window system was shut down while the client was still running.\n"));
    VBoxClient::CleanUp();
    return 0;  /* We should never reach this. */
}

/**
 * Reset all standard termination signals to call our signal handler, which
 * cleans up and exits.
 */
void vboxClientSetSignalHandlers(void)
{
    struct sigaction sigAction;

    LogFlowFunc(("\n"));
    sigAction.sa_handler = vboxClientSignalHandler;
    sigemptyset(&sigAction.sa_mask);
    sigAction.sa_flags = 0;
    sigaction(SIGHUP, &sigAction, NULL);
    sigaction(SIGINT, &sigAction, NULL);
    sigaction(SIGQUIT, &sigAction, NULL);
    sigaction(SIGABRT, &sigAction, NULL);
    sigaction(SIGPIPE, &sigAction, NULL);
    sigaction(SIGALRM, &sigAction, NULL);
    sigaction(SIGTERM, &sigAction, NULL);
    sigaction(SIGUSR1, &sigAction, NULL);
    sigaction(SIGUSR2, &sigAction, NULL);
    LogFlowFunc(("returning\n"));
}

/**
 * Print out a usage message and exit with success.
 */
void vboxClientUsage(const char *pcszFileName)
{
    RTPrintf("Usage: %s --clipboard|--display|--checkhostversion|--seamless [-d|--nodaemon]\n", pcszFileName);
    RTPrintf("Start the VirtualBox X Window System guest services.\n\n");
    RTPrintf("Options:\n");
    RTPrintf("  --clipboard        start the shared clipboard service\n");
    RTPrintf("  --display          start the display management service\n");
# ifdef VBOX_WITH_GUEST_PROPS
    RTPrintf("  --checkhostversion start the host version notifier service\n");
# endif
    RTPrintf("  --seamless         start the seamless windows service\n");
    RTPrintf("  -d, --nodaemon     continue running as a system service\n");
    RTPrintf("\n");
    exit(0);
}

/**
 * The main loop for the VBoxClient daemon.
 */
int main(int argc, char *argv[])
{
    int rcClipboard, rc = VINF_SUCCESS;
    const char *pszFileName = RTPathFilename(argv[0]);
    bool fDaemonise = true;
    /* Have any fatal errors occurred yet? */
    bool fSuccess = true;
    /* Do we know which service we wish to run? */
    bool fHaveService = false;

    if (NULL == pszFileName)
        pszFileName = "VBoxClient";

    /* Initialise our runtime before all else. */
    RTR3Init();

    /* Parse our option(s) */
    /** @todo Use RTGetOpt() if the arguments become more complex. */
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--nodaemon"))
            fDaemonise = false;
        else if (!strcmp(argv[i], "--clipboard"))
        {
            if (g_pService == NULL)
                g_pService = VBoxClient::GetClipboardService();
            else
                fSuccess = false;
        }
        else if (!strcmp(argv[i], "--display"))
        {
            if (g_pService == NULL)
                g_pService = VBoxClient::GetDisplayService();
            else
                fSuccess = false;
        }
        else if (!strcmp(argv[i], "--seamless"))
        {
            if (g_pService == NULL)
                g_pService = VBoxClient::GetSeamlessService();
            else
                fSuccess = false;
        }
        else if (!strcmp(argv[i], "--checkhostversion"))
        {
            if (g_pService == NULL)
                g_pService = VBoxClient::GetHostVersionService();
            else
                fSuccess = false;
        }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            vboxClientUsage(pszFileName);
            exit(0);
        }
        else
        {
            RTPrintf("%s: unrecognized option `%s'\n", pszFileName, argv[i]);
            RTPrintf("Try `%s --help' for more information\n", pszFileName);
            exit(1);
        }
    }
    if (!fSuccess || !g_pService)
    {
        vboxClientUsage(pszFileName);
        exit(1);
    }
    if (fDaemonise)
    {
        rc = VbglR3Daemonize(false /* fNoChDir */, false /* fNoClose */);
        if (RT_FAILURE(rc))
        {
            RTPrintf("VBoxClient: failed to daemonize.  Exiting.\n");
            Log(("VBoxClient: failed to daemonize.  Exiting.\n"));
# ifdef DEBUG
            RTPrintf("Error %Rrc\n", rc);
# endif
            return 1;
        }
    }
    /** @todo explain why we aren't using RTPathUserHome here so it doesn't get
     *        changed accidentally during some cleanup effort. */
    rc = RTEnvGetEx(RTENV_DEFAULT, "HOME", g_szPidFile, sizeof(g_szPidFile), NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("VBoxClient: failed to get home directory, rc=%Rrc.  Exiting.\n", rc);
        Log(("VBoxClient: failed to get home directory, rc=%Rrc.  Exiting.\n", rc));
        return 1;
    }
    rc = RTPathAppend(g_szPidFile, sizeof(g_szPidFile), g_pService->getPidFilePath());
    if (RT_FAILURE(rc))
    {
        RTPrintf("VBoxClient: RTPathAppend failed with rc=%Rrc.  Exiting.\n", rc);
        Log(("VBoxClient: RTPathAppend failed with rc=%Rrc.  Exiting.\n", rc));
        return 1;
    }

    /* Initialise the guest library. */
    if (RT_FAILURE(VbglR3InitUser()))
    {
        RTPrintf("Failed to connect to the VirtualBox kernel service\n");
        Log(("Failed to connect to the VirtualBox kernel service\n"));
        return 1;
    }
    if (g_szPidFile && RT_FAILURE(VbglR3PidFile(g_szPidFile, &g_hPidFile)))
    {
        RTPrintf("Failed to create a pidfile.  Exiting.\n");
        Log(("Failed to create a pidfile.  Exiting.\n"));
        VbglR3Term();
        return 1;
    }
    /* Set signal handlers to clean up on exit. */
    vboxClientSetSignalHandlers();
    /* Set an X11 error handler, so that we don't die when we get unavoidable errors. */
    XSetErrorHandler(vboxClientXLibErrorHandler);
    /* Set an X11 I/O error handler, so that we can shutdown properly on fatal errors. */
    XSetIOErrorHandler(vboxClientXLibIOErrorHandler);
    g_pService->run(fDaemonise);
    VBoxClient::CleanUp();
    return 1;  /* We should never get here. */
}
