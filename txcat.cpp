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
	unsigned int last_ticks;
};

static void update_tick(void *up)
{
	struct uptick_task *uptick;
	unsigned int ticks = tx_getticks();

	uptick = (struct uptick_task *)up;

	if (ticks != uptick->last_ticks) {
		//fprintf(stderr, "tx_getticks: %u %d\n", ticks, uptick->ticks);
		uptick->last_ticks = ticks;
	}

	if (uptick->ticks < 100000) {
		tx_task_active(&uptick->task);
		uptick->ticks++;
		return;
	}

#if 0
	tx_loop_stop(tx_loop_get(&uptick->task));
	fprintf(stderr, "stop the loop\n");
#endif
	return;
}

struct timer_task {
	tx_task_t task;
	tx_timer_t timer;
};

static void update_timer(void *up)
{
	struct timer_task *ttp;
	ttp = (struct timer_task*)up;

	tx_timer_reset(&ttp->timer, 500);
	fprintf(stderr, "update_timer %d\n", tx_ticks);
	return;
}

struct stdio_task {
	tx_file_t file;
	tx_task_t task;
	tx_wait_t incb;
	tx_wait_t outcb;
};

int main(int argc, char *argv[])
{
	struct timer_task tmtask;
	struct stdio_task iotest;
	struct uptick_task uptick;
	unsigned int last_tick = 0;
	tx_loop_t *loop = tx_loop_default();
	tx_timer_ring *provider = tx_timer_ring_get(loop);
	tx_timer_ring *provider1 = tx_timer_ring_get(loop);
	tx_timer_ring *provider2 = tx_timer_ring_get(loop);

	TX_CHECK(provider1 != provider, "timer provider not equal");
	TX_CHECK(provider2 != provider, "timer provider not equal");

	uptick.ticks = 0;
	uptick.last_ticks = tx_getticks();
	tx_task_init(&uptick.task, loop, update_tick, &uptick);
	tx_task_active(&uptick.task);

	tx_task_init(&tmtask.task, loop, update_timer, &tmtask);

	tx_timer_init(&tmtask.timer, provider, &tmtask.task);
	tx_timer_reset(&tmtask.timer, 500);

	tx_file_init(&iotest.file, loop, 0);
	tx_wait_in(&iotest.incb, &iotest.file, &iotest.task);
	tx_wait_out(&iotest.outcb, &iotest.file, &iotest.task);

	tx_loop_main(loop);
	tx_timer_stop(&tmtask.timer);

	tx_wait_cancel(&iotest.outcb);
	tx_wait_cancel(&iotest.incb);
	tx_file_close(&iotest.file);
	tx_loop_delete(loop);

	return 0;
}

