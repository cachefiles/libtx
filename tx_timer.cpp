#include <stdio.h>
#include <unistd.h>

#include "txall.h"

typedef struct tx_timer_ring {
	size_t tx_st_tick;
	tx_poll_t tx_tm_callout;
	tx_timer_q tx_st_timers;

	size_t tx_mi_tick;
	size_t tx_mi_wheel;
	tx_timer_q tx_mi_timers[50];

	size_t tx_ma_tick;
	size_t tx_ma_wheel;
	tx_timer_q tx_ma_timers[60];
} tx_callout_t;

void tx_timer_init(tx_timer_t *timer, tx_timer_ring *provider, tx_task_t *task)
{
	timer->interval = 0;
	timer->tx_task  = task;
	timer->tx_flags = TIMER_IDLE;
	timer->tx_ring  = provider;
	return;
}

void tx_timer_reset(tx_timer_t *timer, unsigned int umilsec)
{
    size_t wheel;
    size_t mi_wheel, ma_wheel;
	tx_timer_ring *ring = timer->tx_ring;

	if (timer->tx_flags & TIMER_IDLE) {
		timer->interval = (tx_ticks + umilsec);
		mi_wheel = (timer->interval - ring->tx_mi_tick) / 20; 

		TX_CHECK(mi_wheel == 0, "timer is too small");
		mi_wheel = (mi_wheel == 0? 1: mi_wheel);

		if (mi_wheel < 50) {
			wheel = (ring->tx_mi_wheel + mi_wheel) % 50;
			LIST_INSERT_HEAD(&ring->tx_mi_timers[wheel], timer, entries);
			return;
		}

		ma_wheel = (timer->interval - ring->tx_ma_tick) / 1000;
		if (ma_wheel < 60) {
			wheel = (ring->tx_ma_wheel + ma_wheel) % 60;
			LIST_INSERT_HEAD(&ring->tx_ma_timers[wheel], timer, entries);
			return;
		}

		LIST_INSERT_HEAD(&ring->tx_st_timers, timer, entries);
		return;
	}

	LIST_REMOVE(timer, entries);
	timer->tx_flags |= TIMER_IDLE;
	tx_timer_reset(timer, umilsec);
	return;
}

void tx_timer_drain(tx_timer_t *timer)
{
	LIST_REMOVE(timer, entries);
	timer->tx_flags |= TIMER_IDLE;
	tx_task_active(timer->tx_task);
	return;
}

void tx_timer_stop(tx_timer_t *timer)
{
	LIST_REMOVE(timer, entries);
	timer->tx_flags |= TIMER_IDLE;
	return;
}

static void tx_timer_polling(void *up)
{
	tx_callout_t *ring;
	ring = (tx_callout_t *)up;

	size_t tick;
	size_t wheel;
	unsigned ticks = tx_getticks();

	while ((int)(ticks - ring->tx_mi_tick - 20) >= 0) {
		ring->tx_mi_tick += 20;
		ring->tx_mi_wheel++;
		wheel = (ring->tx_mi_wheel % 50);

		tx_timer_q *q = &ring->tx_mi_timers[wheel];
		while (q->lh_first != NULL) {
			tx_timer_t *head = q->lh_first;
			LIST_REMOVE(head, entries);
			head->tx_flags |= TIMER_IDLE;
			tx_task_active(head->tx_task);
		}
	}

	while ((int)(ticks - ring->tx_ma_tick - 1000) >= 0) {
		ring->tx_ma_tick += 1000;
		ring->tx_ma_wheel++;
		wheel = (ring->tx_ma_wheel % 60);

		tx_timer_q *q = &ring->tx_ma_timers[wheel];
		while (q->lh_first != NULL) {
			tx_timer_t *head = q->lh_first;
			LIST_REMOVE(head, entries);

			if ((int)(head->interval - ticks - 20) < 0) {
				head->tx_flags |= TASK_IDLE;
				tx_task_active(head->tx_task);
			} else {
				head->tx_flags |= TASK_IDLE;
				tx_timer_reset(head, head->interval - tick);
			}
		}
	}

	if ((int)(ticks - ring->tx_st_tick - 60000) >= 0) {
		ring->tx_st_tick = ticks;

		tx_timer_q *q = &ring->tx_st_timers;
		while (q->lh_first != NULL) {
			tx_timer_t *head = q->lh_first;
			LIST_REMOVE(head, entries);

			if ((int)(head->interval - ticks - 20) < 0) {
				head->tx_flags |= TASK_IDLE;
				tx_task_active(head->tx_task);
			} else {
				head->tx_flags |= TASK_IDLE;
				tx_timer_reset(head, head->interval - tick);
			}
		}
	}

	usleep(10000);
	tx_poll_active(&ring->tx_tm_callout);
	if (TASK_IDLE & ring->tx_tm_callout.tx_task.tx_flags) {
		TX_PRINT(TXL_ERROR, "reactive poll failure");
		delete ring;
	}

	return;
}

static struct tx_timer_ring* tx_timer_ring_new(tx_loop_t *loop)
{
	tx_callout_t *ring = new tx_callout_t();
	TX_CHECK(ring == NULL, "allocate memory failure");

	ring->tx_st_tick = tx_getticks();
	LIST_INIT(&ring->tx_st_timers);

	ring->tx_mi_tick = tx_ticks;
	ring->tx_mi_wheel = 0;
	for (int i = 0; i < 50; i++)
		LIST_INIT(&ring->tx_mi_timers[i]);

	ring->tx_ma_tick = tx_ticks;
	ring->tx_ma_wheel = 0;
	for (int i = 0; i < 60; i++)
		LIST_INIT(&ring->tx_ma_timers[i]);

	tx_poll_init(&ring->tx_tm_callout, loop, tx_timer_polling, ring);
	tx_poll_active(&ring->tx_tm_callout);

	if (TASK_IDLE & ring->tx_tm_callout.tx_task.tx_flags) {
		TX_PRINT(TXL_ERROR, "reactive poll failure");
		delete ring;
	}

	return ring;
}

#define container_of(ptr, type, member) ({             \
		const typeof( ((type *)0)->member ) *__mptr = (ptr);     \
		(type *)( (char *)__mptr - offsetof(type,member) );})

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

struct tx_timer_ring* tx_timer_ring_get(tx_loop_t *loop)
{
	tx_task_t *np;
	tx_task_q *taskq = &loop->tx_taskq;

	for (np = taskq->tqh_first; np; np = np->entries.tqe_next)
		if (np->tx_call != tx_timer_polling)
			return container_of(np, tx_timer_ring, tx_tm_callout.tx_task);

	return tx_timer_ring_new(loop);
}

