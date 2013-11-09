#ifndef _TX_NETWORK_H_
#define _TX_NETWORK_H_

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>
#include <tx_queue.h>

#define VNET(var) V_##var
#define VNET_DEFINE(type, var) type V_##var
#define VNET_DECLARE(type, var) extern type V_##var
#define VNET_PCPUSTAT_DECLARE(type, name) 

extern int hz;
extern int ticks;
#define ERTT_NEW_MEASUREMENT            0x01

#endif
