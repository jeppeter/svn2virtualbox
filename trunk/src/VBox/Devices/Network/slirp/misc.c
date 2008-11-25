/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#define WANT_SYS_IOCTL_H
#include <slirp.h>

#ifndef HAVE_INET_ATON
int
inet_aton(cp, ia)
        const char *cp;
        struct in_addr *ia;
{
        u_int32_t addr = inet_addr(cp);
        if (addr == 0xffffffff)
                return 0;
        ia->s_addr = addr;
        return 1;
}
#endif

/*
 * Get our IP address and put it in our_addr
 */
void
getouraddr(PNATState pData)
{
        our_addr.s_addr = loopback_addr.s_addr;
}

#if !defined(VBOX_WITH_BSD_REASS) && !defined(VBOX_WITH_BSD_TCP_REASS)
#if SIZEOF_CHAR_P == 8

struct quehead_32 {
        u_int32_t qh_link;
        u_int32_t qh_rlink;
};

void
insque_32(PNATState pData, void *a, void *b)
{
        register struct quehead_32 *element = (struct quehead_32 *) a;
        register struct quehead_32 *head = (struct quehead_32 *) b;
        struct quehead_32 *link = u32_to_ptr(pData, head->qh_link, struct quehead_32 *);

        element->qh_link = head->qh_link;
        element->qh_rlink = ptr_to_u32(pData, head);
        Assert(link->qh_rlink == element->qh_rlink);
        link->qh_rlink = head->qh_link = ptr_to_u32(pData, element);
}

void
remque_32(PNATState pData, void *a)
{
        register struct quehead_32 *element = (struct quehead_32 *) a;
        struct quehead_32 *link = u32_to_ptr(pData, element->qh_link, struct quehead_32 *);
        struct quehead_32 *rlink = u32_to_ptr(pData, element->qh_rlink, struct quehead_32 *);

        u32ptr_done(pData, link->qh_rlink, element);
        link->qh_rlink = element->qh_rlink;
        rlink->qh_link = element->qh_link;
        element->qh_rlink = 0;
}

#endif /* SIZEOF_CHAR_P == 8 */
#endif /* !VBOX_WITH_BSD_REASS && !VBOX_WITH_BSD_TCP_REASS*/

struct quehead {
        struct quehead *qh_link;
        struct quehead *qh_rlink;
};

void
insque(PNATState pData, void *a, void *b)
{
        register struct quehead *element = (struct quehead *) a;
        register struct quehead *head = (struct quehead *) b;
        element->qh_link = head->qh_link;
        head->qh_link = (struct quehead *)element;
        element->qh_rlink = (struct quehead *)head;
        ((struct quehead *)(element->qh_link))->qh_rlink
        = (struct quehead *)element;
}

void
remque(PNATState pData, void *a)
{
  register struct quehead *element = (struct quehead *) a;
  ((struct quehead *)(element->qh_link))->qh_rlink = element->qh_rlink;
  ((struct quehead *)(element->qh_rlink))->qh_link = element->qh_link;
  element->qh_rlink = NULL;
  /*  element->qh_link = NULL;  TCP FIN1 crashes if you do this.  Why ? */
}

/* #endif */


int
add_exec(ex_ptr, do_pty, exec, addr, port)
        struct ex_list **ex_ptr;
        int do_pty;
        char *exec;
        int addr;
        int port;
{
        struct ex_list *tmp_ptr;

        /* First, check if the port is "bound" */
        for (tmp_ptr = *ex_ptr; tmp_ptr; tmp_ptr = tmp_ptr->ex_next) {
                if (port == tmp_ptr->ex_fport && addr == tmp_ptr->ex_addr)
                   return -1;
        }

        tmp_ptr = *ex_ptr;
        *ex_ptr = (struct ex_list *)malloc(sizeof(struct ex_list));
        (*ex_ptr)->ex_fport = port;
        (*ex_ptr)->ex_addr = addr;
        (*ex_ptr)->ex_pty = do_pty;
        (*ex_ptr)->ex_exec = strdup(exec);
        (*ex_ptr)->ex_next = tmp_ptr;
        return 0;
}

#ifndef HAVE_STRERROR

/*
 * For systems with no strerror
 */

extern int sys_nerr;
extern char *sys_errlist[];

char *
strerror(error)
        int error;
{
        if (error < sys_nerr)
           return sys_errlist[error];
        else
           return "Unknown error.";
}

#endif


#ifndef HAVE_STRDUP
char *
strdup(str)
        const char *str;
{
        char *bptr;

        bptr = (char *)malloc(strlen(str)+1);
        strcpy(bptr, str);

        return bptr;
}
#endif

#ifdef BAD_SPRINTF

#undef vsprintf
#undef sprintf

/*
 * Some BSD-derived systems have a sprintf which returns char *
 */

int
vsprintf_len(string, format, args)
        char *string;
        const char *format;
        va_list args;
{
        vsprintf(string, format, args);
        return strlen(string);
}

int
#ifdef __STDC__
sprintf_len(char *string, const char *format, ...)
#else
sprintf_len(va_alist) va_dcl
#endif
{
        va_list args;
#ifdef __STDC__
        va_start(args, format);
#else
        char *string;
        char *format;
        va_start(args);
        string = va_arg(args, char *);
        format = va_arg(args, char *);
#endif
        vsprintf(string, format, args);
        return strlen(string);
}

#endif

void
u_sleep(usec)
        int usec;
{
        struct timeval t;
        fd_set fdset;

        FD_ZERO(&fdset);

        t.tv_sec = 0;
        t.tv_usec = usec * 1000;

        select(0, &fdset, &fdset, &fdset, &t);
}

/*
 * Set fd blocking and non-blocking
 */

void
fd_nonblock(fd)
        int fd;
{
#ifdef FIONBIO
        int opt = 1;

        ioctlsocket(fd, FIONBIO, &opt);
#else
        int opt;

        opt = fcntl(fd, F_GETFL, 0);
        opt |= O_NONBLOCK;
        fcntl(fd, F_SETFL, opt);
#endif
}

void
fd_block(fd)
        int fd;
{
#ifdef FIONBIO
        int opt = 0;

        ioctlsocket(fd, FIONBIO, &opt);
#else
        int opt;

        opt = fcntl(fd, F_GETFL, 0);
        opt &= ~O_NONBLOCK;
        fcntl(fd, F_SETFL, opt);
#endif
}

