#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libtx/queue.h>

#include "txall.h"

void tx_poll_init(tx_poll_t *poll,
		tx_loop_t *loop, void (*call)(void*), void *data)
{
	tx_task_t *task;
	task = &poll->tx_task;
	tx_task_init(task, loop, call, data);
	return;
}

tx_poll_t *tx_poll_get(tx_loop_t *loop)
{
	/* XXXXXXXXXXX */
	return loop->tx_poller;
}

void tx_poll_active(tx_poll_t *poll)
{
	tx_loop_t *up;
	tx_task_t *task;

	task = &poll->tx_task;
	up   = task->tx_loop;

	if ((up->tx_stop == 0) && (task->tx_flags & TASK_IDLE)) {
		LIST_INSERT_BEFORE(&up->tx_tailer, task, entries);
		task->tx_flags &= ~TASK_IDLE;
		up->tx_busy |= 0;
	}

	TX_CHECK(up->tx_stop == 0, "aready stop");
	return;
}
