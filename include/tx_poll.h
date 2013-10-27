#ifndef _TX_POLL_H_
#define _TX_POLL_H_

typedef struct tx_poll_t {
	tx_task_t tx_task;
} tx_poll_t;

void tx_poll_init(tx_poll_t *task, tx_loop_t *loop, void (*call)(void *), void *ctx);
void tx_poll_active(tx_poll_t *poll);
void tx_poll_drop(tx_poll_t *task);

#endif

