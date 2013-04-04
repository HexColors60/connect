
#if defined(KERNEL)

/* --------------------  kernel space provider  ----------------- */

/*** *** *** pragmas *** *** ***/

#pragma message("SYS_NT: kernel space")
#pragma warning(disable:4700)

/*** *** *** defines *** *** ***/

/* versioning */
#define  NDIS50            1
#define  PROTO_NDIS_MAJOR  5
#define  PROTO_NDIS_MINOR  0

/* config */
	/* instance management */
#define INSTANCE_SINGLE             1
#define INSTANCE_MULTI              2
#define CONFIG_INSTANCE             INSTANCE_SINGLE
#define CONFIG_INSTANCE_SINGLE      INSTANCE_SINGLE
	/* management of locks for received packets */
#define PACKETLOCK_DISABLED         1
#define PACKETLOCK_TRANSFER         2
#define PACKETLOCK_RECEIVE          3
#define CONFIG_PACKETLOCK           PACKETLOCK_TRANSFER
#define CONFIG_PACKETLOCK_TRANSFER  PACKETLOCK_TRANSFER

/* includes */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <malloc.h>
#include <errno.h>
#include <sys/types.h>

#include <wdm.h>
#include <ntddk.h>
#include <ndis.h>

/* custom global routine */
#define  ETH_HLEN          14
#define  ETH_ALEN           6         /* Octets in one ethernet addr	 */
#define  ETHER_ADDR_LEN    ETH_ALEN   /* Size of ethernet addr */

#ifndef __func__
#	define __func__ __FUNCTION__  /* stupid MS monkeys cannot into standart defines */
#endif

#define  RSRVD_PCKT_CTX(_p)     ((struct packet_ctx *)((_p)->ProtocolReserved))
#define  TRANSMIT_PACKETS       128
#define  MTU                    1514

/* ioctl related defines */
#define  SIOCTL_TYPE            40000
#define  IOCTL_SAMPLE_BFD       CTL_CODE(SIOCTL_TYPE, 0x801, METHOD_BUFFERED , FILE_ANY_ACCESS)
#define  IOCTL_SAMPLE_DIO_IN    CTL_CODE(SIOCTL_TYPE, 0x802, METHOD_IN_DIRECT, FILE_ANY_ACCESS)

/* inet proto related constants */
#define  IPPROTO_ICMP       1   /* control message protocol      */
#define  IPPROTO_TCP        6   /* transmission control protocol */
#define  IPPROTO_UDP       17   /* user datagram protocol        */

/* values */
#define  NT_OK                 STATUS_SUCCESS
#define  NT_ERR                STATUS_UNSUCCESSFUL
#define  ND_OK                 NDIS_STATUS_SUCCESS
#define  IS_ND_OK(   r)        (r == NDIS_STATUS_SUCCESS)
#define  IS_NT_OK(   r)        NT_SUCCESS(r)
#define  IS_NT_ERR(  r)        NT_ERROR  (r)                      /* http://msdn.microsoft.com/en-us/library/windows/hardware/ff565436%28v=vs.85%29.aspx */
#define  IRP_ASBUF(  i)        (i->AssociatedIrp.SystemBuffer)
#define  IRP_IBLEN(  s)        (s->Parameters.DeviceIoControl.InputBufferLength)
#define  IRP_RBLEN(  s)        (s->Parameters.Read.Length)
#define  IRP_WBLEN(  s)        (s->Parameters.Write.Length)
#define  IRP_IOCTL(  s)        (s->Parameters.DeviceIoControl.IoControlCode)
#define  IRP_DONE(  rp, info, ret) \
		rp->IoStatus.Status = ret; rp->IoStatus.Information = info; IoCompleteRequest(rp, IO_NO_INCREMENT);
#define  SL_FNBUF(   s)        (s->FileObject->FileName.Buffer)
#define  DEV_NOT_OPEN(d)       (!d || !(d->lock_init) || !(d->lock_open))
#define  DEV_NOT_READY(d)      (!d || !(d->lock_init) || !(d->lock_open) || !(d->lock_ready))

/* functions */

	/* basic */
#define nt_malloc(             len     )    ExAllocatePool(                NonPagedPool, len       )
#define nt_free(               src     )    ExFreePool(                             src            )
#define nt_memzero(            src, len)    RtlZeroMemory(                          src, len       )
#define nt_memcpy(        dst, src, len)    RtlCopyMemory(                 dst,     src, len       )
#define nt_ustring_init(  dst, src     )    RtlInitUnicodeString(          dst,     src            )
#define nt_ustring_catu(  dst, src     )    RtlAppendUnicodeStringToString(dst,     src            )
#define nt_ustring_catw(  dst, src     )    RtlAppendUnicodeToString(      dst,     src            )
#define nt_creat_link(    dst, src     )    IoCreateSymbolicLink(          dst,     src            )
#define nt_irp_complete(       ir      )    IoCompleteRequest(              ir,     IO_NO_INCREMENT)
#define nt_irp_get_stack(      ir      )    IoGetCurrentIrpStackLocation(   ir                     )
#define nt_unlink_link(        str     )    IoDeleteSymbolicLink(          str                     )
#define nt_unlink_dev(         dev     )    IoDeleteDevice(                dev                     )
#define nt_splock_init(  lock          )    KeInitializeSpinLock(         lock                     )
#define nt_splock_lock(  lock, i       )    KeAcquireSpinLock(            lock,     i              )
#define nt_splock_unlock(lock, i       )    KeReleaseSpinLock(            lock,     i              )
#define nd_splock_init(  lock          )    NdisAllocateSpinLock(         lock                     )
#define nd_splock_lock(  lock          )    NdisAcquireSpinLock(          lock                     )
#define nd_splock_unlock(lock          )    NdisReleaseSpinLock(          lock                     )
#define nd_splock_free(  lock          )    NdisFreeSpinLock(             lock                     )

#define ATOM_LOCK       nt_splock_lock(   &g_splock, &g_irq );
#define ATOM_UNLOCK     nt_splock_unlock( &g_splock, g_irq  );

#define ATOM(action)    nt_splock_lock(   &g_splock, &g_irq ); action; nt_splock_unlock(&g_splock, g_irq);

#define LOCK_PACKET     printm("LOCK: PACKET"  ); nt_splock_lock(   &g_splock_p, &g_irq_p );
#define UNLOCK_PACKET   printm("UNLOCK: PACKET"); nt_splock_unlock( &g_splock_p, g_irq_p  );

	/* complex */
#define nt_creat(mod, name, dev) \
	IoCreateDevice(mod, 0, name, FILE_DEVICE_TRANSPORT, FILE_DEVICE_SECURE_OPEN, false, dev)

/* routine */
#define  PROTONAME  "cpf"
#define LPROTONAME  "cpf"
#define  MODNAME    "cpf"
#define LMODNAME    "cpf"

#ifndef RELEASE /* debug output routine */
#	define printk(  fmt, ...   )    DbgPrint(fmt, __VA_ARGS__)
#	define printm(    m        )    DbgPrint("%s: %s: %d: %s\n"   ,                       MODNAME, __func__, __LINE__, m          )
#	define printl                   DbgPrint("%s: %s: %d: "       ,                       MODNAME, __func__, __LINE__             )
#	define printmd(   m, d     )    DbgPrint("%s: %s: %d: %s: %d (0x%08X)\n",             MODNAME, __func__, __LINE__, m, d, d    )
#	define printdbg(fmt, ...   )    DbgPrint("%s: %s: %d: " ## fmt,                       MODNAME, __func__, __LINE__, __VA_ARGS__)
#	define DBG_IN                   DbgPrint("%s: == >> %s: %d\n" ,                       MODNAME, __func__, __LINE__             )
#	define DBG_OUT                  DbgPrint("%s: << == %s: %d\n" ,                       MODNAME, __func__, __LINE__             )
#	define DBG_OUT_V                DbgPrint("%s: << == %s: %d\n" ,                       MODNAME, __func__, __LINE__             ); return
#	define DBG_OUT_VM(    m    )    DbgPrint("%s: << == %s: %d: %s\n" ,                   MODNAME, __func__, __LINE__, m          ); return

