/* $Id$ */
/** @file
 * Generic FTP server (RFC 959) implementation.
 * Partly also implements RFC 3659 (Extensions to FTP, for "SIZE", ++).
 */

/*
 * Copyright (C) 2020 Oracle Corporation
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

/**
 * Known limitations so far:
 * - UTF-8 support only.
 * - Only supports ASCII + binary (image type) file streams for now.
 * - No support for writing / modifying ("DELE", "MKD", "RMD", "STOR", ++).
 * - No FTPS / SFTP support.
 * - No passive mode ("PASV") support.
 * - No IPv6 support.
 * - No proxy support.
 * - No FXP support.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_FTP
#include <iprt/ftp.h>
#include "internal/iprt.h"
#include "internal/magics.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h> /* For file mode flags. */
#include <iprt/getopt.h>
#include <iprt/mem.h>
#include <iprt/log.h>
#include <iprt/path.h>
#include <iprt/poll.h>
#include <iprt/socket.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/tcp.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Internal FTP server instance.
 */
typedef struct RTFTPSERVERINTERNAL
{
    /** Magic value. */
    uint32_t                u32Magic;
    /** Callback table. */
    RTFTPSERVERCALLBACKS    Callbacks;
    /** Pointer to TCP server instance. */
    PRTTCPSERVER            pTCPServer;
    /** Number of currently connected clients. */
    uint32_t                cClients;
    /** Pointer to user-specific data. Optional. */
    void                   *pvUser;
    /** Size of user-specific data. Optional. */
    size_t                  cbUser;
} RTFTPSERVERINTERNAL;
/** Pointer to an internal FTP server instance. */
typedef RTFTPSERVERINTERNAL *PRTFTPSERVERINTERNAL;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTFTPSERVER_VALID_RETURN_RC(hFTPServer, a_rc) \
    do { \
        AssertPtrReturn((hFTPServer), (a_rc)); \
        AssertReturn((hFTPServer)->u32Magic == RTFTPSERVER_MAGIC, (a_rc)); \
    } while (0)

/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTFTPSERVER_VALID_RETURN(hFTPServer) RTFTPSERVER_VALID_RETURN_RC((hFTPServer), VERR_INVALID_HANDLE)

/** Validates a handle and returns (void) if not valid. */
#define RTFTPSERVER_VALID_RETURN_VOID(hFTPServer) \
    do { \
        AssertPtrReturnVoid(hFTPServer); \
        AssertReturnVoid((hFTPServer)->u32Magic == RTFTPSERVER_MAGIC); \
    } while (0)

/** Supported FTP server command IDs.
 *  Alphabetically, named after their official command names. */
typedef enum RTFTPSERVER_CMD
{
    /** Invalid command, do not use. Always must come first. */
    RTFTPSERVER_CMD_INVALID = 0,
    /** Aborts the current command on the server. */
    RTFTPSERVER_CMD_ABOR,
    /** Changes the current working directory. */
    RTFTPSERVER_CMD_CDUP,
    /** Changes the current working directory. */
    RTFTPSERVER_CMD_CWD,
    /** Reports features supported by the server. */
    RTFTPSERVER_CMD_FEAT,
    /** Lists a directory. */
    RTFTPSERVER_CMD_LIST,
    /** Sets the transfer mode. */
    RTFTPSERVER_CMD_MODE,
    /** Sends a nop ("no operation") to the server. */
    RTFTPSERVER_CMD_NOOP,
    /** Sets the password for authentication. */
    RTFTPSERVER_CMD_PASS,
    /** Sets the port to use for the data connection. */
    RTFTPSERVER_CMD_PORT,
    /** Gets the current working directory. */
    RTFTPSERVER_CMD_PWD,
    /** Terminates the session (connection). */
    RTFTPSERVER_CMD_QUIT,
    /** Retrieves a specific file. */
    RTFTPSERVER_CMD_RETR,
    /** Retrieves the size of a file. */
    RTFTPSERVER_CMD_SIZE,
    /** Retrieves the current status of a transfer. */
    RTFTPSERVER_CMD_STAT,
    /** Sets the structure type to use. */
    RTFTPSERVER_CMD_STRU,
    /** Gets the server's OS info. */
    RTFTPSERVER_CMD_SYST,
    /** Sets the (data) representation type. */
    RTFTPSERVER_CMD_TYPE,
    /** Sets the user name for authentication. */
    RTFTPSERVER_CMD_USER,
    /** End marker. */
    RTFTPSERVER_CMD_LAST,
    /** The usual 32-bit hack. */
    RTFTPSERVER_CMD_32BIT_HACK = 0x7fffffff
} RTFTPSERVER_CMD;

struct RTFTPSERVERCLIENT;

/**
 * Structure for maintaining a single data connection.
 */
typedef struct RTFTPSERVERDATACONN
{
    /** Pointer to associated client of this data connection. */
    RTFTPSERVERCLIENT          *pClient;
    /** Data connection IP. */
    RTNETADDRIPV4               Addr;
    /** Data connection port number. */
    uint16_t                    uPort;
    /** The current data socket to use.
     *  Can be NIL_RTSOCKET if no data port has been specified (yet) or has been closed. */
    RTSOCKET                    hSocket;
    /** Thread serving the data connection. */
    RTTHREAD                    hThread;
    /** Thread started indicator. */
    volatile bool               fStarted;
    /** Thread stop indicator. */
    volatile bool               fStop;
    /** Thread stopped indicator. */
    volatile bool               fStopped;
    /** Overall result when closing the data connection. */
    int                         rc;
    /** Number of command arguments. */
    uint8_t                     cArgs;
    /** Command arguments array. Optional and can be NULL.
     *  Will be free'd by the data connection thread. */
    char**                      papszArgs;
} RTFTPSERVERDATACONN;
/** Pointer to a data connection struct. */
typedef RTFTPSERVERDATACONN *PRTFTPSERVERDATACONN;

/**
 * Structure for maintaining an internal FTP server client.
 */
typedef struct RTFTPSERVERCLIENT
{
    /** Pointer to internal server state. */
    PRTFTPSERVERINTERNAL        pServer;
    /** Socket handle the client is bound to. */
    RTSOCKET                    hSocket;
    /** Actual client state. */
    RTFTPSERVERCLIENTSTATE      State;
    /** Data connection information.
     *  At the moment we only allow one data connection per client at a time. */
    PRTFTPSERVERDATACONN        pDataConn;
} RTFTPSERVERCLIENT;
/** Pointer to an internal FTP server client state. */
typedef RTFTPSERVERCLIENT *PRTFTPSERVERCLIENT;

/** Function pointer declaration for a specific FTP server command handler. */
typedef DECLCALLBACK(int) FNRTFTPSERVERCMD(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs);
/** Pointer to a FNRTFTPSERVERCMD(). */
typedef FNRTFTPSERVERCMD *PFNRTFTPSERVERCMD;

/** Handles a FTP server callback with no arguments and returns. */
#define RTFTPSERVER_HANDLE_CALLBACK_RET(a_Name) \
    do \
    { \
        PRTFTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTFTPCALLBACKDATA Data = { &pClient->State }; \
            return pCallbacks->a_Name(&Data); \
        } \
        else \
            return VERR_NOT_IMPLEMENTED; \
    } while (0)

