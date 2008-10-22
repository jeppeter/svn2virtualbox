/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * The main() function
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
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

#include "VBoxGlobal.h"
#include "VBoxProblemReporter.h"
#include "VBoxSelectorWnd.h"
#include "VBoxConsoleWnd.h"
#ifdef Q_WS_MAC
# include "QIApplication.h"
# include "VBoxUtils.h"
#else
# define QIApplication QApplication
#endif

#ifdef Q_WS_X11
#include <QFontDatabase>
#endif

#include <QCleanlooksStyle>
#include <QPlastiqueStyle>
#include <qmessagebox.h>
#include <qlocale.h>
#include <qtranslator.h>

#include <iprt/runtime.h>
#include <iprt/stream.h>
#ifdef VBOX_WITH_HARDENING
# include <VBox/sup.h>
#endif

#if defined(DEBUG) && defined(Q_WS_X11) && defined(RT_OS_LINUX)

#include <signal.h>
#include <execinfo.h>

/* get REG_EIP from ucontext.h */
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <ucontext.h>
#ifdef RT_ARCH_AMD64
# define REG_PC REG_RIP
#else
# define REG_PC REG_EIP
#endif

/**
 * the signal handler that prints out a backtrace of the call stack.
 * the code is taken from http://www.linuxjournal.com/article/6391.
 */
void bt_sighandler (int sig, siginfo_t *info, void *secret) {

    void *trace[16];
    char **messages = (char **)NULL;
    int i, trace_size = 0;
    ucontext_t *uc = (ucontext_t *)secret;

    /* Do something useful with siginfo_t */
    if (sig == SIGSEGV)
        Log (("GUI: Got signal %d, faulty address is %p, from %p\n",
              sig, info->si_addr, uc->uc_mcontext.gregs[REG_PC]));
    else
        Log (("GUI: Got signal %d\n", sig));

    trace_size = backtrace (trace, 16);
    /* overwrite sigaction with caller's address */
    trace[1] = (void *) uc->uc_mcontext.gregs [REG_PC];

    messages = backtrace_symbols (trace, trace_size);
    /* skip first stack frame (points here) */
    Log (("GUI: [bt] Execution path:\n"));
    for (i = 1; i < trace_size; ++i)
        Log (("GUI: [bt] %s\n", messages[i]));

    exit (0);
}

#endif // defined(DEBUG) && defined(Q_WS_X11) && defined(RT_OS_LINUX)

/**
 * Qt warning/debug/fatal message handler.
 */
static void QtMessageOutput (QtMsgType type, const char *msg)
{
#ifndef Q_WS_X11
    NOREF(msg);
#endif
    switch (type)
    {
        case QtDebugMsg:
            Log (("Qt DEBUG: %s\n", msg));
            break;
        case QtWarningMsg:
            Log (("Qt WARNING: %s\n", msg));
#ifdef Q_WS_X11
            /* Needed for instance for the message ``cannot connect to X server'' */
            RTStrmPrintf(g_pStdErr, "Qt WARNING: %s\n", msg);
#endif
            break;
        case QtCriticalMsg:
            Log (("Qt CRITICAL: %s\n", msg));
#ifdef Q_WS_X11
            /* Needed for instance for the message ``cannot connect to X server'' */
            RTStrmPrintf(g_pStdErr, "Qt CRITICAL: %s\n", msg);
#endif
            break;
        case QtFatalMsg:
            Log (("Qt FATAL: %s\n", msg));
#ifdef Q_WS_X11
            RTStrmPrintf(g_pStdErr, "Qt FATAL: %s\n", msg);
#endif
    }
}

#ifndef Q_WS_WIN
/**
 * Shows all available command line parameters.
 */