/* Return */
#	define DBG_OUT_R(       r  )    DbgPrint("%s: << == %s: %d\n" ,                       MODNAME, __func__, __LINE__             ); return r
/* Return value + Print value */
#	define DBG_OUT_RP(      r  )    DbgPrint("%s: << == %s: %d: ret = %d (0x%08X)\n",     MODNAME, __func__, __LINE__, r, r       ); return r
/* return Void, but Print returned value from the previous call */
#	define DBG_OUT_VP(      r  )    DbgPrint("%s: << == %s: %d: ret = %d (0x%08X)\n",     MODNAME, __func__, __LINE__, r, r       ); return
/* Return value, Print returned value and additional Message with information */
#	define DBG_OUT_RPM(  m, r  )    DbgPrint("%s: << == %s: %d: %s: ret = %d (0x%08X)\n", MODNAME, __func__, __LINE__, m, r, r    ); return r
/* return Void, Print returned value and additional Message with information */
#	define DBG_OUT_VPM(  m, r  )    DbgPrint("%s: << == %s: %d: %s: ret = %d (0x%08X)\n", MODNAME, __func__, __LINE__, m, r, r    ); return

#else /* silence in release version */

#	define printk(    fmt, ... )
#	define printm(      m      )
#	define printl
#	define printmd(     m, d   )
#	define DBG_IN
#	define DBG_OUT
#	define DBG_OUT_V                return
#	define DBG_OUT_VM(  m      )    return
#	define DBG_OUT_R(      r   )    return r
#	define DBG_OUT_RP(     r   )    return r
#	define DBG_OUT_VP(     r   )    return
#	define DBG_OUT_RPM( m, r   )    return r
#	define DBG_OUT_VPM( m, r   )    return
#	define printdbg(  fmt, ... )

#endif

#define  RET_ON_ERRND_VPM( m, r    )    if (!(IS_ND_OK(r)))   { DBG_OUT_VPM(m, r);                }
#define  RET_ON_ERRND_RPM( m, r    )    if (!(IS_ND_OK(r)))   { DBG_OUT_RPM(m, r);                }
#define  RET_ON_ERRND_VP(     r    )    if (!(IS_ND_OK(r)))   { DBG_OUT_VP(    r);                }
#define  RET_ON_ERRNT_VP(     r    )    if (!(NT_SUCCESS(r))) { DBG_OUT_VP(    r);                }
#define  RET_ON_ERRNT_RP(     r    )    if (!(NT_SUCCESS(r))) { DBG_OUT_RP(    r);                }
#define  RET_ON_V_RP(         r, v )    if (r != v)           { DBG_OUT_RP(    r);                }
#define  RET_ON_V_VM(   v, m       )    if (v)                { DBG_OUT_VM( m   );                }
#define  RET_ON_V_RPM(  v, m, r    )    if (v)                { DBG_OUT_RPM(m, r);                }
#define  RET_ON_V_VPM(  v, m, r    )    if (v)                { DBG_OUT_VPM(m, r);                }
#define  RET_ON_NULL(      ptr     )    if (!ptr)             { printm("ENOMEM" ); return NT_ERR; }

/* TODO: fix it */
#define IRP_IFACE 1
#define IFACE_LEN 38

/*** *** *** data types *** *** ***/


/*** custom types ***/


/* defining standart C types */


#ifndef true
#define true  TRUE
#endif
#ifndef false
#define false FALSE
#endif


typedef signed char     int8_t;
typedef signed short    int16_t;
typedef signed int      int32_t;
typedef unsigned char   uint8_t;
typedef unsigned short  uint16_t;
typedef unsigned int    uint32_t;

typedef unsigned char   u_char;
typedef unsigned short  u_short;
typedef unsigned int    u_int;
typedef unsigned long   u_long;

typedef unsigned char   uchar;
typedef unsigned short  ushort;
typedef unsigned int    uint;
typedef unsigned long   ulong;


/* redefining existing nt types */


	/* simple */
typedef  NTSTATUS                       nt_ret;
typedef  NDIS_STATUS                    nd_ret;
	/* complex */
typedef  NET_PNP_EVENT                  nd_pnp_ev;
typedef  UNICODE_STRING                 ustring;
typedef  LARGE_INTEGER                  lrgint;
typedef  DRIVER_OBJECT                  mod_obj;   /* PDRIVER_OBJECT       *mod_obj   */
typedef  DEVICE_OBJECT                  dev_obj;   /* PDEVICE_OBJECT       *dev_obj   */
typedef  MDL                            mdl;       /* PMDL                 *mdl       */
typedef  IRP                            irp;       /* PIRP                 *irp       */
typedef  IO_STACK_LOCATION              io_stack;  /* PIO_STACK_LOCATION   *io_stack  */
typedef  FILE_OBJECT                    file_obj;  /* PFILE_OBJECT         *file_obj  */
typedef  NDIS_STRING                    nd_str;
typedef  NDIS_PACKET                    nd_pack;
typedef  NDIS_BUFFER                    nd_buf;
typedef  NDIS_HANDLE                    nd_hndl;
typedef  NDIS_EVENT                     nd_ev;
typedef  NDIS_REQUEST                   nd_req;
typedef  NDIS_MEDIUM                    nd_medm;
typedef  NDIS_PROTOCOL_CHARACTERISTICS  nd_proto;
typedef  LIST_ENTRY                     lent;
typedef  KSPIN_LOCK                     nt_splock;
typedef  NDIS_SPIN_LOCK                 nd_splock;
typedef  KIRQL                          irq;
typedef  NDIS_PHYSICAL_ADDRESS          nd_phyaddr;


/*** custom structs ***/


struct timeval {
        long    tv_sec;         ///< seconds
        long    tv_usec;        ///< microseconds
};


/* This is a name for the 48 bit ethernet address available on many systems. */
struct ether_addr {
	uint8_t  ether_addr_octet[ETH_ALEN];
}; /*__attribute__ ((__packed__))*/


/* 10Mb/s ethernet header */
struct ether_header {
	uint8_t  ether_dhost[ETH_ALEN];  /* destination eth addr */
	uint8_t  ether_shost[ETH_ALEN];  /* source ether addr    */
	uint16_t ether_type;             /* packet type ID field */
} /*__attribute__ ((__packed__))*/;


struct in_addr {
    unsigned long s_addr;                // load with inet_pton()
};


struct ip {
//#if __BYTE_ORDER == __LITTLE_ENDIAN
	unsigned int   ip_hl:4;  /* header length */
	unsigned int   ip_v:4;   /* version */
//#endif
/*
#if __BYTE_ORDER == __BIG_ENDIAN
	unsigned int ip_v:4;
	unsigned int ip_hl:4;
#endif
*/
	uint8_t        ip_tos;   /* type of service */
	u_short        ip_len;   /* total length */
	u_short        ip_id;    /* identification */
	u_short        ip_off;   /* fragment offset field */
#define IP_RF          0x8000    /* reserved fragment flag */
#define IP_DF          0x4000    /* dont fragment flag */
#define IP_MF          0x2000    /* more fragments flag */
#define IP_OFFMASK     0x1fff    /* mask for fragmenting bits */
	uint8_t        ip_ttl;   /* time to live */
	uint8_t        ip_p;     /* protocol */
	u_short        ip_sum;   /* checksum */
	struct in_addr ip_src;   /* source address */
	struct in_addr ip_dst;   /* dest address */
};


struct packet_ctx {
	lent        entry;
	irp        *rp;
	void       *pbuf;
	ulong       pbuf_len;
	void       *phdr_buf;
	ulong       phdr_buf_len;
	mdl        *m;
};


/* TODO: fix user irp management arch */
#define  LEN_DEV         ( 7 * sizeof(wchar_t))
#define  LEN_IFACE       (38 * sizeof(wchar_t))
//#define  LEN_IFACE_DEV   (46 * sizeof(wchar_t))
#define  LEN_IFACE_NAME  (LEN_IFACE + sizeof(wchar_t))
#define  LEN_IFACE_PATH  (LEN_DEV + LEN_IFACE_NAME)
#define  STR_DEV_LEN     LEN_IFACE
#define  PACKET_SIZE     64 * 1024