/** Handles a FTP server callback with no arguments and sets rc accordingly. */
#define RTFTPSERVER_HANDLE_CALLBACK(a_Name) \
    do \
    { \
        PRTFTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTFTPCALLBACKDATA Data = { &pClient->State, pClient->pServer->pvUser, pClient->pServer->cbUser }; \
            rc = pCallbacks->a_Name(&Data); \
        } \
        else \
            rc = VERR_NOT_IMPLEMENTED; \
    } while (0)

/** Handles a FTP server callback with arguments and sets rc accordingly. */
#define RTFTPSERVER_HANDLE_CALLBACK_VA(a_Name, ...) \
    do \
    { \
        PRTFTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTFTPCALLBACKDATA Data = { &pClient->State, pClient->pServer->pvUser, pClient->pServer->cbUser }; \
            rc = pCallbacks->a_Name(&Data, __VA_ARGS__); \
        } \
        else \
            rc = VERR_NOT_IMPLEMENTED; \
    } while (0)

/** Handles a FTP server callback with arguments and returns. */
#define RTFTPSERVER_HANDLE_CALLBACK_VA_RET(a_Name, ...) \
    do \
    { \
        PRTFTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTFTPCALLBACKDATA Data = { &pClient->State, pClient->pServer->pvUser, pClient->pServer->cbUser }; \
            return pCallbacks->a_Name(&Data, __VA_ARGS__); \
        } \
        else \
            return VERR_NOT_IMPLEMENTED; \
    } while (0)


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

static int rtFtpServerDataConnOpen(PRTFTPSERVERDATACONN pDataConn, PRTNETADDRIPV4 pAddr, uint16_t uPort);
static void rtFtpServerDataConnReset(PRTFTPSERVERDATACONN pDataConn);
static int rtFtpServerDataConnStart(PRTFTPSERVERDATACONN pDataConn, PFNRTTHREAD pfnThread,
                                    uint8_t cArgs, const char * const *apcszArgs);
static int rtFtpServerDataConnDestroy(PRTFTPSERVERDATACONN pDataConn);

static void rtFtpServerClientStateReset(PRTFTPSERVERCLIENTSTATE pState);

/**
 * Function prototypes for command handlers.
 */
static FNRTFTPSERVERCMD rtFtpServerHandleABOR;
static FNRTFTPSERVERCMD rtFtpServerHandleCDUP;
static FNRTFTPSERVERCMD rtFtpServerHandleCWD;
static FNRTFTPSERVERCMD rtFtpServerHandleFEAT;
static FNRTFTPSERVERCMD rtFtpServerHandleLIST;
static FNRTFTPSERVERCMD rtFtpServerHandleMODE;
static FNRTFTPSERVERCMD rtFtpServerHandleNOOP;
static FNRTFTPSERVERCMD rtFtpServerHandlePASS;
static FNRTFTPSERVERCMD rtFtpServerHandlePORT;
static FNRTFTPSERVERCMD rtFtpServerHandlePWD;
static FNRTFTPSERVERCMD rtFtpServerHandleQUIT;
static FNRTFTPSERVERCMD rtFtpServerHandleRETR;
static FNRTFTPSERVERCMD rtFtpServerHandleSIZE;
static FNRTFTPSERVERCMD rtFtpServerHandleSTAT;
static FNRTFTPSERVERCMD rtFtpServerHandleSTRU;
static FNRTFTPSERVERCMD rtFtpServerHandleSYST;
static FNRTFTPSERVERCMD rtFtpServerHandleTYPE;
static FNRTFTPSERVERCMD rtFtpServerHandleUSER;

/**
 * Structure for maintaining a single command entry for the command table.
 */
typedef struct RTFTPSERVER_CMD_ENTRY
{
    /** Command ID. */
    RTFTPSERVER_CMD    enmCmd;
    /** Command represented as ASCII string. */
    char               szCmd[RTFTPSERVER_MAX_CMD_LEN];
    /** Whether the commands needs a logged in (valid) user. */
    bool               fNeedsUser;
    /** Function pointer invoked to handle the command. */
    PFNRTFTPSERVERCMD  pfnCmd;
} RTFTPSERVER_CMD_ENTRY;
/** Pointer to a command entry. */
typedef RTFTPSERVER_CMD_ENTRY *PRTFTPSERVER_CMD_ENTRY;

/**
 * Table of handled commands.
 */
const RTFTPSERVER_CMD_ENTRY g_aCmdMap[] =
{
    { RTFTPSERVER_CMD_ABOR,     "ABOR", true,  rtFtpServerHandleABOR },
    { RTFTPSERVER_CMD_CDUP,     "CDUP", true,  rtFtpServerHandleCDUP },
    { RTFTPSERVER_CMD_CWD,      "CWD",  true,  rtFtpServerHandleCWD  },
    { RTFTPSERVER_CMD_FEAT,     "FEAT", true,  rtFtpServerHandleFEAT },
    { RTFTPSERVER_CMD_LIST,     "LIST", true,  rtFtpServerHandleLIST },
    { RTFTPSERVER_CMD_MODE,     "MODE", true,  rtFtpServerHandleMODE },
    { RTFTPSERVER_CMD_NOOP,     "NOOP", true,  rtFtpServerHandleNOOP },
    { RTFTPSERVER_CMD_PASS,     "PASS", false, rtFtpServerHandlePASS },
    { RTFTPSERVER_CMD_PORT,     "PORT", true,  rtFtpServerHandlePORT },
    { RTFTPSERVER_CMD_PWD,      "PWD",  true,  rtFtpServerHandlePWD  },
    { RTFTPSERVER_CMD_QUIT,     "QUIT", false, rtFtpServerHandleQUIT },
    { RTFTPSERVER_CMD_RETR,     "RETR", true,  rtFtpServerHandleRETR },
    { RTFTPSERVER_CMD_SIZE,     "SIZE", true,  rtFtpServerHandleSIZE },
    { RTFTPSERVER_CMD_STAT,     "STAT", true,  rtFtpServerHandleSTAT },
    { RTFTPSERVER_CMD_STRU,     "STRU", true,  rtFtpServerHandleSTRU },
    { RTFTPSERVER_CMD_SYST,     "SYST", false, rtFtpServerHandleSYST },
    { RTFTPSERVER_CMD_TYPE,     "TYPE", true,  rtFtpServerHandleTYPE },
    { RTFTPSERVER_CMD_USER,     "USER", false, rtFtpServerHandleUSER },
    { RTFTPSERVER_CMD_LAST,     "",     false, NULL }
};

/** Feature string which represents all commands we support in addition to RFC 959.
 *  Must match the command table above.
 *
 *  Don't forget the terminating ";" at each feature. */
#define RTFTPSERVER_FEATURES_STRING \
    "SIZE;" /* Supports reporting file sizes. */


/*********************************************************************************************************************************
*   Protocol Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Replies a (three digit) reply code back to the client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to reply to.
 * @param   enmReply            Reply code to send.
 */
static int rtFtpServerSendReplyRc(PRTFTPSERVERCLIENT pClient, RTFTPSERVER_REPLY enmReply)
{
    /* Note: If we don't supply any additional text, make sure to include an empty stub, as
     *       some clients expect this as part of their parsing code. */
    char szReply[32];
    RTStrPrintf2(szReply, sizeof(szReply), "%RU32 -\r\n", enmReply);

    LogFlowFunc(("Sending reply code %RU32\n", enmReply));

    return RTTcpWrite(pClient->hSocket, szReply, strlen(szReply) + 1);
}