static void ShowHelp()
{
    QString mode = "", dflt = "";
#ifdef VBOX_GUI_USE_SDL
    mode += "sdl";
#endif
#ifdef VBOX_GUI_USE_QIMAGE
    if (!mode.isEmpty())
        mode += "|";
    mode += "image";
#endif
#ifdef VBOX_GUI_USE_DDRAW
    if (!mode.isEmpty())
        mode += "|";
    mode += "ddraw";
#endif
#ifdef VBOX_GUI_USE_QUARTZ2D
    if (!mode.isEmpty())
        mode += "|";
    mode += "quartz2d";
#endif
#if defined (Q_WS_MAC) && defined (VBOX_GUI_USE_QUARTZ2D)
    dflt = "quartz2d";
#elif (defined (Q_WS_WIN32) || defined (Q_WS_PM)) && defined (VBOX_GUI_USE_QIMAGE)
    dflt = "image";
#elif defined (Q_WS_X11) && defined (VBOX_GUI_USE_SDL)
    dflt = "sdl";
#else
    dflt = "image";
#endif

    RTPrintf("Sun xVM VirtualBox Graphical User Interface "VBOX_VERSION_STRING"\n"
            "(C) 2005-2008 Sun Microsystems, Inc.\n"
            "All rights reserved.\n"
            "\n"
            "Usage:\n"
            "  -startvm <vmname|UUID>     start a VM by specifying its UUID or name\n"
            "  -rmode %-19s select different render mode (default is %s)\n",
            mode.toLatin1().constData(),
            dflt.toLatin1().constData());
}
#endif // ifndef Q_WS_WIN

/*
 * Initializes Qt and the rest. Must be called before
 */
static void InitQtAndAll (QIApplication &aApp, int argc = 0, char **argv = NULL)
{

}

////////////////////////////////////////////////////////////////////////////////

/**
 * The thing. Initializes Qt and all and does the opposite when dissolved.
 */
class TheThing
{
public:

    TheThing (int &argc, char ** &argv)
        : mIsOk (false), mApp (NULL)
    { init (&argc, &argv); }

    TheThing()
        : mIsOk (false), mApp (NULL)
    { init(); }

    ~TheThing() { uninit(); }

    bool isOk() const { return mIsOk; }

    QIApplication &app() const { return *mApp; }

private:

    void init (int *argc = NULL, char ***argv = NULL);
    void uninit();

    bool mIsOk;

#ifdef Q_WS_WIN
    HRESULT mInitRC;
#endif

    QIApplication *mApp;

    char mAppRaw [sizeof (QIApplication)];

    static int mFakeArgc;
    static char *mFakeArgv [2];
};

int TheThing::mFakeArgc = 0;
char *TheThing::mFakeArgv [2] = { NULL, NULL };

/**
 * Initializer of the thing.
 */
