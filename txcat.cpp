#include <stdio.h>
#include <errno.h>
#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "txall.h"

#define STDIN_FILE_FD 0

struct uptick_task {
	int ticks;
	tx_task_t task;
	unsigned int last_ticks;
};

static void update_tick(void *up)
{
	struct uptick_task *uptick;
	unsigned int ticks = tx_ticks;

	uptick = (struct uptick_task *)up;

	if (ticks != uptick->last_ticks) {
		//fprintf(stderr, "tx_getticks: %u %d\n", ticks, uptick->ticks);
		uptick->last_ticks = ticks;
	}

	if (uptick->ticks < 10000000) {
		tx_task_active(&uptick->task);
		uptick->ticks++;
		return;
	}

	fprintf(stderr, "all update_tick finish\n");
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

	tx_timer_reset(&ttp->timer, 50000);
	fprintf(stderr, "update_timer %d\n", tx_ticks);
	return;
}

struct stdio_task {
	tx_file_t file;
	tx_task_t task;
};

static void update_stdio(void *up)
{
    int len;
    char buf[8192];
    struct stdio_task *tp;
    tp = (struct stdio_task *)up;

	fprintf(stderr, "update_stdio %d\n", tx_ticks);
    len = tx_read(&tp->file, buf, sizeof(buf));
    if (len > 0 || (len == -1 && errno == EAGAIN))
        tx_file_active_in(&tp->file, &tp->task);
    return;
}

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

	tx_timer_init(&tmtask.timer, provider, &tmtask.task);
	tx_task_init(&tmtask.task, loop, update_timer, &tmtask);
	tx_timer_reset(&tmtask.timer, 500);

	tx_file_init(&iotest.file, loop, STDIN_FILE_FD);
	tx_task_init(&iotest.task, loop, update_stdio, &iotest);
	tx_file_active_in(&iotest.file, &iotest.task);

	tx_loop_main(loop);

	tx_file_cancel_in(&iotest.file, &iotest.task);
	tx_file_close(&iotest.file);
	tx_timer_stop(&tmtask.timer);
	tx_loop_delete(loop);

	return 0;
}