/**
 * Replies a (three digit) reply code with a custom message back to the client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to reply to.
 * @param   enmReply            Reply code to send.
 * @param   pcszFormat          Format string of message to send with the reply code.
 */
static int rtFtpServerSendReplyRcEx(PRTFTPSERVERCLIENT pClient, RTFTPSERVER_REPLY enmReply,
                                    const char *pcszFormat, ...)
{
    char *pszMsg = NULL;

    va_list args;
    va_start(args, pcszFormat);
    char *pszFmt = NULL;
    const int cch = RTStrAPrintfV(&pszFmt, pcszFormat, args);
    va_end(args);
    AssertReturn(cch > 0, VERR_NO_MEMORY);

    int rc = RTStrAPrintf(&pszMsg, "%RU32", enmReply);
    AssertRCReturn(rc, rc);

    if (pszFmt)
    {
        rc = RTStrAAppend(&pszMsg, " - ");
        AssertRCReturn(rc, rc);

        rc = RTStrAAppend(&pszMsg, pszFmt);
        AssertRCReturn(rc, rc);
    }


    rc = RTStrAAppend(&pszMsg, "\r\n");
    AssertRCReturn(rc, rc);

    RTStrFree(pszFmt);

    rc = RTTcpWrite(pClient->hSocket, pszMsg, strlen(pszMsg) + 1 /* Include termination */);

    RTStrFree(pszMsg);

    return rc;
}

/**
 * Replies a string back to the client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to reply to.
 * @param   pcszFormat          Format to reply.
 * @param   ...                 Format arguments.
 */
static int rtFtpServerSendReplyStr(PRTFTPSERVERCLIENT pClient, const char *pcszFormat, ...)
{
    va_list args;
    va_start(args, pcszFormat);
    char *psz = NULL;
    const int cch = RTStrAPrintfV(&psz, pcszFormat, args);
    va_end(args);
    AssertReturn(cch > 0, VERR_NO_MEMORY);

    int rc = RTStrAAppend(&psz, "\r\n");
    AssertRCReturn(rc, rc);

    rc = RTTcpWrite(pClient->hSocket, psz, strlen(psz) + 1 /* Include termination */);

    RTStrFree(psz);

    return rc;
}

/**
 * Looks up an user account.
 *
 * @returns VBox status code, or VERR_NOT_FOUND if user has not been found.
 * @param   pClient             Client to look up user for.
 * @param   pcszUser            User name to look up.
 */
static int rtFtpServerLookupUser(PRTFTPSERVERCLIENT pClient, const char *pcszUser)
{
    RTFTPSERVER_HANDLE_CALLBACK_VA_RET(pfnOnUserConnect, pcszUser);
}

/**
 * Handles the actual client authentication.
 *
 * @returns VBox status code, or VERR_ACCESS_DENIED if authentication failed.
 * @param   pClient             Client to authenticate.
 * @param   pcszUser            User name to authenticate with.
 * @param   pcszPassword        Password to authenticate with.
 */
static int rtFtpServerAuthenticate(PRTFTPSERVERCLIENT pClient, const char *pcszUser, const char *pcszPassword)
{
    RTFTPSERVER_HANDLE_CALLBACK_VA_RET(pfnOnUserAuthenticate, pcszUser, pcszPassword);
}

/**
 * Converts a RTFSOBJINFO struct to a string.
 *
 * @returns VBox status code.
 * @param   pObjInfo            RTFSOBJINFO object to convert.
 * @param   pszFsObjInfo        Where to store the output string.
 * @param   cbFsObjInfo         Size of the output string in bytes.
 */
static int rtFtpServerFsObjInfoToStr(PRTFSOBJINFO pObjInfo, char *pszFsObjInfo, size_t cbFsObjInfo)
{
    RTFMODE fMode = pObjInfo->Attr.fMode;
    char chFileType;
    switch (fMode & RTFS_TYPE_MASK)
    {
        case RTFS_TYPE_FIFO:        chFileType = 'f'; break;
        case RTFS_TYPE_DEV_CHAR:    chFileType = 'c'; break;
        case RTFS_TYPE_DIRECTORY:   chFileType = 'd'; break;
        case RTFS_TYPE_DEV_BLOCK:   chFileType = 'b'; break;
        case RTFS_TYPE_FILE:        chFileType = '-'; break;
        case RTFS_TYPE_SYMLINK:     chFileType = 'l'; break;
        case RTFS_TYPE_SOCKET:      chFileType = 's'; break;
        case RTFS_TYPE_WHITEOUT:    chFileType = 'w'; break;
        default:                    chFileType = '?'; break;
    }

    char szTimeBirth[RTTIME_STR_LEN];
    char szTimeChange[RTTIME_STR_LEN];
    char szTimeModification[RTTIME_STR_LEN];
    char szTimeAccess[RTTIME_STR_LEN];

#define INFO_TO_STR(a_Format, ...) \
    do \
    { \
        const ssize_t cchSize = RTStrPrintf2(szTemp, sizeof(szTemp), a_Format, __VA_ARGS__); \
        AssertReturn(cchSize > 0, VERR_BUFFER_OVERFLOW); \
        const int rc2 = RTStrCat(pszFsObjInfo, cbFsObjInfo, szTemp); \
        AssertRCReturn(rc2, rc2); \
    } while (0);

    char szTemp[32];

    INFO_TO_STR("%c", chFileType);
    INFO_TO_STR("%c%c%c",
                fMode & RTFS_UNIX_IRUSR ? 'r' : '-',
                fMode & RTFS_UNIX_IWUSR ? 'w' : '-',
                fMode & RTFS_UNIX_IXUSR ? 'x' : '-');
    INFO_TO_STR("%c%c%c",
                fMode & RTFS_UNIX_IRGRP ? 'r' : '-',
                fMode & RTFS_UNIX_IWGRP ? 'w' : '-',
                fMode & RTFS_UNIX_IXGRP ? 'x' : '-');
    INFO_TO_STR("%c%c%c",
                fMode & RTFS_UNIX_IROTH ? 'r' : '-',
                fMode & RTFS_UNIX_IWOTH ? 'w' : '-',
                fMode & RTFS_UNIX_IXOTH ? 'x' : '-');

    INFO_TO_STR( " %c%c%c%c%c%c%c%c%c%c%c%c%c%c",
                fMode & RTFS_DOS_READONLY          ? 'R' : '-',
                fMode & RTFS_DOS_HIDDEN            ? 'H' : '-',
                fMode & RTFS_DOS_SYSTEM            ? 'S' : '-',
                fMode & RTFS_DOS_DIRECTORY         ? 'D' : '-',
                fMode & RTFS_DOS_ARCHIVED          ? 'A' : '-',
                fMode & RTFS_DOS_NT_DEVICE         ? 'd' : '-',
                fMode & RTFS_DOS_NT_NORMAL         ? 'N' : '-',
                fMode & RTFS_DOS_NT_TEMPORARY      ? 'T' : '-',
                fMode & RTFS_DOS_NT_SPARSE_FILE    ? 'P' : '-',
                fMode & RTFS_DOS_NT_REPARSE_POINT  ? 'J' : '-',
                fMode & RTFS_DOS_NT_COMPRESSED     ? 'C' : '-',
                fMode & RTFS_DOS_NT_OFFLINE        ? 'O' : '-',
                fMode & RTFS_DOS_NT_NOT_CONTENT_INDEXED ? 'I' : '-',
                fMode & RTFS_DOS_NT_ENCRYPTED      ? 'E' : '-');

    INFO_TO_STR( " %d %4d %4d %10lld %10lld",
                pObjInfo->Attr.u.Unix.cHardlinks,
                pObjInfo->Attr.u.Unix.uid,
                pObjInfo->Attr.u.Unix.gid,
                pObjInfo->cbObject,
                pObjInfo->cbAllocated);

    INFO_TO_STR( " %s %s %s %s",
                RTTimeSpecToString(&pObjInfo->BirthTime,        szTimeBirth,        sizeof(szTimeBirth)),
                RTTimeSpecToString(&pObjInfo->ChangeTime,       szTimeChange,       sizeof(szTimeChange)),
                RTTimeSpecToString(&pObjInfo->ModificationTime, szTimeModification, sizeof(szTimeModification)),
                RTTimeSpecToString(&pObjInfo->AccessTime,       szTimeAccess,       sizeof(szTimeAccess)) );

#undef INFO_TO_STR

    return VINF_SUCCESS;
}

