/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_MAC_PROVIDER_H
#define	_SYS_MAC_PROVIDER_H

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stream.h>
#include <sys/mkdev.h>
#include <sys/mac_flow.h>
#include <sys/mac.h>

/*
 * MAC Provider Interface
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * MAC version identifier.  This is used by mac_alloc() mac_register() to
 * verify that incompatible drivers don't register.
 */
#define	MAC_VERSION	0x2

/*
 * Opaque handle types
 */
typedef struct __mac_rule_handle	*mac_rule_handle_t;

/*
 * Statistics
 */

#define	XCVR_UNDEFINED		0
#define	XCVR_NONE		1
#define	XCVR_10			2
#define	XCVR_100T4		3
#define	XCVR_100X		4
#define	XCVR_100T2		5
#define	XCVR_1000X		6
#define	XCVR_1000T		7

#ifdef	_KERNEL

/*
 * Definitions for MAC Drivers Capabilities
 */
/*
 * MAC layer capabilities.  These capabilities are handled by the drivers'
 * mc_capab_get() callbacks.  Some capabilities require the driver to fill
 * in a given data structure, and others are simply boolean capabilities.
 * Note that capability values must be powers of 2 so that consumers and
 * providers of this interface can keep track of which capabilities they
 * care about by keeping a bitfield of these things around somewhere.
 */
typedef enum {
	/*
	 * Capabilities reserved for internal use only
	 */
	MAC_CAPAB_VNIC		= 0x0001, /* data is mac_capab_vnic_t */
	MAC_CAPAB_ANCHOR_VNIC	= 0x0002, /* boolean only, no data */
	MAC_CAPAB_AGGR		= 0x0004, /* data is mac_capab_aggr_t */
	MAC_CAPAB_NO_NATIVEVLAN	= 0x0008, /* boolean only, no data */
	MAC_CAPAB_NO_ZCOPY	= 0x0010, /* boolean only, no data */
	MAC_CAPAB_LEGACY	= 0x0020, /* data is mac_capab_legacy_t */
	MAC_CAPAB_VRRP		= 0x0040, /* data is mac_capab_vrrp_t */

	/*
	 * Public Capabilities
	 */
	MAC_CAPAB_HCKSUM	= 0x0100, /* data is a uint32_t */
	MAC_CAPAB_LSO		= 0x0200, /* data is mac_capab_lso_t */
	MAC_CAPAB_RINGS		= 0x0400, /* data is mac_capab_rings_t */
	MAC_CAPAB_MULTIFACTADDR = 0x0800, /* mac_data_multifactaddr_t */
	MAC_CAPAB_SHARES	= 0x1000 /* data is mac_capab_share_t */

	/* add new capabilities here */
} mac_capab_t;


/*
 * LSO capability
 */
typedef struct lso_basic_tcp_ipv4_s {
	t_uscalar_t	lso_max;		/* maximum payload */
} lso_basic_tcp_ipv4_t;

/*
 * Currently supported flags for LSO.
 */
#define	LSO_TX_BASIC_TCP_IPV4	0x01		/* TCP LSO capability */

/*
 * Future LSO capabilities can be added at the end of the mac_capab_lso_t.
 * When such capability is added to the GLDv3 framework, the size of the
 * mac_capab_lso_t it allocates and passes to the drivers increases. Older
 * drivers wil access only the (upper) sections of that structure, that is the
 * sections carrying the capabilities they understand. This ensures the
 * interface can be safely extended in a binary compatible way.
 */
typedef	struct mac_capab_lso_s {
	t_uscalar_t		lso_flags;
	lso_basic_tcp_ipv4_t	lso_basic_tcp_ipv4;
	/* Add future lso capabilities here */
} mac_capab_lso_t;

/*
 * Multiple Factory MAC Addresses Capability
 */
typedef struct mac_capab_multifactaddr_s {
	/*
	 * Number of factory addresses
	 */
	uint_t		mcm_naddr;

	/*
	 * Callbacks to query all the factory addresses.
	 */
	void		(*mcm_getaddr)(void *, uint_t, uint8_t *);
} mac_capab_multifactaddr_t;

/*
 * Info and callbacks of legacy devices.
 */
typedef struct mac_capab_legacy_s {
	/*
	 * Notifications that the legacy device does not support.
	 */
	uint32_t	ml_unsup_note;
	/*
	 * dev_t of the legacy device; can be held to force attach.
	 */
	dev_t		ml_dev;
	boolean_t	(*ml_active_set)(void *);
	void		(*ml_active_clear)(void *);
	int		(*ml_fastpath_disable)(void *);
	void		(*ml_fastpath_enable)(void *);
} mac_capab_legacy_t;

/*
 * MAC driver entry point types.
 */
