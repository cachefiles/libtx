#ifndef _TX_PLATFORM_H_
#define _TX_PLATFORM_H_

struct tcpip_info {
	unsigned short port;
	unsigned int   address;
};

extern int ticks;
unsigned int tx_getticks(void);
extern volatile unsigned int tx_ticks;
int get_target_address(struct tcpip_info *info, const char *address);

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({             			\
		const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
		(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar)                       \
        for ((var) = LIST_FIRST((head));                                \
            (var) && ((tvar) = LIST_NEXT((var), field), 1);             \
            (var) = (tvar))
#endif

#ifndef TAILQ_FOREACH
#define TAILQ_FOREACH(var, head, field)                                 \
		for ((var) = TAILQ_FIRST((head));                               \
			(var);                                                      \
			(var) = TAILQ_NEXT((var), field))
#endif

int tx_setblockopt(int fd, int block);

#if defined(WIN32)

typedef int socklen_t;
int pipe(int filds[2]);
int inet_pton(int af, const char *src, void *dst);
#define SHUT_RD 0x00
#define SHUT_WR 0x01
#define SHUT_WRRD 0x02
#else

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#if !defined(min) && !defined(max)
#define min(a, b) ((a) < (b)? (a): (b))
#define max(a, b) ((a) < (b)? (b): (a))
#endif

#define KASSERT(exp, msg)

#endif