/**
 * Parses a string which consists of an IPv4 (ww,xx,yy,zz) and a port number (hi,lo), all separated by comma delimiters.
 * See RFC 959, 4.1.2.
 *
 * @returns VBox status code.
 * @param   pcszStr             String to parse.
 * @param   pAddr               Where to store the IPv4 address on success.
 * @param   puPort              Where to store the port number on success.
 */
static int rtFtpParseHostAndPort(const char *pcszStr, PRTNETADDRIPV4 pAddr, uint16_t *puPort)
{
    AssertPtrReturn(pcszStr, VERR_INVALID_POINTER);
    AssertPtrReturn(pAddr, VERR_INVALID_POINTER);
    AssertPtrReturn(puPort, VERR_INVALID_POINTER);

    char *pszNext;
    int rc;

    /* Parse IP (v4). */
    /** @todo I don't think IPv6 ever will be a thing here, or will it? */
    rc = RTStrToUInt8Ex(pcszStr, &pszNext, 10, &pAddr->au8[0]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != ',')
        return VERR_INVALID_PARAMETER;

    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &pAddr->au8[1]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != ',')
        return VERR_INVALID_PARAMETER;

    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &pAddr->au8[2]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != ',')
        return VERR_INVALID_PARAMETER;

    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &pAddr->au8[3]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != ',')
        return VERR_INVALID_PARAMETER;

    /* Parse port. */
    uint8_t uPortHi;
    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &uPortHi);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != ',')
        return VERR_INVALID_PARAMETER;
    uint8_t uPortLo;
    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &uPortLo);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;

    *puPort = RT_MAKE_U16(uPortLo, uPortHi);

    return rc;
}

/**
 * Duplicates a command argument vector.
 *
 * @returns Duplicated argument vector or NULL if failed or no arguments given. Needs to be free'd with rtFtpCmdArgsFree().
 * @param   cArgs               Number of arguments in argument vector.
 * @param   papcszArgs          Pointer to argument vector to duplicate.
 */
static char** rtFtpCmdArgsDup(uint8_t cArgs, const char * const *apcszArgs)
{
    if (!cArgs)
        return NULL;

    char **apcszArgsDup = (char **)RTMemAlloc(cArgs * sizeof(char *));
    if (!apcszArgsDup)
    {
        AssertFailed();
        return NULL;
    }

    int rc2 = VINF_SUCCESS;

    uint8_t i;
    for (i = 0; i < cArgs; i++)
    {
        apcszArgsDup[i] = RTStrDup(apcszArgs[i]);
        if (!apcszArgsDup[i])
            rc2 = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(rc2))
    {
        while (i--)
            RTStrFree(apcszArgsDup[i]);

        RTMemFree(apcszArgsDup);
        return NULL;
    }

    return apcszArgsDup;
}

/**
 * Frees a command argument vector.
 *
 * @param   cArgs               Number of arguments in argument vector.
 * @param   papcszArgs          Pointer to argument vector to free.
 */
static void rtFtpCmdArgsFree(uint8_t cArgs, char **papcszArgs)
{
    while (cArgs--)
        RTStrFree(papcszArgs[cArgs]);

    RTMemFree(papcszArgs);
}

/**
 * Opens a data connection to the client.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to open.
 * @param   pAddr               Address for the data connection.
 * @param   uPort               Port for the data connection.
 */
static int rtFtpServerDataConnOpen(PRTFTPSERVERDATACONN pDataConn, PRTNETADDRIPV4 pAddr, uint16_t uPort)
{
    LogFlowFuncEnter();

    /** @todo Implement IPv6 handling here. */
    char szAddress[32];
    const ssize_t cchAdddress = RTStrPrintf2(szAddress, sizeof(szAddress), "%RU8.%RU8.%RU8.%RU8",
                                             pAddr->au8[0], pAddr->au8[1], pAddr->au8[2], pAddr->au8[3]);
    AssertReturn(cchAdddress > 0, VERR_NO_MEMORY);

    return RTTcpClientConnect(szAddress, uPort, &pDataConn->hSocket);
}

/**
 * Closes a data connection to the client.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to close.
 */
static int rtFtpServerDataConnClose(PRTFTPSERVERDATACONN pDataConn)
{
    int rc = VINF_SUCCESS;

    if (pDataConn->hSocket != NIL_RTSOCKET)
    {
        LogFlowFuncEnter();

        rc = RTTcpFlush(pDataConn->hSocket);
        if (RT_SUCCESS(rc))
        {
            rc = RTTcpClientClose(pDataConn->hSocket);
            pDataConn->hSocket = NIL_RTSOCKET;
        }
    }

    return rc;
}

/**
 * Writes data to the data connection.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to write to.
 * @param   pvData              Data to write.
 * @param   cbData              Size (in bytes) of data to write.
 * @param   pcbWritten          How many bytes were written. Optional and unused atm.
 */
static int rtFtpServerDataConnWrite(PRTFTPSERVERDATACONN pDataConn, const void *pvData, size_t cbData, size_t *pcbWritten)
{
    RT_NOREF(pcbWritten);

    return RTTcpWrite(pDataConn->hSocket, pvData, cbData);
}

/**
 * Data connection thread for writing (sending) a file to the client.
 *
 * @returns VBox status code.
 * @param   ThreadSelf          Thread handle. Unused at the moment.
 * @param   pvUser              Pointer to user-provided data. Of type PRTFTPSERVERCLIENT.
 */
