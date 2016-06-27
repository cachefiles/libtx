#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <netdb.h>
#endif

#include "txall.h"
#include "tx_debug.h"
#include "tx_platform.h"

int get_target_address(struct tcpip_info *info, const char *address)
{
	const char *last;

#define FLAG_HAVE_DOT    1
#define FLAG_HAVE_ALPHA  2
#define FLAG_HAVE_NUMBER 4
#define FLAG_HAVE_SPLIT  8

	int flags = 0;
	char host[128] = {};

	for (last = address; *last; last++) {
		if (isdigit(*last)) flags |= FLAG_HAVE_NUMBER;
		else if (*last == ':') flags |= FLAG_HAVE_SPLIT;
		else if (*last == '.') flags |= FLAG_HAVE_DOT;
		else if (isalpha(*last)) flags |= FLAG_HAVE_ALPHA;
		else { fprintf(stderr, "get target address failure: %s!\n", address); return -1;}
	}

	if (flags == FLAG_HAVE_NUMBER) {
		info->port = htons(atoi(address));
		return 0;
	}

	if (flags == (FLAG_HAVE_NUMBER| FLAG_HAVE_DOT)) {
		info->address = inet_addr(address);
		return 0;
	}

	struct hostent *host0 = NULL;
	if ((flags & ~FLAG_HAVE_NUMBER) == (FLAG_HAVE_ALPHA | FLAG_HAVE_DOT)) {
		host0 = gethostbyname(address);
		if (host0 != NULL)
			memcpy(&info->address, host0->h_addr, 4);
		return 0;
	}

	if (flags & FLAG_HAVE_SPLIT) {
		const char *split = strchr(address, ':');
		info->port = htons(atoi(split + 1));

		if (strlen(address) < sizeof(host)) {
			strncpy(host, address, sizeof(host));
			host[split - address] = 0;

			if (flags & FLAG_HAVE_ALPHA) {
				host0 = gethostbyname(host);
				if (host0 != NULL)
					memcpy(&info->address, host0->h_addr, 4);
				return 0;
			}

			info->address = inet_addr(host);
		}
	}

	return 0;
}

volatile unsigned int tx_ticks = 0;

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