struct dev_ctx {
	wchar_t          path[LEN_IFACE_PATH];
	wchar_t          name[LEN_IFACE_NAME];
	nd_hndl          hndl;
	nd_hndl          buffer_pool;
	nd_hndl          packet_pool;
	uchar           *packet;
	int              packet_rq;
	int              packet_len;
	nd_ret           status;
	/* TODO: lock_ should be removed */
	int              lock_init;  /* lock on init in dev_open               */
	int              lock_open;  /* lock on NdisOpenAdapter in iface_open  */
	int              lock_ready; /* lock on SIOCSIFADDR in dev_ioctl       */
	/* TODO: l_ should be implemented */
	nd_splock        l_rdrv;     /* ndis spin lock for dev_read/ndis_recv  */
	nd_splock        l_wrsd;     /* ndis spin lock for dev_write/ndis_send */
	int              counter;
	long volatile   *total_packets;
	long volatile   *read_packets;
};


struct io_buf {
	int flag;
	uchar data[8];
};


/*** *** *** declarations of functions (code map) *** *** ***/


/* routine */
int      gettimeofday       (struct timeval *dst);

/* all this code - for this two functions */
void     ndis_packet_recv   (struct dev_ctx *dctx, const uchar *packet, int len);
void     ndis_packet_send   (struct dev_ctx *dctx, const uchar *packet, int len);

/* ndis proto callbacks */
void     ndis_status        (nd_hndl protobind_ctx  , nd_ret      s        , void    *sbuf, uint sbuf_len);
void     ndis_status_cmplt  (nd_hndl protobind_ctx);
void     ndis_iface_open    (nd_hndl protobind_ctx  , nd_ret      s        , nd_ret   err);
void     ndis_iface_bind    (nd_ret *s              , nd_hndl     bind_ctx , nd_str  *devname, void *ss1, void *ss2);
void     ndis_iface_unbind  (nd_ret *s              , nd_hndl     bind_ctx , nd_hndl *unbind_ctx);
void     ndis_iface_close   (nd_hndl protobind_ctx  , nd_ret      s);
void     ndis_reset         (nd_hndl protobind_ctx  , nd_ret      s);
void     ndis_send          (nd_hndl protobind_ctx  , nd_pack    *npacket  , nd_ret   s);
nt_ret   ndis_recv          (nd_hndl protobind_ctx  , nd_hndl     mac_ctx  , void    *hdr_buf, uint hdr_buf_len, void *labuf, uint labuf_len, uint psize);
void     ndis_recv_pckt     (nd_hndl protobind_ctx  , nd_pack    *npacket);
void     ndis_recv_cmplt    (nd_hndl protobind_ctx);
void     ndis_transfer      (nd_hndl protobind_ctx  , nd_pack    *tx_packet, nd_ret   ret, uint tx);
void     ndis_request       (nd_hndl protobind_ctx  , nd_req     *nreq     , nd_ret   s);
void     ndis_pnp           (nd_hndl protobind_ctx  , nd_pnp_ev  *pev);
void     ndis_unload        (void);

/* device helpers */
nt_ret   dev_check    (io_stack  *sl, irp *i, uchar *ubuf, ulong *ulen, int IRP_MJ_TYPE);

/* device callbacks */
nt_ret   dev_open     (dev_obj *dobj, irp *i)           ;
nt_ret   dev_read     (dev_obj *dobj, irp *i)           ;
nt_ret   dev_write    (dev_obj *dobj, irp *i)           ;
nt_ret   dev_ioctl    (dev_obj *dobj, irp *i)           ;
nt_ret   dev_close    (dev_obj *dobj, irp *i)           ;

/* interface management */
nt_ret   iface_open   (struct dev_ctx *dctx)            ;
void     iface_close  (struct dev_ctx *dctx)            ;

/* init/exit routine */
nt_ret   init_ndis    (mod_obj *mobj)                   ;
nt_ret   init_device  (mod_obj *mobj)                   ;
void     exit_ndis    (mod_obj *mobj)                   ;
void     exit_device  (mod_obj *mobj)                   ;
void     exit_module  (mod_obj *mobj)                   ;
nt_ret   init_module  (mod_obj *mobj)                   ;

/* test stub */
void     init_sending (struct dev_ctx *dev)             ;

/* mainline entry point:
nt_ret   DriverEntry  (mod_obj *mobj, ustring *regpath) ;
*/


/*** *** *** global variables *** *** ***/


dev_obj           *g_device;
nd_str             g_dev_prefix = NDIS_STRING_CONST("\\Device");
/* TODO: rename to cpf, update LEN_ macros */
const wchar_t      g_devpath[] = L"\\Device\\myDevice1";     // Define the device
const wchar_t      g_devlink[] = L"\\DosDevices\\myDevice1"; // Symlink for the device
/* TODO: move some g_ vars in dev_ctx */
nd_hndl            g_proto_hndl;
nd_ev              g_closew_event;
nt_splock          g_splock;
irq                g_irq;
nt_splock          g_splock_p;
irq                g_irq_p;
nd_phyaddr         g_phymax = NDIS_PHYSICAL_ADDRESS_CONST(-1,-1);

#define LIST_MAX 8
struct dev_ctx    *g_dev_list[LIST_MAX];
long   volatile   *g_st_packets = 0;

/*** *** *** helper functions *** *** ***/


int gettimeofday(struct timeval *tv)
{
	lrgint system_time;
	lrgint local_time;
	
	KeQuerySystemTime(&system_time);
	ExSystemTimeToLocalTime(&system_time, &local_time);
	
	tv->tv_sec  = (long) (local_time.QuadPart / 10000000 - 11644473600);
	tv->tv_usec = (long)((local_time.QuadPart % 10000000) / 10);
	
	return 0;
}


/*** *** *** mainline functions *** *** ***/


void ndis_packet_recv(struct dev_ctx *dctx, const uchar *packet, int len)
{
	DBG_IN;

#ifdef CONFIG_PACKETLOCK_RECEIVE
	LOCK_PACKET;
#endif
	/* ndis_lock rdrv*/
	nt_memzero(dctx->packet, PACKET_SIZE);
	memcpy(dctx->packet, packet, len);
	dctx->packet_len = len;
	/* ndis_release rdrv*/
#ifdef CONFIG_PACKETLOCK_RECEIVE
	UNLOCK_PACKET;
#endif
	DBG_OUT_V;
}


void ndis_packet_send(struct dev_ctx *dctx, const uchar *packet, int len)
{
	nd_ret    ret;
	nd_pack  *npacket;
	void     *pbuf;
	nd_buf   *nbuf;
	
	DBG_IN;
	
	/* TODO: fix interface ready detection */
	RET_ON_V_VM((!dctx || !(dctx->hndl) || !(dctx->lock_init) || !(dctx->lock_open) || !(dctx->lock_ready) || !packet), "iface is not ready for sending");
	
	NdisAllocatePacket(&ret, &npacket, dctx->packet_pool);
	RET_ON_V_VPM((!(IS_ND_OK(ret))), "NdisAllocatePacket", ret);
	
	/* aquire lock, release only when send is complete */
	nt_splock_lock(&g_splock, &g_irq);
	
	NdisAllocateMemory(&pbuf, len, 0, g_phymax);
	memcpy(pbuf, (void *) packet, len);
	NdisAllocateBuffer(&ret, &nbuf, dctx->buffer_pool, pbuf, len);
	if (!(IS_ND_OK(ret))) {
		nt_splock_unlock(&g_splock, g_irq);
		DBG_OUT_VPM("NdisAllocateBuffer", ret);
	}
	
	/* mark IRP for ndis_send as local */
	RSRVD_PCKT_CTX(npacket)->rp = NULL;
	NdisChainBufferAtBack(npacket, nbuf);
	NdisSend(&ret, dctx->hndl, npacket);
	if (ret != NDIS_STATUS_PENDING) {
		ndis_send(dctx, npacket, ret);
	}
	
	/* release lock, can send next now */
	nt_splock_unlock(&g_splock, g_irq);
	DBG_OUT_V;
}


/*** *** *** ndis proto callbacks *** *** ***/


