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
	int err;

#ifdef __linux__
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