static DECLCALLBACK(int) rtFtpServerDataConnFileWriteThread(RTTHREAD ThreadSelf, void *pvUser)
{
    RT_NOREF(ThreadSelf);

    PRTFTPSERVERCLIENT pClient = (PRTFTPSERVERCLIENT)pvUser;
    AssertPtr(pClient);

    PRTFTPSERVERDATACONN pDataConn = pClient->pDataConn;
    AssertPtr(pDataConn);

    LogFlowFuncEnter();

    uint32_t cbBuf = _64K; /** @todo Improve this. */
    void *pvBuf = RTMemAlloc(cbBuf);
    if (!pvBuf)
        return VERR_NO_MEMORY;

    int rc;

    /* Set start indicator. */
    pDataConn->fStarted = true;

    RTThreadUserSignal(RTThreadSelf());

    AssertPtr(pDataConn->papszArgs);
    const char *pcszFile = pDataConn->papszArgs[0];
    AssertPtr(pcszFile);

    void *pvHandle = NULL; /* Opaque handle known to the actual implementation. */

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileOpen, pcszFile,
                                   RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &pvHandle);
    if (RT_SUCCESS(rc))
    {
        LogFlowFunc(("Transfer started\n"));

        do
        {
            size_t cbRead = 0;
            RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileRead, pvHandle, pvBuf, cbBuf, &cbRead);
            if (   RT_SUCCESS(rc)
                && cbRead)
            {
                rc = rtFtpServerDataConnWrite(pDataConn, pvBuf, cbRead, NULL /* pcbWritten */);
            }

            if (   !cbRead
                || ASMAtomicReadBool(&pDataConn->fStop))
            {
                break;
            }
        }
        while (RT_SUCCESS(rc));

        RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileClose, pvHandle);

        LogFlowFunc(("Transfer done\n"));
    }

    RTMemFree(pvBuf);
    pvBuf = NULL;

    pDataConn->fStopped = true;
    pDataConn->rc       = rc;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates a data connection.
 *
 * @returns VBox status code.
 * @param   pClient             Client to create data connection for.
 * @param   ppDataConn          Where to return the (allocated) data connection.
 */
static int rtFtpServerDataConnCreate(PRTFTPSERVERCLIENT pClient, PRTFTPSERVERDATACONN *ppDataConn)
{
    if (pClient->pDataConn)
        return VERR_FTP_DATA_CONN_LIMIT_REACHED;

    PRTFTPSERVERDATACONN pDataConn = (PRTFTPSERVERDATACONN)RTMemAllocZ(sizeof(RTFTPSERVERDATACONN));
    if (!pDataConn)
        return VERR_NO_MEMORY;

    rtFtpServerDataConnReset(pDataConn);

    pDataConn->pClient = pClient;

    *ppDataConn = pDataConn;

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}

/**
 * Starts a data connection.
 *
 * @returns VBox status code.
 * @param   pClient             Client to start data connection for.
 * @param   pfnThread           Pointer to thread function to use.
 * @param   cArgs               Number of arguments.
 * @param   apcszArgs           Array of arguments.
 */
static int rtFtpServerDataConnStart(PRTFTPSERVERDATACONN pDataConn, PFNRTTHREAD pfnThread,
                                    uint8_t cArgs, const char * const *apcszArgs)
{
    AssertPtrReturn(pDataConn, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnThread, VERR_INVALID_POINTER);

    AssertReturn(!pDataConn->fStarted, VERR_WRONG_ORDER);
    AssertReturn(!pDataConn->fStop,    VERR_WRONG_ORDER);
    AssertReturn(!pDataConn->fStopped, VERR_WRONG_ORDER);

    int rc = VINF_SUCCESS;

    if (cArgs)
    {
        pDataConn->papszArgs = rtFtpCmdArgsDup(cArgs, apcszArgs);
        if (!pDataConn->papszArgs)
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        pDataConn->cArgs = cArgs;

        rc = rtFtpServerDataConnOpen(pDataConn, &pDataConn->Addr, pDataConn->uPort);
        if (RT_SUCCESS(rc))
        {
            rc = RTThreadCreate(&pDataConn->hThread, pfnThread,
                                pDataConn->pClient, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE,
                                "ftpdata");
            if (RT_SUCCESS(rc))
            {
                int rc2 = RTThreadUserWait(pDataConn->hThread, 30 * 1000 /* Timeout in ms */);
                AssertRC(rc2);

                if (!pDataConn->fStarted) /* Did the thread indicate that it started correctly? */
                    rc = VERR_FTP_DATA_CONN_INIT_FAILED;
            }

            if (RT_FAILURE(rc))
                rtFtpServerDataConnClose(pDataConn);
        }
    }

    if (RT_FAILURE(rc))
    {
        rtFtpCmdArgsFree(pDataConn->cArgs, pDataConn->papszArgs);

        pDataConn->cArgs     = 0;
        pDataConn->papszArgs = NULL;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Destroys a data connection.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection to destroy. The pointer is not valid anymore after successful return.
 */
static int rtFtpServerDataConnDestroy(PRTFTPSERVERDATACONN pDataConn)
{
    if (!pDataConn)
        return VINF_SUCCESS;

    LogFlowFuncEnter();

    int rc = VINF_SUCCESS;

    if (pDataConn->hThread != NIL_RTTHREAD)
    {
        /* Set stop indicator. */
        pDataConn->fStop = true;

        int rcThread = VERR_WRONG_ORDER;
        rc = RTThreadWait(pDataConn->hThread, 30 * 1000 /* Timeout in ms */, &rcThread);
    }

    if (RT_SUCCESS(rc))
    {
        rtFtpServerDataConnClose(pDataConn);
        rtFtpCmdArgsFree(pDataConn->cArgs, pDataConn->papszArgs);

        RTMemFree(pDataConn);
        pDataConn = NULL;

        /** @todo Also check / handle rcThread? */
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Resets a data connection structure.
 *
 * @returns VBox status code.
 * @param   pDataConn           Data connection structure to reset.
 */
static void rtFtpServerDataConnReset(PRTFTPSERVERDATACONN pDataConn)
{
    LogFlowFuncEnter();

    pDataConn->hSocket  = NIL_RTSOCKET;
    pDataConn->uPort    = 20; /* Default port to use. */
    pDataConn->hThread  = NIL_RTTHREAD;
    pDataConn->fStarted = false;
    pDataConn->fStop    = false;
    pDataConn->fStopped = false;
    pDataConn->rc       = VERR_IPE_UNINITIALIZED_STATUS;
}


/*********************************************************************************************************************************
*   Command Protocol Handlers                                                                                                    *
*********************************************************************************************************************************/

static int rtFtpServerHandleABOR(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    RT_NOREF(cArgs, apcszArgs);

    int rc = rtFtpServerDataConnDestroy(pClient->pDataConn);
    if (RT_SUCCESS(rc))
    {
        pClient->pDataConn = NULL;

        rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);
    }

    return rc;
}

static int rtFtpServerHandleCDUP(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    RT_NOREF(cArgs, apcszArgs);

    int rc;

    RTFTPSERVER_HANDLE_CALLBACK(pfnOnPathUp);

    if (RT_SUCCESS(rc))
        return rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);

    return rc;
}

static int rtFtpServerHandleCWD(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    int rc;

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnPathSetCurrent, apcszArgs[0]);

    if (RT_SUCCESS(rc))
        return rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);

    return rc;
}

static int rtFtpServerHandleFEAT(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    RT_NOREF(cArgs, apcszArgs);

    int rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_SYSTEM_STATUS); /* Features begin */
    if (RT_SUCCESS(rc))
    {
        rc = rtFtpServerSendReplyStr(pClient, RTFTPSERVER_FEATURES_STRING);
        if (RT_SUCCESS(rc))
            rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_SYSTEM_STATUS); /* Features end */
    }

    return rc;
}

/**
 * Thread for handling the LIST command's output in a separate data connection.
 *
 * @returns VBox status code.
 * @param   ThreadSelf          Thread handle. Unused.
 * @param   pvUser              User-provided arguments. Of type PRTFTPSERVERCLIENT.
 */