void ndis_status(nd_hndl protobind_ctx, nd_ret s, void *sbuf, uint sbuf_len)
{
	DBG_IN;
	DBG_OUT_V;
}


void ndis_status_cmplt(nd_hndl protobind_ctx)
{
	DBG_IN;
	DBG_OUT_V;
}


void ndis_iface_open(nd_hndl protobind_ctx, nd_ret s, nd_ret err)
{
	nd_ret ret;
	nd_ret ret_req;
	nd_req ndis_req;
	ulong  pcktype = NDIS_PACKET_TYPE_PROMISCUOUS; // NDIS_PACKET_TYPE_ALL_LOCAL;
	struct dev_ctx *dctx = (struct dev_ctx *) protobind_ctx;
	
	DBG_IN;
	
	RET_ON_ERRNT_VP(s);
	
	ndis_req.RequestType                                   =  NdisRequestSetInformation     ;
	ndis_req.DATA.SET_INFORMATION.Oid                      =  OID_GEN_CURRENT_PACKET_FILTER ;
	ndis_req.DATA.SET_INFORMATION.InformationBuffer        =  &pcktype                      ;
	ndis_req.DATA.SET_INFORMATION.InformationBufferLength  =  sizeof(ulong)                 ;
	
	NdisRequest(&ret_req, dctx->hndl, &ndis_req);
	
	NdisAllocatePacketPool(&ret, &(dctx->packet_pool), TRANSMIT_PACKETS, sizeof(struct packet_ctx));
	RET_ON_ERRND_VPM("NdisAllocatePacketPool: error", ret);
	
	NdisAllocateBufferPool(&ret, &(dctx->buffer_pool), TRANSMIT_PACKETS);
	RET_ON_ERRND_VPM("NdisAllocateBufferPool: error", ret);
	
	DBG_OUT_V;
}


void ndis_iface_bind(nd_ret *s, nd_hndl bind_ctx, nd_str *devname, void *ss1, void *ss2)
{
	DBG_IN;
	DBG_OUT_V;
}


void ndis_iface_unbind(nd_ret *s, nd_hndl bind_ctx, nd_hndl *unbind_ctx)
{
	DBG_IN;
	DBG_OUT_V;
}


void ndis_iface_close(nd_hndl protobind_ctx, nd_ret s)
{
	DBG_IN;
	NdisSetEvent(&g_closew_event);
	DBG_OUT_V;
}


void ndis_reset(nd_hndl protobind_ctx, nd_ret s)
{
	DBG_IN;
	DBG_OUT_V;
}


void ndis_send(nd_hndl protobind_ctx, nd_pack *npacket, nd_ret s)
{
	nd_buf *nbuf;
	void   *pbuf;
	uint    blen;
	irp    *i;
	
	DBG_IN;
	
	/* TODO: verify non-RC */
	nt_splock_lock(&g_splock, &g_irq);
	
	i = RSRVD_PCKT_CTX(npacket)->rp;
	if (i) {
		NdisReinitializePacket(npacket);
		NdisFreePacket(npacket);
		IRP_DONE(i, 0, ND_OK);
	} else {
		NdisUnchainBufferAtFront(npacket, &nbuf);
		if (nbuf) {
			NdisQueryBuffer(nbuf, &pbuf, &blen);
			if (pbuf) {
				NdisFreeMemory(pbuf, blen, 0);
			}
			NdisFreeBuffer(nbuf);
		}
		NdisReinitializePacket(npacket);
		NdisFreePacket(npacket);
	}
	
	nt_splock_unlock(&g_splock, g_irq);
	
	DBG_OUT_V;
}


nt_ret ndis_recv(nd_hndl protobind_ctx, nd_hndl mac_ctx, void *hdr_buf, uint hdr_buf_len, void *labuf, uint labuf_len, uint psize)
{
	void            *mbuf;
	nd_pack         *npacket;
	nd_buf          *nbuf;
	nd_ret           ret;
	ulong            blen;
	ulong            tx_size  = 0;
	uint             tx_bytes = 0;
	struct dev_ctx  *dctx = (struct dev_ctx *) protobind_ctx;
	
	DBG_IN;
	
	tx_size = psize;
	if ((hdr_buf_len > ETH_HLEN) || (tx_size > (MTU - ETH_HLEN))) {
		DBG_OUT_RPM("ndis_recv: packet not accepted", NDIS_STATUS_NOT_ACCEPTED);
	}
	
	/* TODO: refactoring ledder */
	mbuf = nt_malloc(MTU - ETH_HLEN);
	if (mbuf) {
		nt_memzero(mbuf, (MTU - ETH_HLEN));
		
		NdisAllocatePacket(&ret, &npacket, dctx->packet_pool);
		if (NDIS_STATUS_SUCCESS == ret) {
			RSRVD_PCKT_CTX(npacket)->phdr_buf = nt_malloc(ETH_HLEN);
			if (RSRVD_PCKT_CTX(npacket)->phdr_buf) {
				nt_memzero(RSRVD_PCKT_CTX(npacket)->phdr_buf, ETH_HLEN);
				memcpy(RSRVD_PCKT_CTX(npacket)->phdr_buf, (uchar *) hdr_buf, ETH_HLEN);
				RSRVD_PCKT_CTX(npacket)->phdr_buf_len = ETH_HLEN;
				
				NdisAllocateBuffer(&ret, &nbuf, dctx->buffer_pool, mbuf, (MTU - ETH_HLEN));
				if (NDIS_STATUS_SUCCESS == ret) {
					RSRVD_PCKT_CTX(npacket)->pbuf = mbuf;
					
					/* attach the buffer to the packet */
					NdisChainBufferAtFront(npacket, nbuf);
					NdisTransferData(&(dctx->status), dctx->hndl, mac_ctx, 0, tx_size, npacket, &tx_bytes);
					
					if (ret != NDIS_STATUS_PENDING) {
						/* call the complete routine since it's not pending */
						ndis_transfer(dctx, npacket, ret, tx_bytes);
					}
					InterlockedIncrement(g_st_packets);
					InterlockedIncrement(dctx->total_packets);
					/* TODO: ZwWriteFile */
					/* TODO: dev_read_cb */
					DBG_OUT_R(ND_OK);
				}
				nt_free(RSRVD_PCKT_CTX(npacket)->phdr_buf);
			} else {
				printm("nt_malloc: error");
			}
			NdisFreePacket(npacket);
		}
		nt_free(mbuf);
	}
	DBG_OUT_R(ND_OK);
}


void ndis_recv_pckt(nd_hndl protobind_ctx, nd_pack *npacket)
{
	DBG_IN;
	DBG_OUT_V;
}


void ndis_recv_cmplt(nd_hndl protobind_ctx)
{
	DBG_IN;
	DBG_OUT_V;
}


void ndis_transfer(nd_hndl protobind_ctx, nd_pack *tx_packet , nd_ret ret, uint tx)
{
	nd_buf *nbuf;
	void   *tbuf;
	ulong   tbuf_len;
	void   *hdr_tbuf;
	ulong   hdr_tbuf_len;
	struct  dev_ctx *dctx = (struct dev_ctx *) protobind_ctx;
	
	DBG_IN;
	
	tbuf         = RSRVD_PCKT_CTX(tx_packet)->pbuf         ;
	tbuf_len     = tx                                      ;
	hdr_tbuf     = RSRVD_PCKT_CTX(tx_packet)->phdr_buf     ;
	hdr_tbuf_len = RSRVD_PCKT_CTX(tx_packet)->phdr_buf_len ;
#ifdef CONFIG_PACKETLOCK_TRANSFER
	LOCK_PACKET;
#endif
	if (tbuf && hdr_tbuf) {
		uchar *pbuf = NULL;
		pbuf = nt_malloc(hdr_tbuf_len + tbuf_len);
		if (pbuf) {
			memcpy(pbuf, hdr_tbuf, hdr_tbuf_len);
			memcpy(pbuf + hdr_tbuf_len, tbuf, tbuf_len);
			ndis_packet_recv(dctx, pbuf, (hdr_tbuf_len + tbuf_len));
			nt_free(pbuf);
		}
		nt_free(tbuf);
		nt_free(hdr_tbuf);
	}
	
	NdisUnchainBufferAtFront(tx_packet, &nbuf);
	if (nbuf) {
		NdisFreeBuffer(nbuf);
	}
	
	NdisReinitializePacket(tx_packet);
	NdisFreePacket(tx_packet);
#ifdef CONFIG_PACKETLOCK_TRANSFER
	UNLOCK_PACKET;
#endif
	/* TODO: try event */
	while (dctx->packet_rq) {
		continue;
	}
	
	/* packet is gone here */
	/* ATOM? */
#ifdef CONFIG_PACKETLOCK_RECEIVE
	dctx->packet_len = 0;
#endif
	DBG_OUT_V;
}


