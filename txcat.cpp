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

	if (uptick->ticks < 100) {
		tx_task_active(&uptick->task, "tick");
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
	tx_aiocb file;
	tx_task_t task;
};

static void update_stdio(void *up)
{
    int len;
    char buf[8192 * 4];
    struct stdio_task *tp;
    tp = (struct stdio_task *)up;

	for ( ; ; ) {
#ifndef WIN32
		len = read(STDIN_FILE_FD, buf, sizeof(buf));
#else
		len = recv(STDIN_FILE_FD, buf, sizeof(buf), 0);
#endif
		tx_aincb_update(&tp->file, len);
		if (!tx_readable(&tp->file)) {
			tx_aincb_active(&tp->file, &tp->task);
			break;
		}

		if (len <= 0) {
			fprintf(stderr, "reach end of file, stop the loop\n");
			tx_loop_stop(tx_loop_get(&tp->task));
			break;
		}

		fwrite(buf, len, 1, stdout);
	}

	return;
}

int main(int argc, char *argv[])
{
	struct timer_task tmtask;
	struct stdio_task iotest;
	struct uptick_task uptick;
	unsigned int last_tick = 0;
	tx_loop_t *loop = tx_loop_default();
	tx_poll_t *poll = tx_epoll_init(loop);
	tx_poll_t *poll1 = tx_completion_port_init(loop);
	tx_timer_ring *provider = tx_timer_ring_get(loop);
	tx_timer_ring *provider1 = tx_timer_ring_get(loop);
	tx_timer_ring *provider2 = tx_timer_ring_get(loop);

	TX_CHECK(provider1 == provider, "timer provider not equal");
	TX_CHECK(provider2 == provider, "timer provider not equal");

	uptick.ticks = 0;
	uptick.last_ticks = tx_getticks();
	tx_task_init(&uptick.task, loop, update_tick, &uptick);
	tx_task_active(&uptick.task, "tick");

	tx_timer_init(&tmtask.timer, loop, &tmtask.task);
	tx_task_init(&tmtask.task, loop, update_timer, &tmtask);
	tx_timer_reset(&tmtask.timer, 500);

	tx_aiocb_init(&iotest.file, loop, STDIN_FILE_FD);
	tx_task_init(&iotest.task, loop, update_stdio, &iotest);
	tx_aincb_active(&iotest.file, &iotest.task);

	tx_loop_main(loop);

	tx_aincb_stop(&iotest.file, &iotest.task);
	tx_timer_stop(&tmtask.timer);
	tx_aiocb_fini(&iotest.file);
#ifdef WIN32
	closesocket(STDIN_FILE_FD);
#else
	close(STDIN_FILE_FD);
#endif
	tx_loop_delete(loop);

	TX_UNUSED(last_tick);
	TX_UNUSED(provider2);
	TX_UNUSED(provider1);

	return 0;
}