static DECLCALLBACK(int) rtFtpServerDataConnListThread(RTTHREAD ThreadSelf, void *pvUser)
{
    RT_NOREF(ThreadSelf);

    PRTFTPSERVERCLIENT pClient = (PRTFTPSERVERCLIENT)pvUser;
    AssertPtr(pClient);

    PRTFTPSERVERDATACONN pDataConn = pClient->pDataConn;
    AssertPtr(pDataConn);

    LogFlowFuncEnter();

    uint32_t cbBuf = _64K; /** @todo Improve this. */
    void *pvBuf = RTMemAlloc(cbBuf);
    if (!pvBuf)
        return VERR_NO_MEMORY;

    int rc;

    /* Set start indicator. */
    pDataConn->fStarted = true;

    RTThreadUserSignal(RTThreadSelf());

    for (;;)
    {
        /* The first argument might indicate a directory to list.
         * If no argument is given, the implementation must use the last directory set. */
        size_t cbRead = 0;
        RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnList,
                                         pDataConn->cArgs == 1
                                       ? pDataConn->papszArgs[0] : NULL, pvBuf, cbBuf, &cbRead);
        if (RT_SUCCESS(rc))
        {
            int rc2 = rtFtpServerDataConnWrite(pDataConn, pvBuf, cbRead, NULL /* pcbWritten */);
            AssertRC(rc2);

            if (rc == VINF_EOF)
                break;
        }
        else
            break;

        if (ASMAtomicReadBool(&pDataConn->fStop))
            break;
    }

    RTMemFree(pvBuf);
    pvBuf = NULL;

    pDataConn->fStopped = true;
    pDataConn->rc       = rc;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static int rtFtpServerHandleLIST(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    int rc;

    /* Note: Data connection gets created when the PORT command was sent. */
    if (pClient->pDataConn)
    {
        rc = rtFtpServerDataConnStart(pClient->pDataConn, rtFtpServerDataConnListThread, cArgs, apcszArgs);
    }
    else
        rc = VERR_FTP_DATA_CONN_NOT_FOUND;

    int rc2 = rtFtpServerSendReplyRc(pClient,   RT_SUCCESS(rc)
                                     ? RTFTPSERVER_REPLY_PATHNAME_OK
                                     : RTFTPSERVER_REPLY_CANT_OPEN_DATA_CONN);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}

static int rtFtpServerHandleMODE(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    RT_NOREF(pClient, cArgs, apcszArgs);

    /** @todo Anything to do here? */
    return VINF_SUCCESS;
}

static int rtFtpServerHandleNOOP(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    RT_NOREF(cArgs, apcszArgs);

    /* Save timestamp of last command sent. */
    pClient->State.tsLastCmdMs = RTTimeMilliTS();

    return rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);
}

static int rtFtpServerHandlePASS(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    if (cArgs != 1)
        return rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_ERROR_INVALID_PARAMETERS);

    const char *pcszPassword = apcszArgs[0];
    AssertPtrReturn(pcszPassword, VERR_INVALID_PARAMETER);

    int rc = rtFtpServerAuthenticate(pClient, pClient->State.pszUser, pcszPassword);
    if (RT_SUCCESS(rc))
    {
        rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_LOGGED_IN_PROCEED);
    }
    else
    {
        pClient->State.cFailedLoginAttempts++;

        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_NOT_LOGGED_IN);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}

static int rtFtpServerHandlePORT(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    if (cArgs != 1)
        return rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_ERROR_INVALID_PARAMETERS);

    PRTFTPSERVERDATACONN pDataConn;
    int rc = rtFtpServerDataConnCreate(pClient, &pDataConn);
    if (RT_SUCCESS(rc))
    {
        pClient->pDataConn = pDataConn;

        rc = rtFtpParseHostAndPort(apcszArgs[0], &pDataConn->Addr, &pDataConn->uPort);
        if (RT_SUCCESS(rc))
        {
            rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);
        }
    }

    if (RT_FAILURE(rc))
    {
        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_CANT_OPEN_DATA_CONN);
        AssertRC(rc2);
    }

    return rc;
}

static int rtFtpServerHandlePWD(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    RT_NOREF(cArgs, apcszArgs);

    int rc;

    char szPWD[RTPATH_MAX];

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnPathGetCurrent, szPWD, sizeof(szPWD));

    if (RT_SUCCESS(rc))
       rc = rtFtpServerSendReplyRcEx(pClient, RTFTPSERVER_REPLY_PATHNAME_OK, "\"%s\"", szPWD); /* See RFC 959, APPENDIX II. */

    return rc;
}

static int rtFtpServerHandleQUIT(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    RT_NOREF(cArgs, apcszArgs);

    int rc = rtFtpServerDataConnDestroy(pClient->pDataConn);
    if (RT_SUCCESS(rc))
        pClient->pDataConn = NULL;

    return rc;
}

static int rtFtpServerHandleRETR(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    if (cArgs != 1) /* File name needs to be present. */
        return VERR_INVALID_PARAMETER;

    int rc;

    const char *pcszPath = apcszArgs[0];

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileStat, pcszPath, NULL /* PRTFSOBJINFO */);

    if (RT_SUCCESS(rc))
    {
        if (pClient->pDataConn)
        {
            /* Note: Data connection gets created when the PORT command was sent. */
            rc = rtFtpServerDataConnStart(pClient->pDataConn, rtFtpServerDataConnFileWriteThread, cArgs, apcszArgs);
            if (RT_SUCCESS(rc))
                rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_FILE_STATUS_OKAY);
        }
        else
            rc = VERR_FTP_DATA_CONN_NOT_FOUND;
    }

    if (RT_FAILURE(rc))
    {
        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_REQ_ACTION_NOT_TAKEN);
        AssertRC(rc2);
    }

    return rc;
}

static int rtFtpServerHandleSIZE(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    int rc;

    const char *pcszPath = apcszArgs[0];
    uint64_t uSize = 0;

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileGetSize, pcszPath, &uSize);

    if (RT_SUCCESS(rc))
    {
        rc = rtFtpServerSendReplyStr(pClient, "213 %RU64\r\n", uSize);
    }
    else
    {
        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_REQ_ACTION_NOT_TAKEN);
        AssertRC(rc2);
    }

    return rc;
}

static int rtFtpServerHandleSTAT(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    int rc;

    RTFSOBJINFO objInfo;
    RT_ZERO(objInfo);

    const char *pcszPath = apcszArgs[0];

    RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnFileStat, pcszPath, &objInfo);

    if (RT_SUCCESS(rc))
    {
        char szFsObjInfo[_4K]; /** @todo Check this size. */
        rc = rtFtpServerFsObjInfoToStr(&objInfo, szFsObjInfo, sizeof(szFsObjInfo));
        if (RT_SUCCESS(rc))
        {
            char szFsPathInfo[RTPATH_MAX + 16];
            const ssize_t cchPathInfo = RTStrPrintf2(szFsPathInfo, sizeof(szFsPathInfo), " %2zu %s\n", strlen(pcszPath), pcszPath);
            if (cchPathInfo > 0)
            {
                rc = RTStrCat(szFsObjInfo, sizeof(szFsObjInfo), szFsPathInfo);
                if (RT_SUCCESS(rc))
                    rc = rtFtpServerSendReplyStr(pClient, szFsObjInfo);
            }
            else
                rc = VERR_BUFFER_OVERFLOW;
        }
    }

    if (RT_FAILURE(rc))
    {
        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_REQ_ACTION_NOT_TAKEN);
        AssertRC(rc2);
    }

    return rc;
}

