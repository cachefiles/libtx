#include <stdio.h>
#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "txall.h"

int main(int argc, char *argv[])
{
	unsigned int last_tick = 0;

	for (int i = 0; i < 10000; i++) {
		unsigned int ticks = tx_getticks();
		if (ticks != last_tick) {
			fprintf(stderr, "%u\n", ticks);
			last_tick = ticks;
		}

#ifdef WIN32
		Sleep(0);
#else
		sleep(0);
#endif
	}

	return 0;
}

