#ifndef _TX_NETWORK_H_
#define _TX_NETWORK_H_

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <tx_queue.h>

struct socket {
	struct {
		int sb_cc;
		int sb_flags;
		int sb_hiwat;
		int sb_state;
	} so_snd, so_rcv;
	int so_error;
	int so_state;
	int so_oobmark;
	int so_options;
};

#define sb_max 8192

#define SO_OOBINLINE 0x04
#define SS_NOFDREF 0x2
#define SO_OOBINLINE 0x1
#define SB_AUTOSIZE  0x1
#define SBS_CANTRCVMORE 0x2
#define SBS_RCVATMARK 0x4

#define TI_WLOCKED 0x01
#define TI_UNLOCKED 0x02
#define BANDLIM_UNLIMITED 0x01
#define BANDLIM_RST_OPENPORT 0x02

#define TCP_PROBE5(a, b, c, d, e, f)

struct callout {};
struct sockaddr {};
#define log(level, fmt, args...) 

#define INET
#define _KERNEL
#define VNET(var) V_##var
#define VNET_NAME(v)
#define SYSCTL_INT(a, b, c, d, e, f, g)
#define SYSCTL_NODE(a, b, c, d, e, f)
#define VNET_PCPUSTAT_DEFINE(t, n) t n
#define VNET_PCPUSTAT_ADD(args...)
#define SYSCTL_VNET_INT(a, b, c, d, e, f, g)
#define VNET_DEFINE(type, var) type V_##var
#define VNET_DECLARE(type, var) extern type V_##var
#define VNET_PCPUSTAT_DECLARE(type, name) 
#define MALLOC_DEFINE(a, b, c)
#define VNET_ITERATOR_DECL(v)
#define VNET_FOREACH(v)
#define CURVNET_SET(v)

#define uma_zalloc(s, t) malloc(s)
#define uma_zfree(p, t) free(p)
typedef struct uma_zone_s * uma_zone_t;

#define malloc(p, args...) malloc(p)
#define free(p, args...) free(p)
#define __unused

extern int hz;
extern int ticks;
#define ERTT_NEW_MEASUREMENT            0x01

#endif
