/** @file
 *
 * VBox Remote Desktop Protocol:
 * External Authentication Library:
 * Linux PAM Authentication.
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/* The PAM service name.
 *
 * The service name is the name of a file in the /etc/pam.d which contains
 * authentication rules. It is possible to use an existing service
 * name, like "login" for example. But if different set of rules
 * is required, one can create a new file /etc/pam.d/vrdpauth
 * specially for VRDP authentication. Note that the name of the
 * service must be lowercase. See PAM documentation for details.
 *
 * The VRDPAuth module takes the PAM service name from the
 * environment variable VRDP_AUTH_PAM_SERVICE. If the variable
 * is not specified, then the 'login' PAM service is used.
 */
#define VRDP_AUTH_PAM_SERVICE_NAME_ENV "VRDP_AUTH_PAM_SERVICE"
#define VRDP_AUTH_PAM_DEFAULT_SERVICE_NAME "login"


/* The debug log file name.
 *
 * If defined, debug messages will be written to the file specified in the
 * VRDP_AUTH_DEBUG_FILENAME environment variable:
 *
 * export VRDP_AUTH_DEBUG_FILENAME=pam.log
 *
 * The above will cause writing to the pam.log.
 */
#define VRDP_AUTH_DEBUG_FILENAME_ENV "VRDP_AUTH_DEBUG_FILENAME"


/* Dynamic loading of the PAM library.
 *
 * If defined, the libpam.so is loaded dynamically.
 * Enabled by default since it is often required,
 * and does not harm.
 */
#define VRDP_PAM_DLLOAD


#ifdef VRDP_PAM_DLLOAD
/* The name of the PAM library */
# ifdef RT_OS_SOLARIS
#  define VRDP_PAM_LIB "libpam.so.1"
# else
#  define VRDP_PAM_LIB "libpam.so.0"
# endif
#endif /* VRDP_PAM_DLLOAD */


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#ifndef RT_OS_FREEBSD
# include <malloc.h>
#endif

#include <security/pam_appl.h>

#include <VBox/VRDPAuth.h>

#ifdef VRDP_PAM_DLLOAD
#include <dlfcn.h>

static int (*fn_pam_start)(const char *service_name,
                           const char *user,
                           const struct pam_conv *pam_conversation,
                           pam_handle_t **pamh);
static int (*fn_pam_authenticate)(pam_handle_t *pamh, int flags);
static int (*fn_pam_acct_mgmt)(pam_handle_t *pamh, int flags);
static int (*fn_pam_end)(pam_handle_t *pamh, int pam_status);
static const char * (*fn_pam_strerror)(pam_handle_t *pamh, int errnum);
#else
#define fn_pam_start        pam_start
#define fn_pam_authenticate pam_authenticate
#define fn_pam_acct_mgmt    pam_acct_mgmt
#define fn_pam_end          pam_end
#define fn_pam_strerror     pam_strerror
#endif /* VRDP_PAM_DLLOAD */

static void debug_printf(const char *fmt, ...)
{
#ifdef VRDP_AUTH_DEBUG_FILENAME_ENV
    va_list va;

    char buffer[1024];

    const char *filename = getenv (VRDP_AUTH_DEBUG_FILENAME_ENV);

    va_start(va, fmt);

    if (filename)
    {
       FILE *f;

       vsnprintf (buffer, sizeof (buffer), fmt, va);

       f = fopen (filename, "ab");
       if (f != NULL)
       {
          fprintf (f, "%s", buffer);
          fclose (f);
       }
    }

    va_end (va);
#endif /* VRDP_AUTH_DEBUG_FILENAME_ENV */
}

#ifdef VRDP_PAM_DLLOAD

static void *gpvLibPam = NULL;

typedef struct _SymMap
{
    void **ppfn;
    const char *pszName;
} SymMap;

static SymMap symmap[] =
{
    { (void **)&fn_pam_start,        "pam_start" },
    { (void **)&fn_pam_authenticate, "pam_authenticate" },
    { (void **)&fn_pam_acct_mgmt,    "pam_acct_mgmt" },
    { (void **)&fn_pam_end,          "pam_end" },
    { (void **)&fn_pam_strerror,     "pam_strerror" },
    { NULL,                          NULL }
};

static int vrdpauth_pam_init(void)
{
    SymMap *iter;

    gpvLibPam = dlopen(VRDP_PAM_LIB, RTLD_LAZY | RTLD_GLOBAL);

    if (!gpvLibPam)
    {
        debug_printf("vrdpauth_pam_init: dlopen %s failed\n", VRDP_PAM_LIB);
        return PAM_SYSTEM_ERR;
    }

    iter = &symmap[0];

    while (iter->pszName != NULL)
    {
        void *pv = dlsym (gpvLibPam, iter->pszName);

        if (pv == NULL)
        {
            debug_printf("vrdpauth_pam_init: dlsym %s failed\n", iter->pszName);

            dlclose(gpvLibPam);
            gpvLibPam = NULL;

            return PAM_SYSTEM_ERR;
        }

        *iter->ppfn = pv;

        iter++;
    }

    return PAM_SUCCESS;
}

static void vrdpauth_pam_close(void)
{
    if (gpvLibPam)
    {
        dlclose(gpvLibPam);
        gpvLibPam = NULL;
    }

    return;
}
#else
static int vrdpauth_pam_init(void)
{
    return PAM_SUCCESS;
}

