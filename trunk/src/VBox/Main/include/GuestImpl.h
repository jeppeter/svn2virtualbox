/** @file
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ____H_GUESTIMPL
#define ____H_GUESTIMPL

#include "VirtualBoxBase.h"
#include <iprt/list.h>
#include <iprt/time.h>
#include <VBox/ostypes.h>

#include "AdditionsFacilityImpl.h"
#include "GuestCtrlImplPrivate.h"
#include "GuestSessionImpl.h"
#include "HGCM.h"
#ifdef VBOX_WITH_GUEST_CONTROL
# include <iprt/fs.h>
# include <VBox/HostServices/GuestControlSvc.h>
using namespace guestControl;
#endif

#ifdef VBOX_WITH_DRAG_AND_DROP
class GuestDnD;
#endif

typedef enum
{
    GUESTSTATTYPE_CPUUSER     = 0,
    GUESTSTATTYPE_CPUKERNEL   = 1,
    GUESTSTATTYPE_CPUIDLE     = 2,
    GUESTSTATTYPE_MEMTOTAL    = 3,
    GUESTSTATTYPE_MEMFREE     = 4,
    GUESTSTATTYPE_MEMBALLOON  = 5,
    GUESTSTATTYPE_MEMCACHE    = 6,
    GUESTSTATTYPE_PAGETOTAL   = 7,
    GUESTSTATTYPE_PAGEFREE    = 8,
    GUESTSTATTYPE_MAX         = 9
} GUESTSTATTYPE;

class Console;
#ifdef VBOX_WITH_GUEST_CONTROL
class Progress;
#endif

class ATL_NO_VTABLE Guest :
    public VirtualBoxBase,
    VBOX_SCRIPTABLE_IMPL(IGuest)
{
public:
    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(Guest, IGuest)

    DECLARE_NOT_AGGREGATABLE(Guest)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(Guest)
        VBOX_DEFAULT_INTERFACE_ENTRIES(IGuest)
    END_COM_MAP()

    DECLARE_EMPTY_CTOR_DTOR (Guest)

    HRESULT FinalConstruct();
    void FinalRelease();

    // Public initializer/uninitializer for internal purposes only
    HRESULT init (Console *aParent);
    void uninit();

    // IGuest properties
    STDMETHOD(COMGETTER(OSTypeId)) (BSTR *aOSTypeId);
    STDMETHOD(COMGETTER(AdditionsRunLevel)) (AdditionsRunLevelType_T *aRunLevel);
    STDMETHOD(COMGETTER(AdditionsVersion))(BSTR *a_pbstrAdditionsVersion);
    STDMETHOD(COMGETTER(AdditionsRevision))(ULONG *a_puAdditionsRevision);
    STDMETHOD(COMGETTER(Facilities)) (ComSafeArrayOut(IAdditionsFacility *, aFacilities));
    STDMETHOD(COMGETTER(Sessions)) (ComSafeArrayOut(IGuestSession *, aSessions));
    STDMETHOD(COMGETTER(MemoryBalloonSize)) (ULONG *aMemoryBalloonSize);
    STDMETHOD(COMSETTER(MemoryBalloonSize)) (ULONG aMemoryBalloonSize);
    STDMETHOD(COMGETTER(StatisticsUpdateInterval)) (ULONG *aUpdateInterval);
    STDMETHOD(COMSETTER(StatisticsUpdateInterval)) (ULONG aUpdateInterval);

    // IGuest methods
    STDMETHOD(GetFacilityStatus)(AdditionsFacilityType_T aType, LONG64 *aTimestamp, AdditionsFacilityStatus_T *aStatus);
    STDMETHOD(GetAdditionsStatus)(AdditionsRunLevelType_T aLevel, BOOL *aActive);
    STDMETHOD(SetCredentials)(IN_BSTR aUsername, IN_BSTR aPassword,
                              IN_BSTR aDomain, BOOL aAllowInteractiveLogon);
    STDMETHOD(DragHGEnter)(ULONG uScreenId, ULONG uX, ULONG uY, DragAndDropAction_T defaultAction, ComSafeArrayIn(DragAndDropAction_T, allowedActions), ComSafeArrayIn(IN_BSTR, formats), DragAndDropAction_T *pResultAction);
    STDMETHOD(DragHGMove)(ULONG uScreenId, ULONG uX, ULONG uY, DragAndDropAction_T defaultAction, ComSafeArrayIn(DragAndDropAction_T, allowedActions), ComSafeArrayIn(IN_BSTR, formats), DragAndDropAction_T *pResultAction);
    STDMETHOD(DragHGLeave)(ULONG uScreenId);
    STDMETHOD(DragHGDrop)(ULONG uScreenId, ULONG uX, ULONG uY, DragAndDropAction_T defaultAction, ComSafeArrayIn(DragAndDropAction_T, allowedActions), ComSafeArrayIn(IN_BSTR, formats), BSTR *pstrFormat, DragAndDropAction_T *pResultAction);
    STDMETHOD(DragHGPutData)(ULONG uScreenId, IN_BSTR strFormat, ComSafeArrayIn(BYTE, data), IProgress **ppProgress);
    STDMETHOD(DragGHPending)(ULONG uScreenId, ComSafeArrayOut(BSTR, formats), ComSafeArrayOut(DragAndDropAction_T, allowedActions), DragAndDropAction_T *pDefaultAction);
    STDMETHOD(DragGHDropped)(IN_BSTR strFormat, DragAndDropAction_T action, IProgress **ppProgress);
    STDMETHOD(DragGHGetData)(ComSafeArrayOut(BYTE, data));
    // Process execution
    STDMETHOD(ExecuteProcess)(IN_BSTR aCommand, ULONG aFlags,
                              ComSafeArrayIn(IN_BSTR, aArguments), ComSafeArrayIn(IN_BSTR, aEnvironment),
                              IN_BSTR aUsername, IN_BSTR aPassword,
                              ULONG aTimeoutMS, ULONG *aPID, IProgress **aProgress);
    STDMETHOD(GetProcessOutput)(ULONG aPID, ULONG aFlags, ULONG aTimeoutMS, LONG64 aSize, ComSafeArrayOut(BYTE, aData));
    STDMETHOD(SetProcessInput)(ULONG aPID, ULONG aFlags, ULONG aTimeoutMS, ComSafeArrayIn(BYTE, aData), ULONG *aBytesWritten);
    STDMETHOD(GetProcessStatus)(ULONG aPID, ULONG *aExitCode, ULONG *aFlags, ExecuteProcessStatus_T *aStatus);
    // File copying
    STDMETHOD(CopyFromGuest)(IN_BSTR aSource, IN_BSTR aDest, IN_BSTR aUsername, IN_BSTR aPassword, ULONG aFlags, IProgress **aProgress);
    STDMETHOD(CopyToGuest)(IN_BSTR aSource, IN_BSTR aDest, IN_BSTR aUsername, IN_BSTR aPassword, ULONG aFlags, IProgress **aProgress);
    // Directory handling
    STDMETHOD(DirectoryClose)(ULONG aHandle);
    STDMETHOD(DirectoryCreate)(IN_BSTR aDirectory, IN_BSTR aUsername, IN_BSTR aPassword, ULONG aMode, ULONG aFlags);
#if 0
    STDMETHOD(DirectoryExists)(IN_BSTR aDirectory, IN_BSTR aUsername, IN_BSTR aPassword, BOOL *aExists);
#endif
    STDMETHOD(DirectoryOpen)(IN_BSTR aDirectory, IN_BSTR aFilter,
                             ULONG aFlags, IN_BSTR aUsername, IN_BSTR aPassword, ULONG *aHandle);
    STDMETHOD(DirectoryRead)(ULONG aHandle, IGuestDirEntry **aDirEntry);
    // File handling
    STDMETHOD(FileExists)(IN_BSTR aFile, IN_BSTR aUsername, IN_BSTR aPassword, BOOL *aExists);
    STDMETHOD(FileQuerySize)(IN_BSTR aFile, IN_BSTR aUsername, IN_BSTR aPassword, LONG64 *aSize);
    // Misc stuff
    STDMETHOD(InternalGetStatistics)(ULONG *aCpuUser, ULONG *aCpuKernel, ULONG *aCpuIdle,
                                     ULONG *aMemTotal, ULONG *aMemFree, ULONG *aMemBalloon, ULONG *aMemShared, ULONG *aMemCache,
                                     ULONG *aPageTotal, ULONG *aMemAllocTotal, ULONG *aMemFreeTotal, ULONG *aMemBalloonTotal, ULONG *aMemSharedTotal);
    STDMETHOD(UpdateGuestAdditions)(IN_BSTR aSource, ComSafeArrayIn(AdditionsUpdateFlag_T, aFlags), IProgress **aProgress);
    STDMETHOD(CreateSession)(IN_BSTR aUser, IN_BSTR aPassword, IN_BSTR aDomain, IN_BSTR aSessionName, IGuestSession **aGuestSession);
    STDMETHOD(FindSession)(IN_BSTR aSessionName, ComSafeArrayOut(IGuestSession *, aSessions));

    // Public methods that are not in IDL (only called internally).
    void setAdditionsInfo(Bstr aInterfaceVersion, VBOXOSTYPE aOsType);
    void setAdditionsInfo2(uint32_t a_uFullVersion, const char *a_pszName, uint32_t a_uRevision, uint32_t a_fFeatures);
    bool facilityIsActive(VBoxGuestFacilityType enmFacility);
    void facilityUpdate(VBoxGuestFacilityType a_enmFacility, VBoxGuestFacilityStatus a_enmStatus, uint32_t a_fFlags, PCRTTIMESPEC a_pTimeSpecTS);
    void setAdditionsStatus(VBoxGuestFacilityType a_enmFacility, VBoxGuestFacilityStatus a_enmStatus, uint32_t a_fFlags, PCRTTIMESPEC a_pTimeSpecTS);
    void setSupportedFeatures(uint32_t aCaps);
    HRESULT setStatistic(ULONG aCpuId, GUESTSTATTYPE enmType, ULONG aVal);
    BOOL isPageFusionEnabled();
    static HRESULT setErrorStatic(HRESULT aResultCode,
                                  const Utf8Str &aText)
    {
        return setErrorInternal(aResultCode, getStaticClassIID(), getStaticComponentName(), aText, false, true);
    }

# ifdef VBOX_WITH_GUEST_CONTROL
    // Internal guest directory functions
    int directoryCreateHandle(ULONG *puHandle, ULONG uPID, IN_BSTR aDirectory, IN_BSTR aFilter, ULONG uFlags);
    HRESULT directoryCreateInternal(IN_BSTR aDirectory, IN_BSTR aUsername, IN_BSTR aPassword,
                                    ULONG aMode, ULONG aFlags, int *pRC);
    void directoryDestroyHandle(uint32_t uHandle);
    HRESULT directoryExistsInternal(IN_BSTR aDirectory, IN_BSTR aUsername, IN_BSTR aPassword, BOOL *aExists);
    uint32_t directoryGetPID(uint32_t uHandle);
    int directoryGetNextEntry(uint32_t uHandle, GuestProcessStreamBlock &streamBlock);
    bool directoryHandleExists(uint32_t uHandle);
    HRESULT directoryOpenInternal(IN_BSTR aDirectory, IN_BSTR aFilter,
                                  ULONG aFlags,
                                  IN_BSTR aUsername, IN_BSTR aPassword,
                                  ULONG *aHandle, int *pRC);
    HRESULT directoryQueryInfoInternal(IN_BSTR aDirectory, IN_BSTR aUsername, IN_BSTR aPassword, PRTFSOBJINFO aObjInfo, RTFSOBJATTRADD enmAddAttribs, int *pRC);
    // Internal guest execution functions
    HRESULT executeAndWaitForTool(IN_BSTR aTool, IN_BSTR aDescription,
                                  ComSafeArrayIn(IN_BSTR, aArguments), ComSafeArrayIn(IN_BSTR, aEnvironment),
                                  IN_BSTR aUsername, IN_BSTR aPassword,
                                  ULONG uFlagsToAdd,
                                  GuestCtrlStreamObjects *pObjStdOut, GuestCtrlStreamObjects *pObjStdErr,
                                  IProgress **aProgress, ULONG *aPID);
    HRESULT executeProcessInternal(IN_BSTR aCommand, ULONG aFlags,
                                   ComSafeArrayIn(IN_BSTR, aArguments), ComSafeArrayIn(IN_BSTR, aEnvironment),
                                   IN_BSTR aUsername, IN_BSTR aPassword,
                                   ULONG aTimeoutMS, ULONG *aPID, IProgress **aProgress, int *pRC);
    HRESULT getProcessOutputInternal(ULONG aPID, ULONG aFlags, ULONG aTimeoutMS,
                                     LONG64 aSize, ComSafeArrayOut(BYTE, aData), int *pRC);
    HRESULT executeSetResult(const char *pszCommand, const char *pszUser, ULONG ulTimeout, PCALLBACKDATAEXECSTATUS pExecStatus, ULONG *puPID);
    int     executeStreamQueryFsObjInfo(IN_BSTR aObjName,GuestProcessStreamBlock &streamBlock, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttribs);
    int     executeStreamDrain(ULONG aPID, ULONG ulFlags, GuestProcessStream *pStream);
    int     executeStreamGetNextBlock(ULONG ulPID, ULONG ulFlags, GuestProcessStream &stream, GuestProcessStreamBlock &streamBlock);
    int     executeStreamParseNextBlock(ULONG ulPID, ULONG ulFlags, GuestProcessStream &stream, GuestProcessStreamBlock &streamBlock);
    HRESULT executeStreamParse(ULONG uPID, ULONG ulFlags, GuestCtrlStreamObjects &streamObjects);
    HRESULT executeWaitForExit(ULONG uPID, ComPtr<IProgress> pProgress, ULONG uTimeoutMS);
    // Internal guest file functions
    HRESULT fileExistsInternal(IN_BSTR aFile, IN_BSTR aUsername, IN_BSTR aPassword, BOOL *aExists);
    HRESULT fileQueryInfoInternal(IN_BSTR aFile, IN_BSTR aUsername, IN_BSTR aPassword, PRTFSOBJINFO aObjInfo, RTFSOBJATTRADD enmAddAttribs, int *pRC);
    HRESULT fileQuerySizeInternal(IN_BSTR aFile, IN_BSTR aUsername, IN_BSTR aPassword, LONG64 *aSize);

    // Guest control dispatcher.
    /** Static callback for handling guest control notifications. */
    static DECLCALLBACK(int) notifyCtrlDispatcher(void *pvExtension, uint32_t u32Function, void *pvParms, uint32_t cbParms);

    // Internal tasks.
    //extern struct GuestTask;
    HRESULT taskCopyFileToGuest(GuestTask *aTask);
    HRESULT taskCopyFileFromGuest(GuestTask *aTask);
    HRESULT taskUpdateGuestAdditions(GuestTask *aTask);