static int rtFtpServerHandleSTRU(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    const char *pcszType = apcszArgs[0];

    int rc;

    if (!RTStrICmp(pcszType, "F"))
    {
        pClient->State.enmStructType = RTFTPSERVER_STRUCT_TYPE_FILE;

        rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);
    }
    else
        rc = VERR_NOT_IMPLEMENTED;

    return rc;
}

static int rtFtpServerHandleSYST(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    RT_NOREF(cArgs, apcszArgs);

    char szOSInfo[64];
    int rc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szOSInfo, sizeof(szOSInfo));
    if (RT_SUCCESS(rc))
        rc = rtFtpServerSendReplyStr(pClient, szOSInfo);

    return rc;
}

static int rtFtpServerHandleTYPE(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    const char *pcszType = apcszArgs[0];

    int rc = VINF_SUCCESS;

    if (!RTStrICmp(pcszType, "A"))
    {
        pClient->State.enmDataType = RTFTPSERVER_DATA_TYPE_ASCII;
    }
    else if (!RTStrICmp(pcszType, "I")) /* Image (binary). */
    {
        pClient->State.enmDataType = RTFTPSERVER_DATA_TYPE_IMAGE;
    }
    else /** @todo Support "E" (EBCDIC) and/or "L <size>" (custom)? */
        rc = VERR_NOT_IMPLEMENTED;

    if (RT_SUCCESS(rc))
        rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_OKAY);

    return rc;
}

static int rtFtpServerHandleUSER(PRTFTPSERVERCLIENT pClient, uint8_t cArgs, const char * const *apcszArgs)
{
    if (cArgs != 1)
        return VERR_INVALID_PARAMETER;

    const char *pcszUser = apcszArgs[0];
    AssertPtrReturn(pcszUser, VERR_INVALID_PARAMETER);

    rtFtpServerClientStateReset(&pClient->State);

    int rc = rtFtpServerLookupUser(pClient, pcszUser);
    if (RT_SUCCESS(rc))
    {
        pClient->State.pszUser = RTStrDup(pcszUser);
        AssertPtrReturn(pClient->State.pszUser, VERR_NO_MEMORY);

        rc = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_USERNAME_OKAY_NEED_PASSWORD);
    }
    else
    {
        pClient->State.cFailedLoginAttempts++;

        int rc2 = rtFtpServerSendReplyRc(pClient, RTFTPSERVER_REPLY_NOT_LOGGED_IN);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}


/*********************************************************************************************************************************
*   Internal server functions                                                                                                    *
*********************************************************************************************************************************/

/**
 * Parses FTP command arguments handed in by the client.
 *
 * @returns VBox status code.
 * @param   pcszCmdParms        Pointer to command arguments, if any. Can be NULL if no arguments are given.
 * @param   pcArgs              Returns the number of parsed arguments, separated by a space (hex 0x20).
 * @param   ppapcszArgs         Returns the string array of parsed arguments. Needs to be free'd with rtFtpServerCmdArgsFree().
 */
