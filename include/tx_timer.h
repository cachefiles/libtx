#ifndef _TIMER_H_

struct tx_task_t;
struct tx_timer_t;
struct tx_timer_ring;

void tx_timer_init(tx_timer_t *timer, tx_timer_ring *ring, tx_task_t *task);
void tx_timer_reset(tx_timer_t *timer, unsigned umilsec);
void tx_timer_drain(tx_timer_t *timer);
void tx_timer_stop(tx_timer_t *timer);

struct tx_loop_t;
#define TIMER_IDLE 0x01
#define tx_timer_idle(t) ((t)->tx_flags & TIMER_IDLE)

struct tx_timer_t {
	int tx_flags;
	unsigned interval;
	tx_task_t *tx_task;
	tx_timer_ring *tx_ring;
	LIST_ENTRY(tx_timer_t) entries;
};

typedef LIST_HEAD(tx_timer_q, tx_timer_t) tx_timer_q;
struct tx_timer_ring* tx_timer_ring_get(tx_loop_t *loop);

#endif

