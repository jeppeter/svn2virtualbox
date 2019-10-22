/* $Id$ */
/** @file
 * Shared Clipboard host service test case.
 */

/*
 * Copyright (C) 2011-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "../VBoxSharedClipboardSvc-internal.h"

#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/test.h>

extern "C" DECLCALLBACK(DECLEXPORT(int)) VBoxHGCMSvcLoad (VBOXHGCMSVCFNTABLE *ptable);

static SHCLCLIENT g_Client;
static VBOXHGCMSVCHELPERS g_Helpers = { NULL };

/** Simple call handle structure for the guest call completion callback */
struct VBOXHGCMCALLHANDLE_TYPEDEF
{
    /** Where to store the result code */
    int32_t rc;
};

/** Call completion callback for guest calls. */
static DECLCALLBACK(int) callComplete(VBOXHGCMCALLHANDLE callHandle, int32_t rc)
{
    callHandle->rc = rc;
    return VINF_SUCCESS;
}

static int setupTable(VBOXHGCMSVCFNTABLE *pTable)
{
    pTable->cbSize = sizeof(*pTable);
    pTable->u32Version = VBOX_HGCM_SVC_VERSION;
    g_Helpers.pfnCallComplete = callComplete;
    pTable->pHelpers = &g_Helpers;
    return VBoxHGCMSvcLoad(pTable);
}

static void testSetMode(void)
{
    struct VBOXHGCMSVCPARM parms[2];
    VBOXHGCMSVCFNTABLE table;
    uint32_t u32Mode;
    int rc;

    RTTestISub("Testing VBOX_SHCL_HOST_FN_SET_MODE");
    rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    /* Reset global variable which doesn't reset itself. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_OFF);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    u32Mode = ShClSvcGetMode();
    RTTESTI_CHECK_MSG(u32Mode == VBOX_SHCL_MODE_OFF, ("u32Mode=%u\n", (unsigned) u32Mode));

    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 0, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 2, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    HGCMSvcSetU64(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_HOST_TO_GUEST);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    u32Mode = ShClSvcGetMode();
    RTTESTI_CHECK_MSG(u32Mode == VBOX_SHCL_MODE_HOST_TO_GUEST, ("u32Mode=%u\n", (unsigned) u32Mode));

    HGCMSvcSetU32(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_NOT_SUPPORTED);

    u32Mode = ShClSvcGetMode();
    RTTESTI_CHECK_MSG(u32Mode == VBOX_SHCL_MODE_OFF, ("u32Mode=%u\n", (unsigned) u32Mode));
    table.pfnUnload(NULL);
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
static void testSetTransferMode(void)
{
    struct VBOXHGCMSVCPARM parms[2];
    VBOXHGCMSVCFNTABLE table;

    RTTestISub("Testing VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE");
    int rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    /* Invalid parameter. */
    HGCMSvcSetU64(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    /* Invalid mode. */
    HGCMSvcSetU32(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_FLAGS);

    /* Enable transfers. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_TRANSFER_MODE_ENABLED);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    /* Disable transfers again. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_TRANSFER_MODE_DISABLED);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

static void testMsgAddOld(PSHCLCLIENT pClient, uint32_t uMsg, uint32_t uParm1)
{
    PSHCLCLIENTMSG pMsg = shclSvcMsgAlloc(uMsg, 2 /* cParms */); /* The old protocol (v0) has a fixed parameter count of 2. */
    RTTESTI_CHECK_RETV(pMsg != NULL);

    HGCMSvcSetU32(&pMsg->paParms[0], uMsg);
    HGCMSvcSetU32(&pMsg->paParms[1], uParm1);

    int rc = shclSvcMsgAdd(pClient, pMsg, true /* fAppend */);
    RTTESTI_CHECK_RC_OK(rc);
    rc = shclSvcClientWakeup(pClient);
    RTTESTI_CHECK_RC_OK(rc);
}