unsigned int tx_getticks(void)
{
#if defined(__linux__) || defined(__FreeBSD__)
	int err;
	struct timespec ts; 

	err = clock_gettime(CLOCK_MONOTONIC, &ts);
	TX_CHECK(err == 0, "clock_gettime failure");

	return tx_ticks = (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);

#elif defined(WIN32)
	LARGE_INTEGER now = {0};
	static LARGE_INTEGER bootup = {0};
	static LARGE_INTEGER frequency = {0};

	if (frequency.QuadPart == 0) {
		QueryPerformanceCounter(&bootup);
		QueryPerformanceFrequency(&frequency);
	}

	QueryPerformanceCounter(&now);
	return  tx_ticks = (now.QuadPart - bootup.QuadPart) * 1000LL / frequency.QuadPart;
#elif defined(__APPLE__)
	clock_serv_t cclock;
	mach_timespec_t ts;
	host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
	clock_get_time(cclock, &ts);
	mach_port_deallocate(mach_task_self(), cclock);
	return tx_ticks = (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

#if defined(WIN32)
#define ABORTON(cond) if (cond) goto clean
static int inet_pton4(const char *src, unsigned char *dst);

int socketpair(int domain, int type, int protocol, int fildes[2])
{
    int error;
    int tcp1, tcp2;
    sockaddr_in name;
    memset(&name, 0, sizeof(name));
    name.sin_family = (domain==AF_INET6? domain: AF_INET);
    name.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int namelen = sizeof(name);
    tcp1 = tcp2 = -1;

    int tcp = socket(domain, type, protocol);
    ABORTON(tcp == -1);

    error = bind(tcp, (sockaddr*)&name, namelen);
    ABORTON(error == -1);

    error = listen(tcp, 5);
    ABORTON(error == -1);

    error = getsockname(tcp, (sockaddr*)&name, &namelen);
    ABORTON(error == -1);

    tcp1 = socket(domain, type, protocol);
    ABORTON(tcp1 == -1);

    error = connect(tcp1, (sockaddr*)&name, namelen);
    ABORTON(error == -1);

    tcp2 = accept(tcp, (sockaddr*)&name, &namelen);
    ABORTON(tcp2 == -1);

    error = closesocket(tcp);
    ABORTON(error == -1);

    fildes[0] = tcp1;
    fildes[1] = tcp2;
    return 0;

clean:
    if (tcp != -1)
        closesocket(tcp);

    if (tcp2 != -1)
        closesocket(tcp2);

    if (tcp1 != -1)
        closesocket(tcp1);

    return -1;
}

int pipe(int fildes[2])
{
	int ret;

	ret = socketpair(AF_INET, SOCK_STREAM, 0, fildes);
	if (ret == 0) {
		shutdown(fildes[1], SD_RECEIVE);
		shutdown(fildes[0], SD_SEND);
		return 0;
	}

    return ret;
} 

int inet_pton(int af, const char* src, void* dst)
{
	switch (af) {
		case AF_INET:
			return inet_pton4(src, (unsigned char *)dst);

#if defined(AF_INET6)
		case AF_INET6:
			/*return inet_pton6(src, (unsigned char *)dst); */
#endif

		default:
			return 0;
	}
	/* NOTREACHED */
}

int inet_pton4(const char *src, unsigned char *dst)
{
	static const char digits[] = "0123456789";
	int saw_digit, octets, ch;
	unsigned char tmp[sizeof(struct in_addr)], *tp;

	saw_digit = 0;
	octets = 0;
	*(tp = tmp) = 0;
	while ((ch = *src++) != '\0') {
		const char *pch;

		if ((pch = strchr(digits, ch)) != NULL) {
			unsigned int nw = *tp * 10 + (pch - digits);

			if (saw_digit && *tp == 0)
				return 0;
			if (nw > 255)
				return 0;
			*tp = nw;
			if (!saw_digit) {
				if (++octets > 4)
					return 0;
				saw_digit = 1;
			}
		} else if (ch == '.' && saw_digit) {
			if (octets == 4)
				return 0;
			*++tp = 0;
			saw_digit = 0;
		} else
			return 0;
	}
	if (octets < 4)
		return 0;
	memcpy(dst, tmp, sizeof(struct in_addr));
	return 1;
}

#endif

int tx_setblockopt(int fd, int blockopt)
{
	int iflags;

#ifndef WIN32
	int oflags;

	iflags = fcntl(fd, F_GETFL);

	if (blockopt)
		oflags = (iflags & ~O_NONBLOCK);
	else
		oflags = (iflags | O_NONBLOCK);

	if (iflags != oflags)
		iflags = fcntl(fd, F_SETFL, oflags);
#else
	u_long blockval = (blockopt == 0);
	iflags = ioctlsocket(fd, FIONBIO, &blockval);
#endif
	return iflags;
}

void init_stub(void)
{
	fprintf(stderr, "init_stub\n");
	return;
}

void clean_stub(void)
{
	fprintf(stderr, "clean_stub\n");
	return;
}

void initialize_modules(struct module_stub *list[])
{
	void (* initp)(void);
	struct module_stub **stubp;

	stubp = list;
	while (*stubp) {
		initp = (*stubp)->init;
		if (initp != 0)
			(void)initp();
		stubp++;
	}
}

void cleanup_modules(struct module_stub *list[])
{
	void (* cleanp)(void);
	struct module_stub **stubp;

	stubp = list;
	while (*stubp) {
		cleanp = (*stubp)->clean;
		if (cleanp != 0)
			(void)cleanp();
		stubp++;
	}
}