void ndis_request(nd_hndl protobind_ctx, nd_req *nreq, nd_ret s)
{
	DBG_IN;
	DBG_OUT_V;
}


void ndis_pnp(nd_hndl protobind_ctx, nd_pnp_ev *pnpev)
{
	DBG_IN;
	DBG_OUT_V;
}


void ndis_unload(void)
{
	DBG_IN;
	DBG_OUT_V;
}


/*** *** *** device callbacks *** *** ***/


nt_ret dev_check(io_stack *sl, irp *i, uchar *ubuf, ulong *ulen, int IRP_MJ_TYPE)
{
	struct dev_ctx *dctx = NULL;
	
	if (!sl) {
		DBG_OUT_R(STATUS_INVALID_PARAMETER);
	}
	
	dctx = (struct dev_ctx *)(sl->FileObject->FsContext);
	if (DEV_NOT_READY(dctx)) {
		DBG_OUT_R(STATUS_DEVICE_NOT_READY);
	}
	
	if (i->MdlAddress) {
		printm("direct_IO");
		if ((!(ubuf = MmGetSystemAddressForMdlSafe(i->MdlAddress, NormalPagePriority))) && (i->UserBuffer)) {
			printm("!mdl; user_buffer");
			ubuf = i->UserBuffer;
		}
	}
	
	if (!ubuf) {
		printm("buffered_IO");
		ubuf = (uchar *) IRP_ASBUF(i);
	}
	
	switch(IRP_MJ_TYPE) {
		case IRP_MJ_READ:
			*ulen = IRP_RBLEN(sl);
			break;
		case IRP_MJ_WRITE:
			*ulen = IRP_WBLEN(sl);
			break;
		case IRP_MJ_DEVICE_CONTROL:
			*ulen = IRP_IBLEN(sl);
			break;
		default:
			*ulen = 0;
			break;
	}
	
	if ((!*ulen) || (!ubuf)) {
		DBG_OUT_R(STATUS_INVALID_PARAMETER);
	}
	
	DBG_OUT_R(NT_OK);
}


nt_ret dev_open(dev_obj *dobj, irp *i)
{
	int       n = 0;
	nt_ret    r;
	struct    dev_ctx *dinit, *dctx;
	ustring   ifname_path;
	io_stack *sl = nt_irp_get_stack(i);
	
	DBG_IN;
	
	if (!sl) {
		IRP_DONE(i, 0, STATUS_INVALID_PARAMETER);
		DBG_OUT_R(STATUS_INVALID_PARAMETER);
	}
	
	printdbg("FileName            == %ws\n", sl->FileObject->FileName.Buffer);
	printdbg("sl->FObj->FName.L   == %d\n" , sl->FileObject->FileName.Length);
	printdbg("g_dev_prefix.Length == %d\n" , g_dev_prefix.Length);
	
	/* legacy code for open global device correctly */
	if (sl->FileObject->FileName.Length == 0 && !(sl->FileObject->FileName.Buffer)) {
		IRP_DONE(i, 0, NT_OK);
		DBG_OUT_R(NT_OK);
	}
	
	for (n = 0; n < LIST_MAX; n++) {
		if (g_dev_list[n]) {
			printdbg("g_dev_list[n]->name == %ws\n", g_dev_list[n]->name);
			if (memcmp(g_dev_list[n]->name, sl->FileObject->FileName.Buffer, LEN_IFACE_NAME) == 0) {
#ifdef CONFIG_INSTANCE_SINGLE
				IRP_DONE(i, 0, STATUS_DEVICE_BUSY);
				DBG_OUT_R(STATUS_DEVICE_BUSY);
#else
				#pragma warning("CONFIG_INSTANCE_MULTI")
				g_dev_list[n]->counter++;
				IRP_DONE(i, 0, NT_OK);
				DBG_OUT_R(NT_OK);
#endif
			}
		}
	}
	
	dinit = (struct dev_ctx *)(sl->FileObject->FsContext);
	if (dinit && (dinit->lock_init || dinit->lock_open || dinit->lock_ready)) {
		IRP_DONE(i, 0, STATUS_DEVICE_BUSY);
		DBG_OUT_R(STATUS_DEVICE_BUSY);
	}
	
	if ((sl->FileObject->FileName.Length != LEN_IFACE_NAME) || ((sl->FileObject->FileName.Length + g_dev_prefix.Length) != LEN_IFACE_PATH)) {
		IRP_DONE(i, 0, STATUS_INVALID_PARAMETER);
		DBG_OUT_R(STATUS_INVALID_PARAMETER);
	}
	
	dctx = nt_malloc(sizeof(struct dev_ctx));
	if (!dctx) {
		IRP_DONE(i, 0, STATUS_NO_MEMORY);
		DBG_OUT_R(STATUS_NO_MEMORY);
	}
	nt_memzero(dctx, sizeof(struct dev_ctx));
	
	dctx->lock_init = 1;
	
	printm("preparing buffer for packet");
	dctx->packet = (uchar *) nt_malloc(PACKET_SIZE);
	if (!(dctx->packet)) {
		IRP_DONE(i, 0, STATUS_NO_MEMORY);
		nt_free(dctx);
		dctx = NULL;
		DBG_OUT_R(STATUS_NO_MEMORY);
	}
	nt_memzero(dctx->packet, PACKET_SIZE);
	
	printm("preparing packet stats");
	dctx->total_packets = nt_malloc(sizeof(long volatile));
	dctx->read_packets = nt_malloc(sizeof(long volatile));
	if (!dctx->total_packets || !dctx->read_packets) {
		IRP_DONE(i, 0, STATUS_NO_MEMORY);
		nt_free(dctx->packet);
		nt_free(dctx);
		dctx = NULL;
		DBG_OUT_R(STATUS_NO_MEMORY);
	}
	*(dctx->total_packets) = 0;
	*(dctx->read_packets) = 0;
	
	nt_memcpy(dctx->name, sl->FileObject->FileName.Buffer, LEN_IFACE_NAME);
	g_dev_list[n] = dctx;
	
	ifname_path.Length = 0;
	ifname_path.MaximumLength = (ushort) (LEN_IFACE_PATH + sizeof(UNICODE_NULL));
	ifname_path.Buffer = nt_malloc(ifname_path.MaximumLength);
	nt_memzero(ifname_path.Buffer, ifname_path.MaximumLength);
	
	printm("init adapter name");
	RtlAppendUnicodeStringToString(&ifname_path, &g_dev_prefix);
	RtlAppendUnicodeToString(&ifname_path, sl->FileObject->FileName.Buffer);
	nt_memcpy(dctx->path, ifname_path.Buffer, LEN_IFACE_PATH);
	
	r = iface_open(dctx);
	if (IS_NT_ERR(r)) {
		IRP_DONE(i, 0, r);
		nt_free(dctx);
		nt_free(ifname_path.Buffer);
		g_dev_list[n] = NULL;
		dctx = NULL;
		DBG_OUT_R(r);
	}
	
	dctx->lock_open = 1;
	
	sl->FileObject->FsContext = (void *) dctx;
	
	printdbg("FileName         == %ws\n", sl->FileObject->FileName.Buffer);
	printdbg("ifname_path      == %ws\n", ifname_path.Buffer);
	printdbg("dctx->name       == %ws\n", dctx->name);
	printdbg("dctx->path       == %ws\n", dctx->path);
	
	dctx->lock_ready = 1;
	dctx->counter = 1;
	
	for (n = 0; n < LIST_MAX; n++) {
		if (!(g_dev_list[n])) {
			g_dev_list[n] = dctx;
		}
	}
	
	IRP_DONE(i, 0, NT_OK);
	DBG_OUT_R(NT_OK);
}