#endif
    void enableVMMStatistics(BOOL aEnable) { mCollectVMMStats = aEnable; };

public:
    /** @name Public internal methods.
     * @{ */
    int         dispatchToSession(uint32_t uContextID, uint32_t uFunction, void *pvData, size_t cbData);
    uint32_t    getAdditionsVersion(void) { return mData.mAdditionsVersionFull; }
    Console    *getConsole(void) { return mParent; }
    int         sessionClose(ComObjPtr<GuestSession> pSession);
    int         sessionCreate(const Utf8Str &strUser, const Utf8Str &strPassword, const Utf8Str &strDomain,
                              const Utf8Str &strSessionName, ComObjPtr<GuestSession> &pGuestSession);
    inline bool sessionExists(uint32_t uSessionID);
    /** @}  */

private:

#ifdef VBOX_WITH_GUEST_CONTROL
    // Internal guest callback representation.
    typedef struct VBOXGUESTCTRL_CALLBACK
    {
        eVBoxGuestCtrlCallbackType  mType;
        /** Pointer to user-supplied data. */
        void                       *pvData;
        /** Size of user-supplied data. */
        uint32_t                    cbData;
        /** The host PID. Needed for translating to
         *  a guest PID. */
        uint32_t                    uHostPID;
        /** Pointer to user-supplied IProgress. */
        ComObjPtr<Progress>         pProgress;
    } VBOXGUESTCTRL_CALLBACK, *PVBOXGUESTCTRL_CALLBACK;
    typedef std::map< uint32_t, VBOXGUESTCTRL_CALLBACK > CallbackMap;
    typedef std::map< uint32_t, VBOXGUESTCTRL_CALLBACK >::iterator CallbackMapIter;
    typedef std::map< uint32_t, VBOXGUESTCTRL_CALLBACK >::const_iterator CallbackMapIterConst;

    int callbackAdd(const PVBOXGUESTCTRL_CALLBACK pCallbackData, uint32_t *puContextID);
    int callbackAssignHostPID(uint32_t uContextID, uint32_t uHostPID);
    void callbackDestroy(uint32_t uContextID);
    void callbackRemove(uint32_t uContextID);
    bool callbackExists(uint32_t uContextID);
    void callbackFreeUserData(void *pvData);
    uint32_t callbackGetHostPID(uint32_t uContextID);
    int callbackGetUserData(uint32_t uContextID, eVBoxGuestCtrlCallbackType *pEnmType, void **ppvData, size_t *pcbData);
    void* callbackGetUserDataMutableRaw(uint32_t uContextID, size_t *pcbData);
    int callbackInit(PVBOXGUESTCTRL_CALLBACK pCallback, eVBoxGuestCtrlCallbackType enmType, ComPtr<Progress> pProgress);
    bool callbackIsCanceled(uint32_t uContextID);
    bool callbackIsComplete(uint32_t uContextID);
    int callbackMoveForward(uint32_t uContextID, const char *pszMessage);
    int callbackNotifyEx(uint32_t uContextID, int iRC, const char *pszMessage);
    int callbackNotifyComplete(uint32_t uContextID);
    int callbackNotifyAllForPID(uint32_t uGuestPID, int iRC, const char *pszMessage);
    int callbackWaitForCompletion(uint32_t uContextID, LONG lStage, LONG lTimeout);

    int notifyCtrlClientDisconnected(uint32_t u32Function, PCALLBACKDATACLIENTDISCONNECTED pData);
    int notifyCtrlExecStatus(uint32_t u32Function, PCALLBACKDATAEXECSTATUS pData);
    int notifyCtrlExecOut(uint32_t u32Function, PCALLBACKDATAEXECOUT pData);
    int notifyCtrlExecInStatus(uint32_t u32Function, PCALLBACKDATAEXECINSTATUS pData);

    // Internal guest process representation.
    typedef struct VBOXGUESTCTRL_PROCESS
    {
        /** The guest PID -- needed for controlling the actual guest
         *  process which has its own PID (generated by the guest OS). */
        uint32_t                    mGuestPID;
        /** The last reported process status. */
        ExecuteProcessStatus_T      mStatus;
        /** The process execution flags. */
        uint32_t                    mFlags;
        /** The process' exit code. */
        uint32_t                    mExitCode;
    } VBOXGUESTCTRL_PROCESS, *PVBOXGUESTCTRL_PROCESS;
    typedef std::map< uint32_t, VBOXGUESTCTRL_PROCESS > GuestProcessMap;
    typedef std::map< uint32_t, VBOXGUESTCTRL_PROCESS >::iterator GuestProcessMapIter;
    typedef std::map< uint32_t, VBOXGUESTCTRL_PROCESS >::const_iterator GuestProcessMapIterConst;

    uint32_t processGetGuestPID(uint32_t uHostPID);
    int  processGetStatus(uint32_t uHostPID, PVBOXGUESTCTRL_PROCESS pProcess, bool fRemove);
    int  processSetStatus(uint32_t uHostPID, uint32_t uGuestPID,
                          ExecuteProcessStatus_T enmStatus, uint32_t uExitCode, uint32_t uFlags);

    // Internal guest directory representation.
    typedef struct VBOXGUESTCTRL_DIRECTORY
    {
        Bstr                        mDirectory;
        Bstr                        mFilter;
        ULONG                       mFlags;
        /** Associated host PID of started vbox_ls tool. */
        ULONG                       mPID;
        GuestProcessStream          mStream;
    } VBOXGUESTCTRL_DIRECTORY, *PVBOXGUESTCTRL_DIRECTORY;
    typedef std::map< uint32_t, VBOXGUESTCTRL_DIRECTORY > GuestDirectoryMap;
    typedef std::map< uint32_t, VBOXGUESTCTRL_DIRECTORY >::iterator GuestDirectoryMapIter;
    typedef std::map< uint32_t, VBOXGUESTCTRL_DIRECTORY >::const_iterator GuestDirectoryMapIterConst;

    // Utility functions.
    int prepareExecuteEnv(const char *pszEnv, void **ppvList, uint32_t *pcbList, uint32_t *pcEnv);

    /*
     * Handler for guest execution control notifications.
     */
    HRESULT setErrorCompletion(int rc);
    HRESULT setErrorFromProgress(ComPtr<IProgress> pProgress);
    HRESULT setErrorHGCM(int rc);