void TheThing::init (int *argc /*= NULL*/, char ***argv /*= NULL*/)
{
    LogFlowFuncEnter();

#ifdef Q_WS_WIN
    /* Initialize COM early, before QApplication calls OleInitialize(), to
     * make sure we enter the multi threded apartment instead of a single
     * threaded one. Note that this will make some non-threadsafe system
     * services that use OLE and require STA (such as Drag&Drop) not work
     * anymore, however it's still better because otherwise VBox will not work
     * on some Windows XP systems at all since it requires MTA (we cannot
     * leave STA by calling CoUninitialize() and re-enter MTA on those systems
     * for some unknown reason), see also src/VBox/Main/glue/initterm.cpp. */
    /// @todo find a proper solution that satisfies both OLE and VBox
    mInitRC = COMBase::InitializeCOM();
#endif

    qInstallMsgHandler (QtMessageOutput);

    /* guarantee successful allocation */
    mApp = new (&mAppRaw) QIApplication (argc != NULL ? *argc : mFakeArgc,
                                         argv != NULL ? *argv : mFakeArgv);
    Assert (mApp != NULL);

    /* Qt4.3 version has the QProcess bug which freezing the application
     * for 30 seconds. This bug is internally used at initialization of
     * Cleanlooks style. So we have to change this style to another one.
     * See http://trolltech.com/developer/task-tracker/index_html?id=179200&method=entry
     * for details. */
    if (QString (qVersion()).startsWith ("4.3") &&
        qobject_cast <QCleanlooksStyle*> (QApplication::style()))
        QApplication::setStyle (new QPlastiqueStyle);

#ifdef Q_WS_X11
    /* Cause Qt4 has the conflict with fontconfig application as a result
     * sometimes substituting some fonts with non scaleable-anti-aliased
     * bitmap font we are reseting substitutes for the current application
     * font family if it is non scaleable-anti-aliased. */
    QFontDatabase fontDataBase;
    QString subFamily (QFont::substitute (QApplication::font().family()));
    bool isScaleable = fontDataBase.isSmoothlyScalable (subFamily);
    if (!isScaleable)
        QFont::removeSubstitution (QApplication::font().family());
#endif

#ifdef Q_WS_WIN
    /* Drag in the sound drivers and DLLs early to get rid of the delay taking
     * place when the main menu bar (or any action from that menu bar) is
     * activated for the first time. This delay is especially annoying if it
     * happens when the VM is executing in real mode (which gives 100% CPU
     * load and slows down the load process that happens on the main GUI
     * thread to several seconds). */
    PlaySound (NULL, NULL, 0);
#endif

#ifdef Q_WS_MAC
    ::darwinDisableIconsInMenus();
#endif /* Q_WS_MAC */

#ifdef Q_WS_X11
    /* version check (major.minor are sensitive, fix number is ignored) */
    QString ver_str = QString::fromLatin1 (QT_VERSION_STR);
    QString ver_str_base = ver_str.section ('.', 0, 1);
    QString rt_ver_str = QString::fromLatin1 (qVersion());
    uint ver =
        (ver_str.section ('.', 0, 0).toInt() << 16) +
        (ver_str.section ('.', 1, 1).toInt() << 8) +
        ver_str.section ('.', 2, 2).toInt();
    uint rt_ver =
        (rt_ver_str.section ('.', 0, 0).toInt() << 16) +
        (rt_ver_str.section ('.', 1, 1).toInt() << 8) +
        rt_ver_str.section ('.', 2, 2).toInt();
    if (rt_ver < (ver & 0xFFFF00))
    {
        QString msg =
            QApplication::tr ("Executable <b>%1</b> requires Qt %2.x, found Qt %3.")
                              .arg (qAppName())
                              .arg (ver_str_base)
                              .arg (rt_ver_str);
        QMessageBox::critical (
            0, QApplication::tr ("Incompatible Qt Library Error"),
            msg, QMessageBox::Abort, 0);
        qFatal (msg.toAscii().constData());
    }
#endif

    /* load a translation based on the current locale */
    VBoxGlobal::loadLanguage();

#ifdef Q_WS_WIN
    /* Check for the COM error after we've initialized Qt */
    if (FAILED (mInitRC))
    {
        vboxProblem().cannotInitCOM (mInitRC);
        return;
    }
#endif

    if (!vboxGlobal().isValid())
        return;

    /* Well, Ok... we are fine */
    mIsOk = true;

    LogFlowFuncLeave();
}

/**
 * Uninitializer of the thing.
 */
void TheThing::uninit()
{
    LogFlowFuncEnter();

    if (mApp != NULL)
    {
        mApp->~QIApplication();
        mApp = NULL;
    }

#ifdef Q_WS_WIN
    /* See COMBase::initializeCOM() in init() */
    if (SUCCEEDED (mInitRC))
        COMBase::CleanupCOM();
#endif

    LogFlowFuncLeave();
}

////////////////////////////////////////////////////////////////////////////////

/**
 * Trusted entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain (int argc, char **argv, char ** /*envp*/)
{
    LogFlowFuncEnter();

#ifndef Q_WS_WIN
    int i;
    for (i=0; i<argc; i++)
        if (   !strcmp(argv[i], "-h")
            || !strcmp(argv[i], "-?")
            || !strcmp(argv[i], "-help")
            || !strcmp(argv[i], "--help"))
        {
            ShowHelp();
            return 0;
        }
#endif

#if defined(DEBUG) && defined(Q_WS_X11) && defined(RT_OS_LINUX)
    /* install our signal handler to backtrace the call stack */
    struct sigaction sa;
    sa.sa_sigaction = bt_sighandler;
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction (SIGSEGV, &sa, NULL);
    sigaction (SIGBUS, &sa, NULL);
    sigaction (SIGUSR1, &sa, NULL);
#endif

    int rc = 1; /* failure */

    /* scope TheThing */
    do
    {
        TheThing theThing (argc, argv);

        if (theThing.isOk())
        {
#ifndef VBOX_OSE
#ifdef Q_WS_X11
            /* show the user license file */
            if (!vboxGlobal().showVirtualBoxLicense())
                break;
#endif
#endif
            vboxGlobal().checkForAutoConvertedSettings();

            VBoxGlobalSettings settings = vboxGlobal().settings();
            /* Process known keys */
            bool noSelector = settings.isFeatureActive ("noSelector");

            if (vboxGlobal().isVMConsoleProcess())
            {
                vboxGlobal().setMainWindow (&vboxGlobal().consoleWnd());
                if (vboxGlobal().startMachine (vboxGlobal().managedVMUuid()))
                    rc = theThing.app().exec();
            }
            else if (noSelector)
            {
                vboxProblem().cannotRunInSelectorMode();
            }
            else
            {
                vboxGlobal().setMainWindow (&vboxGlobal().selectorWnd());
                vboxGlobal().selectorWnd().show();
#ifdef VBOX_WITH_REGISTRATION_REQUEST
                vboxGlobal().showRegistrationDialog (false /* aForce */);
#endif
#ifdef VBOX_WITH_UPDATE_REQUEST
                vboxGlobal().showUpdateDialog (false /* aForce */);
#endif
                vboxGlobal().startEnumeratingMedia();
                rc = theThing.app().exec();
            }
        }
    }
    while (0);

    LogFlowFunc (("rc=%d\n", rc));
    LogFlowFuncLeave();

    return rc;
}

