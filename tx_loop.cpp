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

struct tx_loop_t *tx_loop_get(tx_task_t *task)
{

	return task->tx_loop;
}

void tx_task_init(tx_task_t *task,
		tx_loop_t *loop, void (*call)(void*), void *data)
{
	task->tx_call = call;
	task->tx_data = data;
	task->tx_loop = loop;
	task->tx_flags = TASK_IDLE;
	return;
}

tx_task_t *tx_task_null(void)
{
	static struct tx_task_t null_task = {
		0, NULL, NULL, &_default_loop, {0}
	};

	return &null_task;
}

tx_loop_t *tx_loop_new(void)
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

void tx_task_active(tx_task_t *task)
{
	tx_loop_t *up = task->tx_loop;

	if ((up->tx_stop == 0) && (task->tx_flags & TASK_IDLE)) {
		TAILQ_INSERT_TAIL(&up->tx_taskq, task, entries);
		task->tx_flags &= ~TASK_IDLE;
		up->tx_busy |= 1;
	}

	TX_CHECK(up->tx_stop != 0, "aready stop");
	return;
}

void tx_loop_main(tx_loop_t *up)
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

void tx_loop_stop(tx_loop_t *up)
{
	up->tx_stop = 1;
	return;
}

int  tx_loop_timeout(tx_loop_t *up)
{
    if (up->tx_busy & 0x3) 
        return 0;
    if (up->tx_holder == NULL)
        return 10000;
    return 0;
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

int tx_wait_init(tx_wait_t *wcbp, tx_iocb_t *iocbp, tx_task_t *task)
{
	wcbp->tx_task = task;
	wcbp->tx_iocb = iocbp;
	wcbp->tx_flag = WAIT_IDLE;
	return 0;
}

int tx_wait_active(tx_wait_t *wcbp)
{
	struct tx_iocb_t *iocbp;

	iocbp = wcbp->tx_iocb;
	if (wcbp->tx_flag & WAIT_IDLE) {
		LIST_INSERT_HEAD(&iocbp->tx_waitq, wcbp, entries);
		wcbp->tx_flag &= ~WAIT_IDLE;
		/* tx_iocb_active(iocbp); */
	}

	return 0;
}

int tx_wait_cancel(tx_wait_t *wcbp)
{
	if (wcbp->tx_flag & WAIT_IDLE) {
		/* wait is not in queue */
		return 0;
	}

	LIST_REMOVE(wcbp, entries);
	wcbp->tx_flag |= WAIT_IDLE;
	return 0;
}