nt_ret dev_read(dev_obj *dobj, irp *i)
{
	io_stack       *sl   = NULL;
	struct dev_ctx *dctx = NULL;
	uchar          *rbuf = NULL;
	ulong           rlen = 0;
	int             len  = 0;
	
	DBG_IN;
	
	sl = nt_irp_get_stack(i);
	if (!sl) {
		IRP_DONE(i, 0, STATUS_INVALID_PARAMETER);
		DBG_OUT_R(STATUS_INVALID_PARAMETER);
	}
	
	dctx = (struct dev_ctx *)(sl->FileObject->FsContext);
	if (DEV_NOT_READY(dctx)) {
		IRP_DONE(i, 0, STATUS_DEVICE_NOT_READY);
		DBG_OUT_R(STATUS_DEVICE_NOT_READY);
	}
	
	if (i->MdlAddress) {
		printm("direct_IO");
		if ((!(rbuf = MmGetSystemAddressForMdlSafe(i->MdlAddress, NormalPagePriority))) && (i->UserBuffer)) {
			printm("!mdl; user_buffer");
			rbuf = i->UserBuffer;
		}
	}
	
	if (!rbuf) {
		printm("buffered_IO");
		rbuf = (uchar *) IRP_ASBUF(i);
	}
	
	rlen = IRP_RBLEN(sl);
	if ((!rlen) || (!rbuf)) {
		IRP_DONE(i, 0, STATUS_INVALID_PARAMETER);
		DBG_OUT_R(STATUS_INVALID_PARAMETER);
	}
	
	/* TODO: try event */
	while (!(dctx->packet_len)) {
		continue;
	}
	
	LOCK_PACKET;
	/* ndis_lock rdrv */
	dctx->packet_rq = 1;
	len = (((rlen) <= (dctx->packet_len)) ? (rlen) : (dctx->packet_len));
	nt_memzero(rbuf, rlen);
	nt_memcpy(rbuf, dctx->packet, len);
	dctx->packet_rq = 0;
	InterlockedIncrement(dctx->read_packets);
	/* ndis_release rdrv */
	UNLOCK_PACKET;
	IRP_DONE(i, len, NT_OK);
#ifdef CONFIG_PACKETLOCK_TRANSFER
	dctx->packet_len = 0;
#endif
	DBG_OUT_R(NT_OK);
}


nt_ret dev_write(dev_obj *dobj, irp *i)
{
	io_stack       *sl   = NULL;
	struct dev_ctx *dctx = NULL;
	uchar          *wbuf = NULL;
	ulong           wlen = 0;
	
	DBG_IN;
	
	sl = nt_irp_get_stack(i);
	if (!sl) {
		IRP_DONE(i, 0, STATUS_INVALID_PARAMETER);
		DBG_OUT_R(STATUS_INVALID_PARAMETER);
	}
	
	dctx = (struct dev_ctx *)(sl->FileObject->FsContext);
	if (DEV_NOT_READY(dctx)) {
		IRP_DONE(i, 0, STATUS_DEVICE_NOT_READY);
		DBG_OUT_R(STATUS_DEVICE_NOT_READY);
	}
	
	if (i->MdlAddress) {
		printm("direct_IO");
		if ((!(wbuf = MmGetSystemAddressForMdlSafe(i->MdlAddress, NormalPagePriority))) && (i->UserBuffer)) {
			printm("!mdl; user_buffer");
			wbuf = i->UserBuffer;
		}
	}
	
	if (!wbuf) {
		printm("buffered_IO");
		wbuf = (uchar *) IRP_ASBUF(i);
	}
	
	wlen = IRP_WBLEN(sl);
	if ((!wlen) || (!wbuf)) {
		IRP_DONE(i, 0, STATUS_INVALID_PARAMETER);
		DBG_OUT_R(STATUS_INVALID_PARAMETER);
	}
	
	ndis_packet_send(dctx, wbuf, wlen);
	
	IRP_DONE(i, wlen, NT_OK);
	DBG_OUT_R(NT_OK);
}


/* TODO: refactoring */
nt_ret dev_ioctl(dev_obj *dobj, irp *i)
{
	nt_ret          r    = 0;
	io_stack       *sl   = NULL;
	struct dev_ctx *dctx = NULL;
	ulong          *len;
	uchar          *ibuf = NULL;
	ulong           ilen = 0;
	ulong           icode;
	
	DBG_IN;
	
	sl = nt_irp_get_stack(i);
	/*
	r = dev_check(sl, i, ibuf, len, IRP_MJ_DEVICE_CONTROL)
	if (r != NT_OK) {
		IRP_DONE(i, 0, r);
		DBG_OUT_R(r);
	}
	ilen = *len;
	*/
	if (!sl) {
		IRP_DONE(i, 0, STATUS_INVALID_PARAMETER);
		DBG_OUT_R(STATUS_INVALID_PARAMETER);
	}
	
	dctx = (struct dev_ctx *)(sl->FileObject->FsContext);
	if (DEV_NOT_READY(dctx)) {
		IRP_DONE(i, 0, STATUS_DEVICE_NOT_READY);
		DBG_OUT_R(STATUS_DEVICE_NOT_READY);
	}
	
	if (i->MdlAddress) {
		printm("direct_IO");
		if ((!(ibuf = MmGetSystemAddressForMdlSafe(i->MdlAddress, NormalPagePriority))) && (i->UserBuffer)) {
			printm("!mdl; user_buffer");
			ibuf = i->UserBuffer;
		}
	}
	
	if (!ibuf) {
		printm("buffered_IO");
		ibuf = (uchar *) IRP_ASBUF(i);
	}
	
	ilen = IRP_IBLEN(sl);
	if ((!ilen) || (!ibuf)) {
		IRP_DONE(i, 0, STATUS_INVALID_PARAMETER);
		DBG_OUT_R(STATUS_INVALID_PARAMETER);
	}
	
	/* get ioctl code */
	icode = IRP_IOCTL(sl);
	
	switch (icode) {
		case IOCTL_SAMPLE_BFD:
			printm("IOCTL BFD >>");
			printdbg("ibuf[0] = %02X", ibuf[0]);
			printdbg("ibuf[1] = %02X", ibuf[1]);
			printdbg("ibuf[2] = %02X", ibuf[2]);
			printdbg("ibuf[3] = %02X", ibuf[3]);
			ibuf[0] = 'I';
			ibuf[1] = 'B';
			ibuf[2] = 'F';
			ibuf[3] = 'D';
			if (i->UserBuffer) {
				uchar *u = i->UserBuffer;
				u[0] = 'I';
				u[1] = 'B';
				u[2] = 'F';
				u[3] = 'D';
			}
			IRP_DONE(i, 0, NT_OK);
			printm("IOCTL BFD <<");
			break;
		case IOCTL_SAMPLE_DIO_IN:
			printm("IOCTL DIO IN >>");
			printdbg("ibuf[0] = %02X", ibuf[0]);
			printdbg("ibuf[1] = %02X", ibuf[1]);
			printdbg("ibuf[2] = %02X", ibuf[2]);
			printdbg("ibuf[3] = %02X", ibuf[3]);
			ibuf[0] = 'I';
			ibuf[1] = 'D';
			ibuf[2] = 'I';
			ibuf[3] = 'O';
			IRP_DONE(i, 0, NT_OK);
			printm("IOCTL DIO IN <<");
			break;
		default:
			IRP_DONE(i, 0, STATUS_INVALID_PARAMETER);
			break;
	}
	
	DBG_OUT_R(NT_OK);
}


