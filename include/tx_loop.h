#ifndef _TX_LOOP_H_
#define _TX_LOOP_H_

#if defined(WIN32) && defined(SLIST_ENTRY)
#warning "SLIST_ENTRY is aready defined"
#undef SLIST_ENTRY
#endif

#include <sys/queue.h>

#define TASK_IDLE 1

struct tx_task_t {
	int tx_flags;
	void *tx_data;
	void (*tx_call)(void *ctx);
	struct tx_loop_t *tx_loop;
	TAILQ_ENTRY(tx_task_t) entries;
};

TAILQ_HEAD(tx_task_q, tx_task_t);

struct tx_loop_t {
	int tx_busy;
	int tx_stop;
	void *tx_holder;
	tx_task_q tx_taskq;
};

struct tx_loop_t *tx_loop_new(void);
struct tx_loop_t *tx_loop_default(void);
struct tx_loop_t *tx_loop_get(tx_task_t *task);

void tx_loop_delete(tx_loop_t *up);
void tx_loop_main(tx_loop_t *up);
void tx_loop_stop(tx_loop_t *up);

void tx_task_init(tx_task_t *task, tx_loop_t *loop, void (*call)(void *), void *ctx);
void tx_task_active(tx_task_t *task);
void tx_task_drop(tx_task_t *task);

#endif