typedef int		(*mac_getstat_t)(void *, uint_t, uint64_t *);
typedef	int		(*mac_start_t)(void *);
typedef void		(*mac_stop_t)(void *);
typedef int		(*mac_setpromisc_t)(void *, boolean_t);
typedef int		(*mac_multicst_t)(void *, boolean_t, const uint8_t *);
typedef int		(*mac_unicst_t)(void *, const uint8_t *);
typedef void		(*mac_ioctl_t)(void *, queue_t *, mblk_t *);
typedef void		(*mac_resources_t)(void *);
typedef mblk_t		*(*mac_tx_t)(void *, mblk_t *);
typedef	boolean_t	(*mac_getcapab_t)(void *, mac_capab_t, void *);
typedef	int		(*mac_open_t)(void *);
typedef void		(*mac_close_t)(void *);
typedef	int		(*mac_set_prop_t)(void *, const char *, mac_prop_id_t,
			    uint_t, const void *);
typedef	int		(*mac_get_prop_t)(void *, const char *, mac_prop_id_t,
			    uint_t, uint_t, void *, uint_t *);

/*
 * Drivers must set all of these callbacks except for mc_resources,
 * mc_ioctl, and mc_getcapab, which are optional.  If any of these optional
 * callbacks are set, their appropriate flags must be set in mc_callbacks.
 * Any future additions to this list must also be accompanied by an
 * associated mc_callbacks flag so that the framework can grow without
 * affecting the binary compatibility of the interface.
 */
typedef struct mac_callbacks_s {
	uint_t		mc_callbacks;	/* Denotes which callbacks are set */
	mac_getstat_t	mc_getstat;	/* Get the value of a statistic */
	mac_start_t	mc_start;	/* Start the device */
	mac_stop_t	mc_stop;	/* Stop the device */
	mac_setpromisc_t mc_setpromisc;	/* Enable or disable promiscuous mode */
	mac_multicst_t	mc_multicst;	/* Enable or disable a multicast addr */
	mac_unicst_t	mc_unicst;	/* Set the unicast MAC address */
	mac_tx_t	mc_tx;		/* Transmit a packet */
	mac_ioctl_t	mc_ioctl;	/* Process an unknown ioctl */
	mac_getcapab_t	mc_getcapab;	/* Get capability information */
	mac_open_t	mc_open;	/* Open the device */
	mac_close_t	mc_close;	/* Close the device */
	mac_set_prop_t	mc_setprop;
	mac_get_prop_t	mc_getprop;
} mac_callbacks_t;

typedef struct mac_priv_prop_s {
	char	mpp_name[MAXLINKPROPNAME];
	uint_t	mpp_flags;
} mac_priv_prop_t;

/*
 * Virtualization Capabilities
 */
/*
 * The ordering of entries below is important. MAC_HW_CLASSIFIER
 * is the cutoff below which are entries which don't depend on
 * H/W. MAC_HW_CLASSIFIER and entries after that are cases where
 * H/W has been updated through add/modify/delete APIs.
 */
typedef enum {
	MAC_NO_CLASSIFIER = 0,
	MAC_SW_CLASSIFIER,
	MAC_HW_CLASSIFIER
} mac_classify_type_t;

typedef	void	(*mac_rx_func_t)(void *, mac_resource_handle_t, mblk_t *,
    boolean_t);

/*
 * The virtualization level conveys the extent of the NIC hardware assistance
 * for traffic steering employed for virtualization:
 *
 * MAC_VIRT_NONE:	No assist for v12n.
 *
 * MAC_VIRT_LEVEL1:	Multiple Rx rings with MAC address level
 *			classification between groups of rings.
 *			Requires the support of the MAC_CAPAB_RINGS
 *			capability.
 *
 * MAC_VIRT_HIO:	Hybrid I/O capable MAC. Require the support
 *			of the MAC_CAPAB_SHARES capability.
 *
 * MAC_VIRT_SERIALIZE:	Temporary flag *ONLY* for nxge. Mac layer
 *			uses this to enable mac Tx serializer on
 *			outbound traffic and to always enqueue
 * 			incoming traffic on Rx soft rings in mac.
 */
#define	MAC_VIRT_NONE		0x0
#define	MAC_VIRT_LEVEL1		0x1
#define	MAC_VIRT_HIO		0x2
#define	MAC_VIRT_SERIALIZE	0x4

typedef enum {
	MAC_RING_TYPE_RX = 1,	/* Receive ring */
	MAC_RING_TYPE_TX	/* Transmit ring */
} mac_ring_type_t;

#define	MAX_RINGS_PER_GROUP	128

/*
 * Grouping type of a ring group
 *
 * MAC_GROUP_TYPE_STATIC: The ring group can not be re-grouped.
 * MAC_GROUP_TYPE_DYNAMIC: The ring group support dynamic re-grouping
 */
