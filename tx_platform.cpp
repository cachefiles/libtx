#include <time.h>
#include <stdio.h>

#ifdef WIN32
#include <windows.h>
#endif

#include "tx_debug.h"
#include "tx_platform.h"

volatile unsigned int tx_ticks = 0;

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
#endif
}

#if defined(WIN32)
#define ABORTON(cond) if (cond) goto clean

int pipe(int fildes[2])
{
    int error;
    int tcp1, tcp2;
    sockaddr_in name;
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int namelen = sizeof(name);
    tcp1 = tcp2 = -1;

    int tcp = socket(AF_INET, SOCK_STREAM, 0);
    ABORTON(tcp == -1);

    error = bind(tcp, (sockaddr*)&name, namelen);
    ABORTON(error == -1);

    error = listen(tcp, 5);
    ABORTON(error == -1);

    error = getsockname(tcp, (sockaddr*)&name, &namelen);
    ABORTON(error == -1);

    tcp1 = socket(AF_INET, SOCK_STREAM, 0);
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
#endif
