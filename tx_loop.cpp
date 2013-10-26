#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/queue.h>

#include "txall.h"

static tx_loop_t _default_loop = {0};

struct tx_loop_t * tx_loop_default(void)
{
	static int _init = 0;

	if (_init == 0) {
		TAILQ_INIT(&_default_loop.tx_taskq);
		_init = 1;
	}

	return &_default_loop;
}

void tx_task_init(struct tx_task_t *task, void (*call)(void*), void *data)
{
	task->tx_call = call;
	task->tx_data = data;
	task->tx_flags = TASK_IDLE;
	return;
}

struct tx_loop_t * tx_loop_new(void)
{
	tx_loop_t *up;
	up = (struct tx_loop_t *)malloc(sizeof(*up));
	TX_CHECK(up == NULL, "allocate memory failure");

	if (up != NULL) {
		memset(up, 0, sizeof(*up));
		TAILQ_INIT(&up->tx_taskq);
		up->tx_busy = 0;
	}

	return up;
}

void tx_task(tx_loop_t *up, tx_task_t *task)
{
	if ((up->tx_stop == 0) && (task->tx_flags & TASK_IDLE)) {
		TAILQ_INSERT_TAIL(&up->tx_taskq, task, entries);
		up->tx_busy |= 1;
	}

	TX_CHECK(up->tx_stop != 0, "aready stop");
	return;
}

void tx_poll(tx_loop_t *up, tx_task_t *poll)
{
	if ((up->tx_stop == 0) && (poll->tx_flags & TASK_IDLE)) {
		TAILQ_INSERT_TAIL(&up->tx_taskq, poll, entries);
		up->tx_busy |= 0;
	}

	TX_CHECK(up->tx_stop != 0, "aready stop");
	return;
}

void tx_loop(tx_loop_t *up)
{
	int dirty = 1;
	int first_run = 1;

	tx_task_t phony;
	tx_task_q *taskq = &up->tx_taskq;
	TAILQ_INSERT_TAIL(taskq, &phony, entries);

	while (!up->tx_stop || first_run) {
		tx_task_t *task = taskq->tqh_first;
		TAILQ_REMOVE(taskq, task, entries);
		if (task == &phony) {
			TAILQ_INSERT_TAIL(taskq, &phony, entries);
			up->tx_busy <<= 1;
			first_run = 0;
			tx_getticks();
			continue;
		}

		task->tx_flags |= TASK_IDLE;
		task->tx_call(task->tx_data);
	}

	if (dirty) {
		TAILQ_REMOVE(taskq, &phony, entries);
		/* TX_LOG_DEBUG("remove"); */
	}

	return;
}

void tx_stop(tx_loop_t *up)
{
	up->tx_stop = 1;
	return;
}

void tx_loop_delete(tx_loop_t *up)
{
	if (up != &_default_loop) {
		tx_task_q *taskq = &up->tx_taskq;
		tx_task_t *task = taskq->tqh_first;
		TX_CHECK(task != NULL, "loop not empty");
		task = task; //avoid warning
		free(up);
	}

	return;
}