typedef enum {
	MAC_GROUP_TYPE_STATIC = 1,	/* Static ring group */
	MAC_GROUP_TYPE_DYNAMIC		/* Dynamic ring group */
} mac_group_type_t;

typedef	struct __mac_ring_driver	*mac_ring_driver_t;
typedef	struct __mac_group_driver	*mac_group_driver_t;

typedef	struct mac_ring_info_s mac_ring_info_t;
typedef	struct mac_group_info_s mac_group_info_t;

typedef void	(*mac_get_ring_t)(void *, mac_ring_type_t, const int, const int,
    mac_ring_info_t *, mac_ring_handle_t);
typedef void	(*mac_get_group_t)(void *, mac_ring_type_t, const int,
    mac_group_info_t *, mac_group_handle_t);

typedef void	(*mac_group_add_ring_t)(mac_group_driver_t,
    mac_ring_driver_t, mac_ring_type_t);
typedef void	(*mac_group_rem_ring_t)(mac_group_driver_t,
    mac_ring_driver_t, mac_ring_type_t);

/*
 * Multiple Rings Capability
 */
typedef struct	mac_capab_rings_s {
	mac_ring_type_t		mr_type;	/* Ring type: Rx vs Tx */
	mac_group_type_t	mr_group_type;	/* Dynamic vs static grouping */
	uint_t			mr_rnum;	/* Number of rings */
	uint_t			mr_gnum;	/* Number of ring groups */
	mac_get_ring_t		mr_rget;	/* Get ring from driver */
	mac_get_group_t		mr_gget;	/* Get ring group from driver */
	mac_group_add_ring_t	mr_gaddring;	/* Add ring into a group */
	mac_group_rem_ring_t	mr_gremring;	/* Remove ring from a group */
} mac_capab_rings_t;

/*
 * Common ring functions and driver interfaces
 */
typedef	int	(*mac_ring_start_t)(mac_ring_driver_t, uint64_t);
typedef	void	(*mac_ring_stop_t)(mac_ring_driver_t);

typedef	mblk_t	*(*mac_ring_send_t)(void *, mblk_t *);
typedef	mblk_t	*(*mac_ring_poll_t)(void *, int);

typedef struct mac_ring_info_s {
	mac_ring_driver_t	mri_driver;
	mac_ring_start_t	mri_start;
	mac_ring_stop_t		mri_stop;
	mac_intr_t		mri_intr;
	union {
		mac_ring_send_t	send;
		mac_ring_poll_t	poll;
	} mrfunion;
} mac_ring_info_s;

#define	mri_tx			mrfunion.send
#define	mri_poll		mrfunion.poll

typedef	int	(*mac_group_start_t)(mac_group_driver_t);
typedef	void	(*mac_group_stop_t)(mac_group_driver_t);
typedef	int	(*mac_add_mac_addr_t)(void *, const uint8_t *);
typedef	int	(*mac_rem_mac_addr_t)(void *, const uint8_t *);

struct mac_group_info_s {
	mac_group_driver_t	mgi_driver;	/* Driver reference */
	mac_group_start_t	mgi_start;	/* Start the group */
	mac_group_stop_t	mgi_stop;	/* Stop the group */
	uint_t			mgi_count;	/* Count of rings */
	mac_intr_t		mgi_intr;	/* Optional per-group intr */

	/* Only used for rx groups */
	mac_add_mac_addr_t	mgi_addmac;	/* Add a MAC address */
	mac_rem_mac_addr_t	mgi_remmac;	/* Remove a MAC address */
};

/*
 * Share management functions.
 */
typedef uint64_t mac_share_handle_t;

/*
 * Allocate and free a share. Returns ENOSPC if all shares have been
 * previously allocated.
 */
typedef int (*mac_alloc_share_t)(void *, mac_share_handle_t *);
typedef void (*mac_free_share_t)(mac_share_handle_t);

/*
 * Bind and unbind a share. Binding a share allows a domain
 * to have direct access to the groups and rings associated with
 * that share.
 */
typedef int (*mac_bind_share_t)(mac_share_handle_t, uint64_t, uint64_t *);
typedef void (*mac_unbind_share_t)(mac_share_handle_t);

/*
 * Return information on about a share.
 */
typedef void (*mac_share_query_t)(mac_share_handle_t, mac_ring_type_t,
    mac_ring_handle_t *, uint_t *);

/*
 * Basic idea, bind previously created ring groups to shares
 * for them to be exported (or shared) by another domain.
 * These interfaces bind/unbind the ring group to a share.
 * The groups and their rings will be shared with the guest
 * as soon as the share is bound.
 */
typedef int (*mac_share_add_group_t)(mac_share_handle_t,
    mac_group_driver_t);
typedef int (*mac_share_rem_group_t)(mac_share_handle_t,
    mac_group_driver_t);

