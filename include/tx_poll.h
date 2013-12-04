#ifndef _TX_POLL_H_
#define _TX_POLL_H_

struct tx_aiocb;
struct tx_loop_t;
struct tx_poll_t;

struct tx_poll_op {
	void (*tx_pollout)(tx_aiocb *filp);
	void (*tx_attach)(tx_aiocb *filp);
	void (*tx_pollin)(tx_aiocb *filp);
	void (*tx_detach)(tx_aiocb *filp);
};

struct tx_poll_t {
	tx_task_t tx_task;
	tx_poll_op *tx_ops;
};

tx_poll_t *tx_poll_get(tx_loop_t *loop);

void tx_poll_init(tx_poll_t *t, tx_loop_t *l, void (*c)(void *), void *x);
void tx_poll_active(tx_poll_t *poll);
void tx_poll_drop(tx_poll_t *task);

struct tx_inout_t {
	int tx_len;
	int tx_flags;
	tx_task_t *tx_task;
};

void tx_inout_init(tx_inout_t *iocbp, tx_poll_t *poll, tx_task_t *task);
void tx_inout_drop(tx_inout_t *iocbp);

tx_poll_t *tx_completion_port_init(tx_loop_t *loop);
tx_poll_t *tx_kqueue_init(tx_loop_t *loop);
tx_poll_t *tx_epoll_init(tx_loop_t *loop);

#endif