nt_ret dev_close(dev_obj *dobj, irp *i)
{
	int n = 0;
	int c = 0;
	io_stack       *sl   = nt_irp_get_stack(i);
	struct dev_ctx *dctx = (struct dev_ctx *)(sl->FileObject->FsContext);
	
	DBG_IN;
	
	if (!sl) {
		IRP_DONE(i, 0, STATUS_INVALID_PARAMETER);
		DBG_OUT_R(STATUS_INVALID_PARAMETER);
	}
	
	if (DEV_NOT_READY(dctx)) {
		IRP_DONE(i, 0, STATUS_DEVICE_NOT_READY);
		DBG_OUT_R(STATUS_DEVICE_NOT_READY);
	}
	
	for (n = 0; n < LIST_MAX; n++) {
		if ((g_dev_list[n]) && (memcmp(g_dev_list[n]->name, SL_FNBUF(sl), LEN_IFACE_NAME) == 0)) {
			if (g_dev_list[n]->counter == 1) {
				g_dev_list[n] = NULL;
				c = 1;
			} else {
				g_dev_list[n]->counter--;
				c = 0;
			}
		}
	}
	
	if (!c) {
		/* nothing to close */
		IRP_DONE(i, 0, NT_OK);
		DBG_OUT_R(NT_OK);
	}
	
	iface_close(dctx);
	IRP_DONE(i, 0, NT_OK);
	DBG_OUT_R(NT_OK);
}


/*** *** *** interface management *** *** ***/


void init_sending(struct dev_ctx *dev)
{
	uchar *pckt = nt_malloc(MTU);
	int i = 0; char c = 0;
	DBG_IN;
	
	nt_memzero(pckt, MTU);
	for (i = 6; i < MTU; i++, c++) {
		pckt[i] = c;
	}
	
	memset(pckt, 0xFF, 6);
	ndis_packet_send(dev, pckt, MTU);
	nt_free(pckt);
	
	DBG_OUT_V;
}


nt_ret iface_open(struct dev_ctx *dctx)
{
	nd_ret  ret, err;
	ustring ifname, ifname_path;
	uint    mindex = 0;
	nd_medm marray = NdisMedium802_3; // specify ethernet network
	
	DBG_IN;
	
	printdbg("input: iface_name == %ws\n", dctx->path);
	
	ifname_path.Length        = 0;
	ifname_path.MaximumLength = (ushort) (LEN_IFACE_PATH + sizeof(UNICODE_NULL));
	ifname_path.Buffer        = nt_malloc(ifname_path.MaximumLength);
	nt_memzero(ifname_path.Buffer, ifname_path.MaximumLength);
	
	RtlAppendUnicodeToString(&ifname_path, dctx->path);
	printdbg("output: ifname_path == %ws\n", ifname_path.Buffer);
	
	printm("opening adapter");
	NdisOpenAdapter(&ret, &err, &(dctx->hndl), &mindex, &marray, 1, g_proto_hndl, (nd_hndl)dctx, &ifname_path, 0, NULL);
	if ((!(IS_ND_OK(ret))) && (!(IS_NT_OK(ret)))) {
		printmd("NdisOpenAdapter: error", ret);
		if (ret == NDIS_STATUS_ADAPTER_NOT_FOUND) {
			printm("NdisOpenAdapter: adapter not found");
		}
		DBG_OUT_R(ret);
	}
	
	ndis_iface_open(dctx, ret, ND_OK);
	
	DBG_OUT_R(NT_OK);
}


void iface_close(struct dev_ctx *dctx)
{
	nd_ret ret;
	
	DBG_IN;
	
	if (DEV_NOT_READY(dctx)) {
		printm("BUG_ON?");
		DBG_OUT_VP(STATUS_DEVICE_NOT_READY);
	}
	
	printdbg(" ============= NETSTAT ================");
	printdbg("dctx->total_packets == %ld", *(dctx->total_packets));
	printdbg("dctx->read_packets == %ld", *(dctx->read_packets));
	printdbg("g_st_packets == %ld", *g_st_packets);
	printdbg(" ============= NETSTAT ================");
	
	*(dctx->total_packets) = 0;
	*(dctx->read_packets) = 0;
	
	printm("closing adapter for network interface");
	NdisCloseAdapter(&ret, dctx->hndl);
	if (ret == NDIS_STATUS_PENDING) {
		printm("waiting ndis event");
		NdisWaitEvent(&g_closew_event, 0);
	}
	
	dctx->lock_init  = 0;
	dctx->lock_open  = 0;
	dctx->lock_ready = 0;
	dctx->counter = 0;
	
	NdisFreeBufferPool(dctx->buffer_pool);
	NdisFreePacketPool(dctx->packet_pool);
	
	nt_free(dctx->total_packets);
	dctx->total_packets = NULL;
	nt_free(dctx->read_packets);
	dctx->read_packets = NULL;
	nt_free(dctx->packet);
	dctx->packet = NULL;
	nt_free(dctx);
	dctx = NULL;
	
	DBG_OUT;
}


/*** *** *** init/exit routine *** *** ***/


nt_ret init_ndis(mod_obj *mobj)
{
	nd_ret   ret, err;
	nd_proto proto;
	ustring  ifname;
	uint     mindex = 0;
	
	/* string must match that specified in the registery (under Services) when the protocol was installed */
	nd_str pname   = NDIS_STRING_CONST("cpf"); /* PROTONAME */
	nd_medm marray = NdisMedium802_3;          /* ethernet media */
	
	DBG_IN;
	
	printm("init ndis event");
	NdisInitializeEvent(&g_closew_event);
	
	nt_memzero(&proto, sizeof(nd_proto));
	
	printm("init proto");
	proto.MajorNdisVersion             = PROTO_NDIS_MAJOR    ;
	proto.MinorNdisVersion             = PROTO_NDIS_MINOR    ;
	proto.Name                         = pname               ;
	proto.Reserved                     = 0                   ;
	
	proto.StatusHandler                =  ndis_status        ;
	proto.StatusCompleteHandler        =  ndis_status_cmplt  ;
	
	proto.OpenAdapterCompleteHandler   =  ndis_iface_open    ;
	proto.BindAdapterHandler           =  ndis_iface_bind    ;
	proto.UnbindAdapterHandler         =  ndis_iface_unbind  ;
	proto.CloseAdapterCompleteHandler  =  ndis_iface_close   ;
	
	proto.ResetCompleteHandler         =  ndis_reset         ;
	proto.SendCompleteHandler          =  ndis_send          ;
	proto.ReceiveHandler               =  ndis_recv          ;
	proto.ReceivePacketHandler         =  ndis_recv_pckt     ;
	proto.ReceiveCompleteHandler       =  ndis_recv_cmplt    ;
	proto.TransferDataCompleteHandler  =  ndis_transfer      ;
	proto.RequestCompleteHandler       =  ndis_request       ;
	proto.PnPEventHandler              =  ndis_pnp           ;
	proto.UnloadHandler                =  ndis_unload        ;
	
	printm("register proto");
	NdisRegisterProtocol(&ret, &g_proto_hndl, &proto, sizeof(nd_proto));
	RET_ON_ERRND_RPM("NdisRegisterProtocol: error", ret);
	
	printm("init global spinlock");
	nt_splock_init(&g_splock);
	nt_splock_init(&g_splock_p);
	
	g_st_packets = nt_malloc(sizeof(long volatile));
	*g_st_packets = 0;
	
	DBG_OUT_R(NT_OK);
}


nt_ret init_device(mod_obj *mobj)
{
	int      i = 0;
	nt_ret   ret;
	ustring  devname_path;
	ustring  devname_link;
	
	DBG_IN;
	
	printm("init strings for devices");
	nt_ustring_init(&devname_path, g_devpath);
	nt_ustring_init(&devname_link, g_devlink);
	
	printm("creating device");
	ret = nt_creat(mobj, &devname_path, &g_device);
	RET_ON_ERRNT_RP(ret);
	
	printm("setting O_DIRECT for created device");
	g_device->Flags |= DO_DIRECT_IO;
	
	/*
	printm("setting O_BUFFER for created device");
	g_device->Flags |= DO_BUFFERED_IO;
	*/
	
	printm("creating symlink for device");
	ret = nt_creat_link(&devname_link, &devname_path);
	RET_ON_ERRNT_RP(ret);
	
	printm("init device callbacks");
	mobj->MajorFunction[IRP_MJ_CREATE]         = dev_open  ;
	mobj->MajorFunction[IRP_MJ_READ]           = dev_read  ;
	mobj->MajorFunction[IRP_MJ_WRITE]          = dev_write ;
	mobj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = dev_ioctl ;
	mobj->MajorFunction[IRP_MJ_CLOSE]          = dev_close ;
	
	for (i = 0; i < LIST_MAX; i++) {
		g_dev_list[i] = NULL;
	}
	
	DBG_OUT_R(NT_OK);
}