typedef struct  mac_capab_share_s {
	uint_t			ms_snum;	/* Number of shares (vr's) */
	void			*ms_handle;	/* Handle to driver. */
	mac_alloc_share_t	ms_salloc;	/* Get a share from driver. */
	mac_free_share_t	ms_sfree;	/* Return a share to driver. */
	mac_share_add_group_t	ms_sadd;	/* Add a group to the share. */
	mac_share_rem_group_t	ms_sremove;	/* Remove group from share. */
	mac_share_query_t	ms_squery;	/* Query share constraints */
	mac_bind_share_t	ms_sbind;	/* Bind a share */
	mac_unbind_share_t	ms_sunbind;	/* Unbind a share */
} mac_capab_share_t;

typedef struct mac_capab_vrrp_s {
	/* IPv6 or IPv4? */
	int		mcv_af;
} mac_capab_vrrp_t;

/*
 * MAC registration interface
 */
typedef struct mac_register_s {
	uint_t			m_version;	/* set by mac_alloc() */
	const char		*m_type_ident;
	void			*m_driver;	/* Driver private data */
	dev_info_t		*m_dip;
	uint_t			m_instance;
	uint8_t			*m_src_addr;
	uint8_t			*m_dst_addr;
	mac_callbacks_t		*m_callbacks;
	uint_t			m_min_sdu;
	uint_t			m_max_sdu;
	void			*m_pdata;
	size_t			m_pdata_size;
	uint32_t		m_margin;
	mac_priv_prop_t		*m_priv_props;
	size_t			m_priv_prop_count;
	uint32_t		m_v12n;		/* Virtualization level */
} mac_register_t;

/*
 * Flags for mc_callbacks.  Requiring drivers to set the flags associated
 * with optional callbacks initialized in the structure allows the mac
 * module to add optional callbacks in the future without requiring drivers
 * to recompile.
 */
#define	MC_IOCTL	0x001
#define	MC_GETCAPAB	0x002
#define	MC_OPEN		0x004
#define	MC_CLOSE	0x008
#define	MC_SETPROP	0x010
#define	MC_GETPROP	0x020

/*
 * Driver interface functions.
 */
extern void			mac_sdu_get(mac_handle_t, uint_t *, uint_t *);
extern int			mac_maxsdu_update(mac_handle_t, uint_t);

extern mac_register_t		*mac_alloc(uint_t);
extern void			mac_free(mac_register_t *);
extern int			mac_register(mac_register_t *, mac_handle_t *);
extern int			mac_disable_nowait(mac_handle_t);
extern int			mac_disable(mac_handle_t);
extern int  			mac_unregister(mac_handle_t);
extern void 			mac_rx(mac_handle_t, mac_resource_handle_t,
				    mblk_t *);
extern void 			mac_rx_ring(mac_handle_t, mac_ring_handle_t,
				    mblk_t *, uint64_t);
extern void 			mac_link_update(mac_handle_t, link_state_t);
extern void 			mac_link_redo(mac_handle_t, link_state_t);
extern void 			mac_unicst_update(mac_handle_t,
				    const uint8_t *);
extern void			mac_dst_update(mac_handle_t, const uint8_t *);
extern void			mac_tx_update(mac_handle_t);
extern void			mac_tx_ring_update(mac_handle_t,
				    mac_ring_handle_t);
extern void			mac_capab_update(mac_handle_t);
extern int			mac_pdata_update(mac_handle_t, void *,
				    size_t);
extern void			mac_multicast_refresh(mac_handle_t,
				    mac_multicst_t, void *, boolean_t);
extern void			mac_unicst_refresh(mac_handle_t, mac_unicst_t,
				    void *);
extern void			mac_promisc_refresh(mac_handle_t,
				    mac_setpromisc_t, void *);
extern boolean_t		mac_margin_update(mac_handle_t, uint32_t);
extern void			mac_margin_get(mac_handle_t, uint32_t *);
extern int			mac_margin_remove(mac_handle_t, uint32_t);
extern int			mac_margin_add(mac_handle_t, uint32_t *,
				    boolean_t);
extern void			mac_init_ops(struct dev_ops *, const char *);
extern void			mac_fini_ops(struct dev_ops *);
extern int			mac_devt_to_instance(dev_t);
extern minor_t			mac_private_minor(void);

extern mactype_register_t	*mactype_alloc(uint_t);
extern void			mactype_free(mactype_register_t *);
extern int			mactype_register(mactype_register_t *);
extern int			mactype_unregister(const char *);

extern boolean_t		mac_unicst_verify(mac_handle_t,
				    const uint8_t *, uint_t);

extern int			mac_group_add_ring(mac_group_handle_t, int);
extern void			mac_group_rem_ring(mac_group_handle_t,
				    mac_ring_handle_t);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_MAC_PROVIDER_H */
