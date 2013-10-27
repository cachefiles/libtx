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
	unsigned int last_ticks;
};

static void update_tick(void *up)
{
	struct uptick_task *uptick;
	unsigned int ticks = tx_getticks();

	uptick = (struct uptick_task *)up;

	if (ticks != uptick->last_ticks) {
		fprintf(stderr, "tx_getticks: %u %d\n", ticks, uptick->ticks);
		uptick->last_ticks = ticks;
	}

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

	uptick.ticks = 0;
	uptick.loop = loop;
	uptick.last_ticks = tx_getticks();
	tx_task_init(&uptick.task, update_tick, &uptick);
	tx_task(loop, &uptick.task);

	tx_loop(loop);

	tx_loop_delete(loop);

	return 0;
}

