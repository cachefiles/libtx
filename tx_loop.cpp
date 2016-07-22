#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys/queue.h"

#include "txall.h"

static tx_loop_t _default_loop = {0};

struct tx_loop_t * tx_loop_default(void)
{
	static int _init = 0;

	if (_init == 0) {
		LIST_INIT(&_default_loop.tx_taskq);
		LIST_INSERT_HEAD(&_default_loop.tx_taskq,
				&_default_loop.tx_tailer, entries);
		_init = 1;
	}

	return &_default_loop;
}

tx_loop_t *tx_loop_get(tx_task_t *task)
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

void tx_task_record(tx_task_q *taskq, tx_task_t *task)
{
	tx_task_drop(task);

	LIST_INSERT_HEAD(taskq, task, entries);
	task->tx_flags &= ~TASK_IDLE;
	return;
}

void tx_task_wakeup(tx_task_q *taskq)
{
	tx_task_t *cur, *next;

	 LIST_FOREACH_SAFE(cur, taskq, entries, next) {
		 tx_task_active(cur);
	 }

	/* taskq revert to empty */
	LIST_INIT(taskq);
	return;
}

tx_loop_t *tx_loop_new(void)
{
	tx_loop_t *up;
	up = (struct tx_loop_t *)malloc(sizeof(*up));
	TX_CHECK(up == NULL, "allocate memory failure");

	if (up != NULL) {
		memset(up, 0, sizeof(*up));
		LIST_INIT(&up->tx_taskq);
		LIST_INSERT_HEAD(&up->tx_taskq, &up->tx_tailer, entries);
		up->tx_holder = NULL;
		up->tx_poller = NULL;
		up->tx_break = 0;
		up->tx_stop = 0;
		up->tx_busy = 0;
	}

	return up;
}

void tx_task_active(tx_task_t *task)
{
	tx_loop_t *up;
	
	if (task == NULL) {
		/* XXX */
		return;
	}

	up = task->tx_loop;
	if ((up->tx_stop == 0) && (task->tx_flags & TASK_BUSY) != TASK_BUSY) {
		LIST_INSERT_BEFORE(&up->tx_tailer, task, entries);
		task->tx_flags &= ~TASK_IDLE;
		task->tx_flags |= TASK_BUSY;
		up->tx_actives++;
		up->tx_busy |= 1;
	}

	TX_CHECK(up->tx_stop == 0, "aready stop");
	return;
}

void tx_task_drop(tx_task_t *task)
{
	if (task != NULL) {
#ifdef DEBUG
		TX_CHECK(task->tx_flags & TASK_BUSY, "task is not busy");
#endif
		if ((task->tx_flags & TASK_IDLE) != TASK_IDLE) {
			task->tx_flags &= ~TASK_BUSY;
			LIST_REMOVE(task, entries);
			task->tx_flags |= TASK_IDLE;
		}
	}

	return;
}

void tx_loop_main(tx_loop_t *up)
{
	int dirty = 1;
	int first_run = 1;

	tx_task_t phony;
	tx_task_q *taskq = &up->tx_taskq;
	LIST_INSERT_BEFORE(&up->tx_tailer, &phony, entries);

	while (!up->tx_stop || first_run) {
		tx_task_t *task = taskq->lh_first;
		LIST_REMOVE(task, entries);
		if (task == &phony) {
			LIST_INSERT_BEFORE(&up->tx_tailer, &phony, entries);
			if (up->tx_busy & 0x01) {
				/* XXX */
			} else {
				up->tx_actives = 0;
			}
			up->tx_busy <<= 1;
			up->tx_upcount++;
			first_run = 0;

			if (up->tx_break) {
				up->tx_break = 0;
				break;
			}
			continue;
		}

		if (task->tx_flags & TASK_BUSY) {
			task->tx_flags &= ~TASK_BUSY;
			up->tx_actives--;
		}

		task->tx_flags |= TASK_IDLE;
		task->tx_call(task->tx_data);
	}

	if (dirty) {
		LIST_REMOVE(&phony, entries);
		/* TX_LOG_DEBUG("remove"); */
	}

	return;
}

void tx_loop_break(tx_loop_t *up)
{
	up->tx_break = 1;
	return;
}

void tx_loop_stop(tx_loop_t *up)
{
	up->tx_stop = 1;
	return;
}

int  tx_loop_timeout(tx_loop_t *up, const void *verify)
{
    if ((up->tx_busy & 0x3)
		&& up->tx_actives > 0)
        return 0;
    if (up->tx_break > 0)
        return 0;
    if (up->tx_holder == NULL)
        return 10000;
	if (up->tx_holder == verify)
		return 10000;
    return 0;
}

void tx_loop_delete(tx_loop_t *up)
{
	if (up != &_default_loop) {
		tx_task_q *taskq = &up->tx_taskq;
		tx_task_t *task = taskq->lh_first;
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
