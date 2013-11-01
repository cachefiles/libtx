#include <stdio.h>
#include <unistd.h>

#include "txall.h"

#define MIN_TIME_OUT 15
#define MAX_MI_WHEEL 50
#define MAX_MA_WHEEL 60

#define MIN_MA_TIMER (MIN_TIME_OUT * MAX_MI_WHEEL)
#define MIN_ST_TIMER (MIN_MA_TIMER * MAX_MA_WHEEL)

typedef struct tx_timer_ring {
	size_t tx_st_tick;
	tx_poll_t tx_tm_callout;
	tx_timer_q tx_st_timers;

	size_t tx_mi_tick;
	size_t tx_mi_wheel;
	tx_timer_q tx_mi_timers[MAX_MI_WHEEL];

	size_t tx_ma_tick;
	size_t tx_ma_wheel;
	tx_timer_q tx_ma_timers[MAX_MA_WHEEL];
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
        timer->tx_flags &= ~TIMER_IDLE;
		timer->interval = (tx_ticks + umilsec);
		mi_wheel = (timer->interval - ring->tx_mi_tick) / MIN_TIME_OUT;

		TX_CHECK(mi_wheel > 0, "timer is too small");
		mi_wheel = (mi_wheel == 0? 1: mi_wheel);

		if (mi_wheel < MAX_MI_WHEEL) {
			wheel = (ring->tx_mi_wheel + mi_wheel) % MAX_MI_WHEEL;
			LIST_INSERT_HEAD(&ring->tx_mi_timers[wheel], timer, entries);
			return;
		}

		ma_wheel = (timer->interval - ring->tx_ma_tick) / MIN_MA_TIMER;
		if (ma_wheel < MAX_MA_WHEEL) {
			wheel = (ring->tx_ma_wheel + ma_wheel) % MAX_MA_WHEEL;
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
	int check_flags = (timer->tx_flags & TIMER_IDLE);

	if (check_flags == 0) {
		LIST_REMOVE(timer, entries);
		timer->tx_flags |= TIMER_IDLE;
		tx_task_active(timer->tx_task);
	}

	return;
}

void tx_timer_stop(tx_timer_t *timer)
{
	int check_flags = (timer->tx_flags & TIMER_IDLE);

	if (check_flags == 0) {
		timer->tx_flags |= TIMER_IDLE;
		LIST_REMOVE(timer, entries);
	}

	return;
}

static void tx_timer_polling(void *up)
{
    tx_timer_t *cur;
    tx_timer_t *next;
    tx_timer_q  timerq;
	tx_callout_t *ring;
	ring = (tx_callout_t *)up;

	unsigned wheel;
	unsigned ticks = tx_getticks();

	while ((int)(ticks - ring->tx_mi_tick - MIN_TIME_OUT) >= 0) {
		ring->tx_mi_tick += MIN_TIME_OUT;
		ring->tx_mi_wheel++;
		wheel = (ring->tx_mi_wheel % MAX_MI_WHEEL);

		timerq = ring->tx_mi_timers[wheel];
        LIST_INIT(&ring->tx_mi_timers[wheel]);
        LIST_FOREACH_SAFE(cur, &timerq, entries, next) {
			LIST_REMOVE(cur, entries);
			if ((int)(cur->interval - ticks - MIN_TIME_OUT) < 0) {
				cur->tx_flags |= TIMER_IDLE;
				tx_task_active(cur->tx_task);
			} else {
				cur->tx_flags |= TIMER_IDLE;
				tx_timer_reset(cur, cur->interval - ticks);
			}
		}
	}

	while ((int)(ticks - ring->tx_ma_tick - MIN_MA_TIMER) >= 0) {
		ring->tx_ma_tick += MIN_MA_TIMER;
		ring->tx_ma_wheel++;
		wheel = (ring->tx_ma_wheel % MAX_MA_WHEEL);

		timerq = ring->tx_ma_timers[wheel];
        LIST_INIT(&ring->tx_ma_timers[wheel]);
        LIST_FOREACH_SAFE(cur, &timerq, entries, next) {
			LIST_REMOVE(cur, entries);
			if ((int)(cur->interval - ticks - MIN_TIME_OUT) < 0) {
				cur->tx_flags |= TIMER_IDLE;
				tx_task_active(cur->tx_task);
			} else {
				cur->tx_flags |= TIMER_IDLE;
				tx_timer_reset(cur, cur->interval - ticks);
			}
		}
	}

	if ((int)(ticks - ring->tx_st_tick - MIN_ST_TIMER) >= 0) {
		ring->tx_st_tick = ticks;

		timerq = ring->tx_st_timers;
        LIST_INIT(&ring->tx_st_timers);
        LIST_FOREACH_SAFE(cur, &timerq, entries, next) {
			LIST_REMOVE(cur, entries);
			if ((int)(cur->interval - ticks - MIN_TIME_OUT) < 0) {
				cur->tx_flags |= TIMER_IDLE;
				tx_task_active(cur->tx_task);
			} else {
				cur->tx_flags |= TIMER_IDLE;
				tx_timer_reset(cur, cur->interval - ticks);
			}
		}
	}

    tx_poll_t *poll = &ring->tx_tm_callout;
    if (tx_loop_timeout(poll->tx_task.tx_loop, ring)) {
        usleep(10000);
        tx_getticks();
    }

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
	TX_CHECK(ring != NULL, "allocate memory failure");

	ring->tx_st_tick = tx_getticks();
	LIST_INIT(&ring->tx_st_timers);

	ring->tx_mi_tick = tx_ticks;
	ring->tx_mi_wheel = 0;
	for (int i = 0; i < MAX_MI_WHEEL; i++)
		LIST_INIT(&ring->tx_mi_timers[i]);

	ring->tx_ma_tick = tx_ticks;
	ring->tx_ma_wheel = 0;
	for (int i = 0; i < MAX_MA_WHEEL; i++)
		LIST_INIT(&ring->tx_ma_timers[i]);

	tx_poll_init(&ring->tx_tm_callout, loop, tx_timer_polling, ring);
	tx_poll_active(&ring->tx_tm_callout);

	if (TASK_IDLE & ring->tx_tm_callout.tx_task.tx_flags) {
		TX_PRINT(TXL_ERROR, "reactive poll failure");
		delete ring;
		return NULL;
	}

	return ring;
}

struct tx_timer_ring* tx_timer_ring_get(tx_loop_t *loop)
{
	tx_task_t *np;
	tx_task_q *taskq = &loop->tx_taskq;

	for (np = taskq->tqh_first; np; np = np->entries.tqe_next)
		if (np->tx_call == tx_timer_polling)
			return container_of(np, tx_timer_ring, tx_tm_callout.tx_task);

	return tx_timer_ring_new(loop);
}

