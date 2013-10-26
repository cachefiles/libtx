#ifndef _TX_LOOP_H_
#define _TX_LOOP_H_

#include <sys/queue.h>

#define TASK_IDLE 1

typedef struct tx_task_t {
	int tx_flags;
	void *tx_data;
	void (*tx_call)(void *ctx);
	TAILQ_ENTRY(tx_task_t) entries;
} tx_task_t;

typedef TAILQ_HEAD(tx_task_q, tx_task_t) tx_task_q;

typedef struct tx_loop_t {
	int tx_busy;
	int tx_stop;
	void *tx_holder;
	tx_task_q tx_taskq;
} tx_loop_t;

struct tx_loop_t * tx_loop_new(void);
struct tx_loop_t * tx_loop_default(void);

void tx_loop_delete(tx_loop_t *up);
void tx_task_init(tx_task_t *task, void (*call)(void *), void *ctx);

void tx_loop(tx_loop_t *up);
void tx_stop(tx_loop_t *up);

void tx_task(tx_loop_t *up, tx_task_t *task);
void tx_poll(tx_loop_t *up, tx_task_t *poll);

#endif