#ifndef VBOX_WITH_HARDENING

/**
 * Untrusted entry point. Calls TrustedMain().
 */
int main (int argc, char **argv, char **envp)
{
    /* Initialize VBox Runtime. Initialize the SUPLib as well only if we
     * are really about to start a VM. Don't do this if we are only starting
     * the selector window. */
    bool fInitSUPLib = false;
    for (int i = 0; i < argc; i++)
    {
        if (!::strcmp (argv[i], "-startvm" ))
        {
            fInitSUPLib = true;
            break;
        }
    }

    if (!fInitSUPLib)
        RTR3Init();
    else
        RTR3InitAndSUPLib();

    return TrustedMain (argc, argv, envp);
}

#else  /* VBOX_WITH_HARDENING */

////////////////////////////////////////////////////////////////////////////////

/**
 * Hardened main failed (TrustedMain() not called), report the error without any
 * unnecessary fuzz.
 *
 * @remarks Do not call IPRT here unless really required, it might not be
 *          initialized.
 */
extern "C" DECLEXPORT(void) TrustedError (const char *pszWhere,
                                          SUPINITOP enmWhat, int rc,
                                          const char *pszMsgFmt, va_list va)
{
    TheThing theThing;

    /*
     * Open the direct session to let the spawning progress dialog of the VM
     * Selector window disappear. Otherwise, the dialog will pop up after 2
     * seconds and become the foreground window hiding the message box we will
     * show below.
     *
     * @todo Note that this is an ugly workaround because the real problem is
     * the broken Window Manager on some platforms which allows windows of
     * background processes to be raised above the windows of the foreground
     * process drawing all the user's attention. The proper solution is to fix
     * those broken WMs. A less proper but less ugly workaround is to replace
     * VBoxProgressDialog/QProgressDialog with an implementation that doesn't
     * try to put itself on top if it is not an active process.
     */
    if (theThing.isOk() && vboxGlobal().isVMConsoleProcess())
    {
        CSession session =
            vboxGlobal().openSession (vboxGlobal().managedVMUuid());
        session.Close();
    }

    /*
     * Compose and show the error message.
     */
    QString msgTitle =
        QApplication::tr ("VirtualBox - Error In %1").arg (pszWhere);

    char msgBuf [1024];
    vsprintf (msgBuf, pszMsgFmt, va);

    QString msgText = QApplication::tr ("%1\n\nrc=%2").arg (msgBuf).arg (rc);

    switch (enmWhat)
    {
        case kSupInitOp_Driver:
            msgText += QApplication::tr ("\n\nMake sure the kernel module has "
                                         "been loaded successfully.");
            break;
        case kSupInitOp_IPRT:
        case kSupInitOp_Integrity:
        case kSupInitOp_RootCheck:
            msgText += QApplication::tr ("\n\nIt may help to reinstall "
                                         "VirtualBox."); /* hope this isn't (C), (TM) or (R) Microsoft support ;-) */
            break;
        default:
            /* no hints here */
            break;
    }

    QMessageBox::critical (
        0,                      /* parent */
        msgTitle,               /* title */
        msgText,                /* text */
        QMessageBox::Abort,     /* button0 */
        0);                     /* button1 */

    qFatal (msgText.toAscii().constData());
}

#endif /* VBOX_WITH_HARDENING */

