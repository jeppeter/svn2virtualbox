/*
 * Copyright (C) 2001 Mike McCormack
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the language indicating
 * that LGPLv2 or any later version may be used, or where a choice of which version
 * of the LGPL is applied is otherwise unspecified.
 */

#ifndef NCB_INCLUDED
#define NCB_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#define NCBNAMSZ 16
#define MAX_LANA 0xfe

#define NCBCALL        0x10
#define NCBLISTEN      0x11
#define NCBHANGUP      0x12
#define NCBSEND        0x14
#define NCBRECV        0x15
#define NCBRECVANY     0x16
#define NCBCHAINSEND   0x17
#define NCBDGSEND      0x20
#define NCBDGRECV      0x21
#define NCBDGSENDBC    0x22
#define NCBDGRECVBC    0x23
#define NCBADDNAME     0x30
#define NCBDELNAME     0x31
#define NCBRESET       0x32
#define NCBASTAT       0x33
#define NCBSSTAT       0x34
#define NCBCANCEL      0x35
#define NCBADDGRNAME   0x36
#define NCBENUM        0x37
#define NCBUNLINK      0x70
#define NCBSENDNA      0x71
#define NCBCHAINSENDNA 0x72
#define NCBLANSTALERT  0x73
#define NCBACTION      0x77
#define NCBFINDNAME    0x78
#define NCBTRACE       0x79

#define ASYNCH         0x80

typedef struct _NCB
{
	UCHAR	ncb_command;
	UCHAR	ncb_retcode;
	UCHAR	ncb_lsn;
	UCHAR	ncb_num;
	PUCHAR	ncb_buffer;
	WORD	ncb_length;
	UCHAR	ncb_callname[NCBNAMSZ];
	UCHAR	ncb_name[NCBNAMSZ];
	UCHAR	ncb_rto;
	UCHAR	ncb_sto;
	VOID	(CALLBACK *ncb_post)(struct _NCB *);
	UCHAR	ncb_lana_num;
	UCHAR	ncb_cmd_cplt;
#ifdef _WIN64
	UCHAR	ncb_reserve[18];
#else
	UCHAR	ncb_reserve[10];
#endif
	HANDLE	ncb_event;
} NCB, *PNCB;

typedef struct _ADAPTER_STATUS
{
	UCHAR   adapter_address[6];
	UCHAR	rev_major;
	UCHAR	reserved0;
	UCHAR	adapter_type;
	UCHAR	rev_minor;
	WORD	duration;
	WORD	frmr_recv;
	WORD	frmr_xmit;
	WORD	iframe_recv_error;
	WORD	xmit_aborts;
	DWORD	xmit_success;
	DWORD	recv_success;
	WORD	iframe_xmit_error;
	WORD	recv_buffer_unavail;
	WORD	t1_timeouts;
	WORD	ti_timeouts;
	DWORD	reserved1;
	WORD	free_ncbs;
	WORD	max_cfg_ncbs;
	WORD	max_ncbs;
	WORD	xmit_buf_unavail;
	WORD	max_dgram_size;
	WORD	pending_sess;
	WORD	max_cfg_sess;
	WORD	max_sess;
	WORD	max_sess_pkt_size;
	WORD	name_count;
} ADAPTER_STATUS, *PADAPTER_STATUS;

typedef struct _NAME_BUFFER
{
  UCHAR name[NCBNAMSZ];
  UCHAR name_num;
  UCHAR name_flags;
} NAME_BUFFER, *PNAME_BUFFER;

#define NAME_FLAGS_MASK 0x87
#define GROUP_NAME      0x80
#define UNIQUE_NAME     0x00
#define REGISTERING     0x00
#define REGISTERED      0x04
#define DEREGISTERED    0x05
#define DUPLICATE       0x06
#define DUPLICATE_DEREG 0x07

typedef struct _LANA_ENUM
{
	UCHAR length;
	UCHAR lana[MAX_LANA+1];
} LANA_ENUM, *PLANA_ENUM;

typedef struct _FIND_NAME_HEADER
{
  WORD  node_count;
  UCHAR reserved;
  UCHAR unique_group;
} FIND_NAME_HEADER, *PFIND_NAME_HEADER;

typedef struct _FIND_NAME_BUFFER
{
  UCHAR length;
  UCHAR access_control;
  UCHAR frame_control;
  UCHAR destination_addr[6];
  UCHAR source_addr[6];
  UCHAR routing_info[6];
} FIND_NAME_BUFFER, *PFIND_NAME_BUFFER;

typedef struct _SESSION_HEADER {
  UCHAR sess_name;
  UCHAR num_sess;
  UCHAR rcv_dg_outstanding;
  UCHAR rcv_any_outstanding;
} SESSION_HEADER, *PSESSION_HEADER;

typedef struct _SESSION_BUFFER {
  UCHAR lsn;
  UCHAR state;
  UCHAR local_name[NCBNAMSZ];
  UCHAR remote_name[NCBNAMSZ];
  UCHAR rcvs_outstanding;
  UCHAR sends_outstanding;
} SESSION_BUFFER, *PSESSION_BUFFER;

#define LISTEN_OUTSTANDING  0x01
#define CALL_PENDING        0x02
#define SESSION_ESTABLISHED 0x03
#define HANGUP_PENDING      0x04
#define HANGUP_COMPLETE     0x05
#define SESSION_ABORTED     0x06

#define ALL_TRANSPORTS "M\0\0\0"

#define NRC_GOODRET     0x00
#define NRC_BUFLEN      0x01
#define NRC_ILLCMD      0x03
#define NRC_CMDTMO      0x05
#define NRC_INCOMP      0x06
#define NRC_BADDR       0x07
#define NRC_SNUMOUT     0x08
#define NRC_NORES       0x09
#define NRC_SCLOSED     0x0a
#define NRC_CMDCAN      0x0b
#define NRC_DUPNAME     0x0d
#define NRC_NAMTFUL     0x0e
#define NRC_ACTSES      0x0f
#define NRC_LOCTFUL     0x11
#define NRC_REMTFUL     0x12
#define NRC_ILLNN       0x13
#define NRC_NOCALL      0x14
#define NRC_NOWILD      0x15
#define NRC_INUSE       0x16
#define NRC_NAMERR      0x17
#define NRC_SABORT      0x18
#define NRC_NAMCONF     0x19
#define NRC_IFBUSY      0x21
#define NRC_TOOMANY     0x22
#define NRC_BRIDGE      0x23
#define NRC_CANOCCR     0x24
#define NRC_CANCEL      0x26
#define NRC_DUPENV      0x30
#define NRC_ENVNOTDEF   0x34
#define NRC_OSRESNOTAV  0x35
#define NRC_MAXAPPS     0x36
#define NRC_NOSAPS      0x37
#define NRC_NORESOURCES 0x38
#define NRC_INVADDRESS  0x39
#define NRC_INVDDID     0x3b
#define NRC_LOCKFAIL    0x3c
#define NRC_OPENERROR   0x3f
#define NRC_SYSTEM      0x40
#define NRC_PENDING     0xff

UCHAR WINAPI Netbios(PNCB pncb);

#ifdef __cplusplus
}
#endif

#endif  /* NCB_INCLUDED */
