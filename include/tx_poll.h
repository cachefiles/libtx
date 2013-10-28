#ifndef _TX_POLL_H_
#define _TX_POLL_H_

typedef struct tx_poll_t {
	tx_task_t tx_task;
} tx_poll_t;

void tx_poll_init(tx_poll_t *t, tx_loop_t *l, void (*c)(void *), void *x);
void tx_poll_active(tx_poll_t *poll);
void tx_poll_drop(tx_poll_t *task);

typedef struct tx_inout_t {
	int tx_len;
	int tx_flags;
	tx_task_t *tx_task;
} tx_inout_t;

void tx_inout_init(tx_inout_t *iocbp, tx_poll_t *poll, tx_task_t *task);
void tx_inout_drop(tx_inout_t *iocbp);

#endif