/* Does testing of VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, needed for providing compatibility to older Guest Additions clients. */
static void testGetHostMsgOld(void)
{
    struct VBOXHGCMSVCPARM parms[2];
    VBOXHGCMSVCFNTABLE table;
    VBOXHGCMCALLHANDLE_TYPEDEF call;
    int rc;

    RTTestISub("Setting up VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD test");
    rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));
    /* Unless we are bidirectional the host message requests will be dropped. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_BIDIRECTIONAL);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);

    rc = shclSvcClientInit(&g_Client, 1 /* clientId */);
    RTTESTI_CHECK_RC_OK(rc);

    RTTestISub("Testing one format, waiting guest call.");
    HGCMSvcSetU32(&parms[0], 0);
    HGCMSvcSetU32(&parms[1], 0);
    call.rc = VERR_TRY_AGAIN;
    table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_TRY_AGAIN);  /* This should get updated only when the guest call completes. */
    testMsgAddOld(&g_Client, VBOX_SHCL_HOST_MSG_READ_DATA, VBOX_SHCL_FMT_UNICODETEXT);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_TRY_AGAIN;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_TRY_AGAIN);  /* This call should not complete yet. */

    RTTestISub("Testing one format, no waiting guest calls.");
    shclSvcClientReset(&g_Client);
    testMsgAddOld(&g_Client, VBOX_SHCL_HOST_MSG_READ_DATA, VBOX_SHCL_FMT_HTML);
    HGCMSvcSetU32(&parms[0], 0);
    HGCMSvcSetU32(&parms[1], 0);
    call.rc = VERR_TRY_AGAIN;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, 2, parms, 0);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_HTML);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_TRY_AGAIN;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_TRY_AGAIN);  /* This call should not complete yet. */

    RTTestISub("Testing two formats, waiting guest call.");
    shclSvcClientReset(&g_Client);
    HGCMSvcSetU32(&parms[0], 0);
    HGCMSvcSetU32(&parms[1], 0);
    call.rc = VERR_TRY_AGAIN;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_TRY_AGAIN);  /* This should get updated only when the guest call completes. */
    testMsgAddOld(&g_Client, VBOX_SHCL_HOST_MSG_READ_DATA, VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_TRY_AGAIN;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, 2, parms, 0);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_HTML);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_TRY_AGAIN;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_TRY_AGAIN);  /* This call should not complete yet. */

    RTTestISub("Testing two formats, no waiting guest calls.");
    shclSvcClientReset(&g_Client);
    testMsgAddOld(&g_Client, VBOX_SHCL_HOST_MSG_READ_DATA, VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML);
    HGCMSvcSetU32(&parms[0], 0);
    HGCMSvcSetU32(&parms[1], 0);
    call.rc = VERR_TRY_AGAIN;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, 2, parms, 0);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_TRY_AGAIN;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, 2, parms, 0);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_HTML);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_TRY_AGAIN;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_TRY_AGAIN);  /* This call should not complete yet. */
    table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    table.pfnUnload(NULL);
}

static void testSetHeadless(void)
{
    struct VBOXHGCMSVCPARM parms[2];
    VBOXHGCMSVCFNTABLE table;
    bool fHeadless;
    int rc;

    RTTestISub("Testing HOST_FN_SET_HEADLESS");
    rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));
    /* Reset global variable which doesn't reset itself. */
    HGCMSvcSetU32(&parms[0], false);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    fHeadless = ShClSvcGetHeadless();
    RTTESTI_CHECK_MSG(fHeadless == false, ("fHeadless=%RTbool\n", fHeadless));
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           0, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           2, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    HGCMSvcSetU64(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    HGCMSvcSetU32(&parms[0], true);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    fHeadless = ShClSvcGetHeadless();
    RTTESTI_CHECK_MSG(fHeadless == true, ("fHeadless=%RTbool\n", fHeadless));
    HGCMSvcSetU32(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    fHeadless = ShClSvcGetHeadless();
    RTTESTI_CHECK_MSG(fHeadless == true, ("fHeadless=%RTbool\n", fHeadless));
    table.pfnUnload(NULL);
}

static void testHostCall(void)
{
    testSetMode();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    testSetTransferMode();
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
    testSetHeadless();
}

int main(int argc, char *argv[])
{
    /*
     * Init the runtime, test and say hello.
     */
    const char *pcszExecName;
    NOREF(argc);
    pcszExecName = strrchr(argv[0], '/');
    pcszExecName = pcszExecName ? pcszExecName + 1 : argv[0];
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate(pcszExecName, &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /*
     * Run the tests.
     */
    testHostCall();
    testGetHostMsgOld();

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}

int ShClSvcImplInit() { return VINF_SUCCESS; }
void ShClSvcImplDestroy() { }
int ShClSvcImplDisconnect(PSHCLCLIENT) { return VINF_SUCCESS; }
int ShClSvcImplConnect(PSHCLCLIENT, bool) { return VINF_SUCCESS; }
int ShClSvcImplFormatAnnounce(PSHCLCLIENT, PSHCLCLIENTCMDCTX, PSHCLFORMATDATA) { AssertFailed(); return VINF_SUCCESS; }
int ShClSvcImplReadData(PSHCLCLIENT, PSHCLCLIENTCMDCTX, PSHCLDATABLOCK, unsigned int *) { AssertFailed(); return VERR_WRONG_ORDER; }
int ShClSvcImplWriteData(PSHCLCLIENT, PSHCLCLIENTCMDCTX, PSHCLDATABLOCK) { AssertFailed(); return VINF_SUCCESS; }
int ShClSvcImplSync(PSHCLCLIENT) { AssertFailed(); return VERR_WRONG_ORDER; }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
int ShClSvcImplTransferCreate(PSHCLCLIENT, PSHCLTRANSFER) { return VINF_SUCCESS; }
int ShClSvcImplTransferDestroy(PSHCLCLIENT, PSHCLTRANSFER) { return VINF_SUCCESS; }
int ShClSvcImplTransferGetRoots(PSHCLCLIENT, PSHCLTRANSFER) { return VINF_SUCCESS; }
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