/*** *** *** clean up on exit routine *** *** ***/


void exit_ndis(mod_obj *mobj)
{
	int    i = 0;
	nd_ret ret;
	
	DBG_IN;
	
	printm("resetting ndis event");
	NdisResetEvent(&g_closew_event);
	
	for (i = 0; i < LIST_MAX; i++) {
		if (g_dev_list[i]) {
			printm("closing remaining opened adapters");
			iface_close(g_dev_list[i]);
		}
	}
	
	printm("disabling protocol");
	NdisDeregisterProtocol(&ret, g_proto_hndl);
	
	if (!(IS_NT_OK(ret))) {
		printm("NdisDeregisterProtocol: error");
	}
	
	DBG_OUT;
}


void exit_device(mod_obj *mobj)
{
	ustring devname_link;
	
	DBG_IN;
	
	printm("init string for device");
	nt_ustring_init(&devname_link, g_devlink);
	
	printm("removing device link");
	nt_unlink_link(&devname_link);
	
	printm("removing device object");
	nt_unlink_dev(mobj->DeviceObject);
	
	DBG_OUT;
}


void exit_module(mod_obj *mobj)
{
	DBG_IN;
	
	printm("cleaning up ndis");
	exit_ndis(mobj);
	
	printm("cleaning up devices");
	exit_device(mobj);
	
	DBG_OUT;
}


/*** *** *** main driver entry point *** *** ***/


nt_ret init_module(mod_obj *mobj)
{
	nt_ret ret;
	
	DBG_IN;
	
	printm("init unload module callback");
	mobj->DriverUnload = exit_module;
	
	printm("init device");
	ret = init_device(mobj);
	RET_ON_ERRNT_RP(ret);
	
	printm("init ndis");
	ret = init_ndis(mobj);
	RET_ON_ERRNT_RP(ret);
	
	DBG_OUT_R(NT_OK);
}


nt_ret DriverEntry(mod_obj *mobj, ustring *regpath)
{
	DBG_IN;
	DBG_OUT_R(init_module(mobj));
}

#else

/* --------------------  user space library calls  ------------------- */

#pragma message("SYS_NT: user space")

/* includes */
#include <stdio.h>
#include <windows.h>
#include <winioctl.h>
#include <stdlib.h>
#include <string.h>

/* ioctl */
#define  SIOCTL_TYPE           40000
#define  IOCTL_SAMPLE_BFD      CTL_CODE(SIOCTL_TYPE, 0x801, METHOD_BUFFERED , FILE_ANY_ACCESS)
#define  IOCTL_SAMPLE_DIO_IN   CTL_CODE(SIOCTL_TYPE, 0x802, METHOD_IN_DIRECT, FILE_ANY_ACCESS)

/* len for device names */
#define  STR_DEV_LEN    38*sizeof(WCHAR)
#define  LEN_IFACE_DEV  46*sizeof(WCHAR)
#define  LEN_IFACE      38*sizeof(WCHAR)

/* len for network routine */
#define  ETH_ALEN       6         /* Octets in one ethernet addr */
#define  PACKET_SIZE    64*1024
#define  MTU            1514

/*
 * TODO:
 * - fix warnings
 * - clean up define section
 * - implement dev_write for sending
 * - split IRP/SL defines
 * - check and clean up duplicate/old/unused code
 * - critical sections for packet management
 * - merging code with connect/ tree, integrating into build process
 * - adding autodetect of ifaces in user space
 * - adding file support in user/kernel space
 * - scripts for autobuild(nmake)/autosign(see msdn)
 */

struct user_irp {
	int irp_type;
	wchar_t irp_data[STR_DEV_LEN];
};

int connect_packet_filter_test(void)
{
	HANDLE hDevice;
	HANDLE hInstance;
	int i;
	int c;
	int n;
	int ulen = 0;
	int out_len = 0;
	unsigned char *ubuf;
	unsigned char *out;
	out = (void *) malloc(PACKET_SIZE);
	ubuf = (void *) malloc(PACKET_SIZE);
	if (!ubuf) {
		printf("MALLOC enomem\n");
		return 4;
	}
	
	hDevice = CreateFile("\\\\.\\myDevice1\\{BDB421B0-4B37-4AA2-912B-3AA05F8A0829}", GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	printf("Handle pointer: %p\n", hDevice);
	
	if (INVALID_HANDLE_VALUE == hDevice) {
		printf("CREAT file error\n");
		return 1;
	}
	
	hInstance = CreateFile("\\\\.\\myDevice1\\{BDB421B0-4B37-4AA2-912B-3AA05F8A0829}", GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	printf("Handle pointer for instance: %p\n", hInstance);
	
	if (INVALID_HANDLE_VALUE != hInstance) {
		printf("CREAT multiple instance error\n");
		return 2;
	}
#if 0
	ubuf[0] = 'A';
	ubuf[1] = 'B';
	ubuf[2] = 'C';
	ubuf[3] = 'D';
	
	out[0] = '1';
	out[1] = '2';
	out[2] = '3';
	out[3] = '4';

	printf("bfd input:\n");
	printf("BUF = %c %c %c %c\n", ubuf[0], ubuf[1], ubuf[2], ubuf[3]);
	printf("OUT = %c %c %c %c\n", out[0], out[1], out[2], out[3]);
	printf(" >>> bfd >>>\n");
	if (!DeviceIoControl(hDevice, IOCTL_SAMPLE_BFD, ubuf, PACKET_SIZE, out, PACKET_SIZE, &out_len, NULL)) {
		printf("IOCTL BFD dev error\n");
	}
	
	printf("BUF = %c %c %c %c\n", ubuf[0], ubuf[1], ubuf[2], ubuf[3]);
	printf("OUT = %c %c %c %c\n", out[0], out[1], out[2], out[3]);
	printf(" <<< bfd <<<\n");
	
	ubuf[0] = '6';
	ubuf[1] = '7';
	ubuf[2] = '8';
	ubuf[3] = '9';
	
	out[0] = 'W';
	out[1] = 'X';
	out[2] = 'Y';
	out[3] = 'Z';
	
	printf("dio input:\n");
	printf("BUF = %c %c %c %c\n", ubuf[0], ubuf[1], ubuf[2], ubuf[3]);
	printf("OUT = %c %c %c %c\n", out[0], out[1], out[2], out[3]);
	printf(" >>> dio >>>\n");
	if (!DeviceIoControl(hDevice, IOCTL_SAMPLE_DIO_IN, ubuf, PACKET_SIZE, out, PACKET_SIZE, &out_len, NULL)) {
		printf("IOCTL DIO dev error\n");
	}

	printf("BUF = %c %c %c %c\n", ubuf[0], ubuf[1], ubuf[2], ubuf[3]);
	printf("OUT = %c %c %c %c\n", out[0], out[1], out[2], out[3]);
	printf(" <<< dio <<<\n");
	
	/* prepare for sending */
	ZeroMemory(ubuf, PACKET_SIZE);
	for (i = 6; i < MTU; i++, c++) {
		ubuf[i] = c;
	}
	memset(ubuf, 0xFF, 6);
	/* send packet using WriteFile */
	WriteFile(hDevice, ubuf, MTU, &ulen, NULL);
#endif
	
	for (n = 0; n < 50; n++) {
		ZeroMemory(ubuf, PACKET_SIZE);
		
		if (!ReadFile(hDevice, ubuf, PACKET_SIZE, &ulen, NULL)) {
			printf("READ packet error\n");
			return 3;
		}
		
		printf("[len=%d]\n", ulen);
		for (i = 0; i < ulen; i++) {
			if (i != 0 && ((i % 16) == 0)) {
				printf("\n");
			}
			printf(" %02X", ubuf[i]);
		}
		printf("\n\n");
	}
	
	CloseHandle(hDevice);
	
	return 0;
}

int __cdecl main(int argc, char* argv[])
{
	return connect_packet_filter_test();
}

#endif