static int rtFtpServerCmdArgsParse(const char *pcszCmdParms, uint8_t *pcArgs, char ***ppapcszArgs)
{
    *pcArgs      = 0;
    *ppapcszArgs = NULL;

    if (!pcszCmdParms) /* No parms given? Bail out early. */
        return VINF_SUCCESS;

    /** @todo Anything else to do here? */
    /** @todo Check if quoting is correct. */

    int cArgs = 0;
    int rc = RTGetOptArgvFromString(ppapcszArgs, &cArgs, pcszCmdParms, RTGETOPTARGV_CNV_QUOTE_MS_CRT, " " /* Separators */);
    if (RT_SUCCESS(rc))
    {
        if (cArgs <= UINT8_MAX)
        {
            *pcArgs = (uint8_t)cArgs;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

/**
 * Frees a formerly argument string array parsed by rtFtpServerCmdArgsParse().
 *
 * @param   ppapcszArgs         Argument string array to free.
 */
static void rtFtpServerCmdArgsFree(char **ppapcszArgs)
{
    RTGetOptArgvFree(ppapcszArgs);
}

/**
 * Main function for processing client commands for the control connection.
 *
 * @returns VBox status code.
 * @param   pClient             Client to process commands for.
 * @param   pcszCmd             Command string to parse and handle.
 * @param   cbCmd               Size (in bytes) of command string.
 */
static int rtFtpServerProcessCommands(PRTFTPSERVERCLIENT pClient, char *pcszCmd, size_t cbCmd)
{
    /* Make sure to terminate the string in any case. */
    pcszCmd[RT_MIN(RTFTPSERVER_MAX_CMD_LEN, cbCmd)] = '\0';

    /* A tiny bit of sanitation. */
    RTStrStripL(pcszCmd);

    /* First, terminate string by finding the command end marker (telnet style). */
    /** @todo Not sure if this is entirely correct and/or needs tweaking; good enough for now as it seems. */
    char *pszCmdEnd = RTStrIStr(pcszCmd, "\r\n");
    if (pszCmdEnd)
        *pszCmdEnd = '\0';

    /* Reply which gets sent back to the client. */
    RTFTPSERVER_REPLY rcClient = RTFTPSERVER_REPLY_INVALID;

    int rcCmd = VINF_SUCCESS;

    uint8_t cArgs     = 0;
    char  **papszArgs = NULL;
    int rc = rtFtpServerCmdArgsParse(pcszCmd, &cArgs, &papszArgs);
    if (   RT_SUCCESS(rc)
        && cArgs) /* At least the actual command (without args) must be present. */
    {
        LogFlowFunc(("Handling command '%s'\n", papszArgs[0]));

        unsigned i = 0;
        for (; i < RT_ELEMENTS(g_aCmdMap); i++)
        {
            const RTFTPSERVER_CMD_ENTRY *pCmdEntry = &g_aCmdMap[i];

            if (!RTStrICmp(papszArgs[0], pCmdEntry->szCmd))
            {
                /* Some commands need a valid user before they can be executed. */
                if (   pCmdEntry->fNeedsUser
                    && pClient->State.pszUser == NULL)
                {
                    rcClient = RTFTPSERVER_REPLY_NOT_LOGGED_IN;
                    break;
                }

                /* Save timestamp of last command sent. */
                pClient->State.tsLastCmdMs = RTTimeMilliTS();

                /* Hand in arguments only without the actual command. */
                rcCmd = pCmdEntry->pfnCmd(pClient, cArgs - 1, cArgs > 1 ? &papszArgs[1] : NULL);
                if (RT_FAILURE(rcCmd))
                {
                    LogFunc(("Handling command '%s' failed with %Rrc\n", papszArgs[0], rcCmd));

                    switch (rcCmd)
                    {
                        case VERR_INVALID_PARAMETER:
                            RT_FALL_THROUGH();
                        case VERR_INVALID_POINTER:
                            rcClient = RTFTPSERVER_REPLY_ERROR_INVALID_PARAMETERS;
                            break;

                        case VERR_NOT_IMPLEMENTED:
                            rcClient = RTFTPSERVER_REPLY_ERROR_CMD_NOT_IMPL;
                            break;

                        default:
                            break;
                    }
                }
                break;
            }
        }

        rtFtpServerCmdArgsFree(papszArgs);

        if (i == RT_ELEMENTS(g_aCmdMap))
        {
            LogFlowFunc(("Command not implemented\n"));
            Assert(rcClient == RTFTPSERVER_REPLY_INVALID);
            rcClient = RTFTPSERVER_REPLY_ERROR_CMD_NOT_IMPL;
        }

        const bool fDisconnect =    g_aCmdMap[i].enmCmd == RTFTPSERVER_CMD_QUIT
                                 || pClient->State.cFailedLoginAttempts >= 3; /** @todo Make this dynamic. */
        if (fDisconnect)
        {
            RTFTPSERVER_HANDLE_CALLBACK_VA(pfnOnUserDisconnect, pClient->State.pszUser);

            Assert(rcClient == RTFTPSERVER_REPLY_INVALID);
            rcClient = RTFTPSERVER_REPLY_CLOSING_CTRL_CONN;
        }
    }
    else
        rcClient = RTFTPSERVER_REPLY_ERROR_INVALID_PARAMETERS;

    if (rcClient != RTFTPSERVER_REPLY_INVALID)
    {
        int rc2 = rtFtpServerSendReplyRc(pClient, rcClient);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Main loop for processing client commands.
 *
 * @returns VBox status code.
 * @param   pClient             Client to process commands for.
 */
static int rtFtpServerProcessCommands(PRTFTPSERVERCLIENT pClient)
{
    int rc;

    size_t cbRead;
    char   szCmd[RTFTPSERVER_MAX_CMD_LEN + 1];

    for (;;)
    {
        rc = RTTcpSelectOne(pClient->hSocket, 200 /* ms */); /** @todo Can we improve here? Using some poll events or so? */
        if (RT_SUCCESS(rc))
        {
            rc = RTTcpReadNB(pClient->hSocket, szCmd, sizeof(szCmd), &cbRead);
            if (   RT_SUCCESS(rc)
                && cbRead)
            {
                AssertBreakStmt(cbRead <= sizeof(szCmd), rc = VERR_BUFFER_OVERFLOW);
                rc = rtFtpServerProcessCommands(pClient, szCmd, cbRead);
            }
        }
        else
        {
            if (rc == VERR_TIMEOUT)
                rc = VINF_SUCCESS;

            if (RT_FAILURE(rc))
                break;
        }

        /*
         * Handle data connection replies.
         */
        if (pClient->pDataConn)
        {
            if (   ASMAtomicReadBool(&pClient->pDataConn->fStarted)
                && ASMAtomicReadBool(&pClient->pDataConn->fStopped))
            {
                Assert(pClient->pDataConn->rc != VERR_IPE_UNINITIALIZED_STATUS);

                rc = rtFtpServerSendReplyRc(pClient, RT_SUCCESS(pClient->pDataConn->rc)
                                                     ? RTFTPSERVER_REPLY_CLOSING_DATA_CONN
                                                     : RTFTPSERVER_REPLY_CONN_CLOSED_TRANSFER_ABORTED);

                int rc2 = rtFtpServerDataConnDestroy(pClient->pDataConn);
                if (RT_SUCCESS(rc2))
                    pClient->pDataConn = NULL;

                if (RT_SUCCESS(rc))
                    rc = rc2;
            }
        }
    }

    /* Make sure to destroy all data connections. */
    int rc2 = rtFtpServerDataConnDestroy(pClient->pDataConn);
    if (RT_SUCCESS(rc2))
        pClient->pDataConn = NULL;

    if (RT_SUCCESS(rc))
        rc = rc2;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Resets the client's state.
 *
 * @param   pState              Client state to reset.
 */
static void rtFtpServerClientStateReset(PRTFTPSERVERCLIENTSTATE pState)
{
    LogFlowFuncEnter();

    RTStrFree(pState->pszUser);
    pState->pszUser = NULL;

    pState->cFailedLoginAttempts = 0;
    pState->tsLastCmdMs          = RTTimeMilliTS();
    pState->enmDataType          = RTFTPSERVER_DATA_TYPE_ASCII;
    pState->enmStructType        = RTFTPSERVER_STRUCT_TYPE_FILE;
}

/**
 * Per-client thread for serving the server's control connection.
 *
 * @returns VBox status code.
 * @param   hSocket             Socket handle to use for the control connection.
 * @param   pvUser              User-provided arguments. Of type PRTFTPSERVERINTERNAL.
 */
static DECLCALLBACK(int) rtFtpServerClientThread(RTSOCKET hSocket, void *pvUser)
{
    PRTFTPSERVERINTERNAL pThis = (PRTFTPSERVERINTERNAL)pvUser;
    RTFTPSERVER_VALID_RETURN(pThis);

    RTFTPSERVERCLIENT Client;
    RT_ZERO(Client);

    Client.pServer     = pThis;
    Client.hSocket     = hSocket;

    LogFlowFunc(("New client connected\n"));

    rtFtpServerClientStateReset(&Client.State);

    /*
     * Send welcome message.
     * Note: Some clients (like FileZilla / Firefox) expect a message together with the reply code,
     *       so make sure to include at least *something*.
     */
    int rc = rtFtpServerSendReplyRcEx(&Client, RTFTPSERVER_REPLY_READY_FOR_NEW_USER,
                                      "Welcome!");
    if (RT_SUCCESS(rc))
    {
        ASMAtomicIncU32(&pThis->cClients);

        rc = rtFtpServerProcessCommands(&Client);

        ASMAtomicDecU32(&pThis->cClients);
    }

    rtFtpServerClientStateReset(&Client.State);

    return rc;
}

RTR3DECL(int) RTFtpServerCreate(PRTFTPSERVER phFTPServer, const char *pcszAddress, uint16_t uPort,
                                PRTFTPSERVERCALLBACKS pCallbacks, void *pvUser, size_t cbUser)
{
    AssertPtrReturn(phFTPServer,  VERR_INVALID_POINTER);
    AssertPtrReturn(pcszAddress,  VERR_INVALID_POINTER);
    AssertReturn   (uPort,        VERR_INVALID_PARAMETER);
    AssertPtrReturn(pCallbacks,   VERR_INVALID_POINTER);
    /* pvUser is optional. */

    int rc;

    PRTFTPSERVERINTERNAL pThis = (PRTFTPSERVERINTERNAL)RTMemAllocZ(sizeof(RTFTPSERVERINTERNAL));
    if (pThis)
    {
        pThis->u32Magic  = RTFTPSERVER_MAGIC;
        pThis->Callbacks = *pCallbacks;
        pThis->pvUser    = pvUser;
        pThis->cbUser    = cbUser;

        rc = RTTcpServerCreate(pcszAddress, uPort, RTTHREADTYPE_DEFAULT, "ftpsrv",
                               rtFtpServerClientThread, pThis /* pvUser */, &pThis->pTCPServer);
        if (RT_SUCCESS(rc))
        {
            *phFTPServer = (RTFTPSERVER)pThis;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

RTR3DECL(int) RTFtpServerDestroy(RTFTPSERVER hFTPServer)
{
    if (hFTPServer == NIL_RTFTPSERVER)
        return VINF_SUCCESS;

    PRTFTPSERVERINTERNAL pThis = hFTPServer;
    RTFTPSERVER_VALID_RETURN(pThis);

    AssertPtr(pThis->pTCPServer);

    int rc = RTTcpServerDestroy(pThis->pTCPServer);
    if (RT_SUCCESS(rc))
    {
        pThis->u32Magic = RTFTPSERVER_MAGIC_DEAD;

        RTMemFree(pThis);
    }

    return rc;
}

