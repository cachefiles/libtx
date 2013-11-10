#ifndef _TX_NETWORK_H_
#define _TX_NETWORK_H_

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <tx_queue.h>

#define VNET(var) V_##var
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