# endif

    typedef std::map< AdditionsFacilityType_T, ComObjPtr<AdditionsFacility> > FacilityMap;
    typedef std::map< AdditionsFacilityType_T, ComObjPtr<AdditionsFacility> >::iterator FacilityMapIter;
    typedef std::map< AdditionsFacilityType_T, ComObjPtr<AdditionsFacility> >::const_iterator FacilityMapIterConst;

    /** Map for keeping the guest sessions. The primary key marks the guest session ID. */
    typedef std::map <uint32_t, ComObjPtr<GuestSession> > GuestSessions;

    struct Data
    {
        Data() : mAdditionsRunLevel(AdditionsRunLevelType_None)
            , mAdditionsVersionFull(0), mAdditionsRevision(0), mAdditionsFeatures(0)
        { }

        Bstr                    mOSTypeId;
        FacilityMap             mFacilityMap;
        AdditionsRunLevelType_T mAdditionsRunLevel;
        uint32_t                mAdditionsVersionFull;
        Bstr                    mAdditionsVersionNew;
        uint32_t                mAdditionsRevision;
        uint32_t                mAdditionsFeatures;
        Bstr                    mInterfaceVersion;
        GuestSessions           mGuestSessions;
        uint32_t                mNextSessionID;
    };

    ULONG mMemoryBalloonSize;
    ULONG mStatUpdateInterval;
    ULONG mCurrentGuestStat[GUESTSTATTYPE_MAX];
    ULONG mGuestValidStats;
    BOOL  mCollectVMMStats;
    BOOL  mfPageFusionEnabled;

    Console *mParent;
    Data mData;

# ifdef VBOX_WITH_GUEST_CONTROL
    /** General extension callback for guest control. */
    HGCMSVCEXTHANDLE  mhExtCtrl;
    /** Next upcoming context ID. */
    volatile uint32_t mNextContextID;
    /** Next upcoming host PID */
    volatile uint32_t mNextHostPID;
    /** Next upcoming directory handle ID. */
    volatile uint32_t mNextDirectoryID;
    CallbackMap       mCallbackMap;
    GuestDirectoryMap mGuestDirectoryMap;
    GuestProcessMap   mGuestProcessMap;
# endif

#ifdef VBOX_WITH_DRAG_AND_DROP
    GuestDnD         *m_pGuestDnD;
    friend class GuestDnD;
    friend class GuestDnDPrivate;
#endif
    static void staticUpdateStats(RTTIMERLR hTimerLR, void *pvUser, uint64_t iTick);
    void updateStats(uint64_t iTick);
    RTTIMERLR         mStatTimer;
    uint32_t          mMagic;
};
#define GUEST_MAGIC 0xCEED2006u

#endif // ____H_GUESTIMPL

