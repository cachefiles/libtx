#include <stdio.h>
#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "txall.h"

struct uptick_task {
	int ticks;
	tx_task_t task;
	tx_loop_t *loop;
};

static void update_tick(void *up)
{
	struct uptick_task *uptick;

	uptick = (struct uptick_task *)up;
	if (uptick->ticks < 100000) {
		tx_task(uptick->loop, &uptick->task);
		uptick->ticks++;
		return;
	}

	fprintf(stderr, "stop the loop\n");
	tx_stop(uptick->loop);
	return;
}

int main(int argc, char *argv[])
{
	struct uptick_task uptick;
	unsigned int last_tick = 0;
	tx_loop_t *loop = tx_loop_default();

	for (int i = 0; i < 10000; i++) {
		unsigned int ticks = tx_getticks();
		if (ticks != last_tick) {
			fprintf(stderr, "tx_getticks: %u\n", ticks);
			last_tick = ticks;
		}

#ifdef WIN32
		Sleep(0);
#else
		sleep(0);
#endif
	}

	uptick.ticks = 0;
	uptick.loop = loop;
	tx_task_init(&uptick.task, update_tick, &uptick);
	tx_task(loop, &uptick.task);

	tx_loop(loop);

	tx_loop_delete(loop);

	return 0;
}