static void vrdpauth_pam_close(void)
{
    return;
}
#endif /* VRDP_PAM_DLLOAD */

static const char *vrdpauth_get_pam_service (void)
{
    const char *service = getenv (VRDP_AUTH_PAM_SERVICE_NAME_ENV);

    if (service == NULL)
    {
        service = VRDP_AUTH_PAM_DEFAULT_SERVICE_NAME;
    }

    debug_printf ("Using PAM service: %s\n", service);

    return service;
}

typedef struct _PamContext
{
    char *szUser;
    char *szPassword;
} PamContext;

static int conv (int num_msg, const struct pam_message **msg,
                 struct pam_response **resp, void *appdata_ptr)
{
    int i;
    struct pam_response *r;

    PamContext *ctx = (PamContext *)appdata_ptr;

    if (ctx == NULL)
    {
        debug_printf("conv: ctx is NULL\n");
        return PAM_CONV_ERR;
    }

    debug_printf("conv: num %d u[%s] p[%d]\n", num_msg, ctx->szUser, ctx->szPassword? strlen (ctx->szPassword): 0);

    r = (struct pam_response *) calloc (num_msg, sizeof (struct pam_response));

    if (r == NULL)
    {
        return PAM_CONV_ERR;
    }

    for (i = 0; i < num_msg; i++)
    {
        r[i].resp_retcode = 0;

        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF)
        {
            r[i].resp = strdup (ctx->szPassword);
            debug_printf("conv: %d returning password [%d]\n", i, r[i].resp? strlen (r[i].resp): 0);
        }
        else if (msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
        {
            r[i].resp = strdup (ctx->szUser);
            debug_printf("conv: %d returning name [%s]\n", i, r[i].resp);
        }
        else
        {
            debug_printf("conv: %d style %d: [%s]\n", i, msg[i]->msg_style, msg[i]->msg? msg[i]->msg: "(null)");
            r[i].resp = NULL;
        }
    }

    *resp = r;
    return PAM_SUCCESS;
}

/* The VRDPAuth entry point must be visible. */
#if defined(_MSC_VER) || defined(__OS2__)
# define DECLEXPORT(type)       __declspec(dllexport) type
#else
# ifdef VBOX_HAVE_VISIBILITY_HIDDEN
#  define DECLEXPORT(type)      __attribute__((visibility("default"))) type
# else
#  define DECLEXPORT(type)      type
# endif
#endif

/* prototype to prevent gcc warning */
DECLEXPORT(VRDPAuthResult) VRDPAUTHCALL VRDPAuth (PVRDPAUTHUUID pUuid,
                                                  VRDPAuthGuestJudgement guestJudgement,
                                                  const char *szUser,
                                                  const char *szPassword,
                                                  const char *szDomain);
DECLEXPORT(VRDPAuthResult) VRDPAUTHCALL VRDPAuth (PVRDPAUTHUUID pUuid,
                                                  VRDPAuthGuestJudgement guestJudgement,
                                                  const char *szUser,
                                                  const char *szPassword,
                                                  const char *szDomain)
{
    VRDPAuthResult result = VRDPAuthAccessDenied;

    int rc;

    PamContext ctx;
    struct pam_conv pam_conversation;

    pam_handle_t *pam_handle = NULL;

    debug_printf("u[%s], d[%s], p[%d]\n", szUser, szDomain, szPassword? strlen (szPassword): 0);

    ctx.szUser     = (char *)szUser;
    ctx.szPassword = (char *)szPassword;

    pam_conversation.conv        = conv;
    pam_conversation.appdata_ptr = &ctx;

    rc = vrdpauth_pam_init ();

    if (rc == PAM_SUCCESS)
    {
        debug_printf("init ok\n");

        rc = fn_pam_start(vrdpauth_get_pam_service (), szUser, &pam_conversation, &pam_handle);

        if (rc == PAM_SUCCESS)
        {
            debug_printf("start ok\n");

            rc = fn_pam_authenticate(pam_handle, 0);

            if (rc == PAM_SUCCESS)
            {
                debug_printf("auth ok\n");

                rc = fn_pam_acct_mgmt(pam_handle, 0);
                if (rc == PAM_AUTHINFO_UNAVAIL
                    &&
                    getenv("VBOX_PAM_ALLOW_INACTIVE") != NULL)
                {
                    debug_printf("PAM_AUTHINFO_UNAVAIL\n");
                    rc = PAM_SUCCESS;
                }

                if (rc == PAM_SUCCESS)
                {
                    debug_printf("access granted\n");

                    result = VRDPAuthAccessGranted;
                }
                else
                {
                    debug_printf("pam_acct_mgmt failed %d. %s\n", rc, fn_pam_strerror (pam_handle, rc));
                }
            }
            else
            {
                debug_printf("pam_authenticate failed %d. %s\n", rc, fn_pam_strerror (pam_handle, rc));
            }

            fn_pam_end(pam_handle, rc);
        }
        else
        {
            debug_printf("pam_start failed %d\n", rc);
        }

        vrdpauth_pam_close ();

        debug_printf("vrdpauth_pam_close completed\n");
    }
    else
    {
        debug_printf("vrdpauth_pam_init failed %d\n", rc);
    }

    return result;
}

/* Verify the function prototype. */
static PVRDPAUTHENTRY gpfnAuthEntry = VRDPAuth;
