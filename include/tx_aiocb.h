#ifndef _TX_FILE_
#define _TX_FILE_

struct tx_aiocb;
struct tx_loop_t;

#define TX_POLLIN   0x01
#define TX_POLLOUT  0x02
#define TX_READABLE 0x04
#define TX_WRITABLE 0x08
#define TX_ATTACHED 0x10
#define TX_DETACHED 0x20

#define tx_readable(filp) ((filp)->tx_flags & TX_READABLE)
#define tx_writable(filp) ((filp)->tx_flags & TX_WRITABLE)

struct tx_aiocb_op {
	void (*op_active_out)(tx_aiocb *f, tx_task_t *t);
	void (*op_cancel_out)(tx_aiocb *f, void *v);
	void (*op_active_in)(tx_aiocb *f, tx_task_t *t);
	void (*op_cancel_in)(tx_aiocb *f, void *v);
};

struct tx_aiocb {
	int tx_fd;
	int tx_flags;
	void *tx_privp;
	tx_poll_t *tx_poll;
	tx_aiocb_op *tx_fops;

	tx_task_t *tx_filterin;
	tx_task_t *tx_filterout;
};

void tx_aiocb_init(tx_aiocb *filp, tx_loop_t *loop, int fd);
void tx_aincb_active(tx_aiocb *filp, tx_task_t *task);
void tx_aincb_cancel(tx_aiocb *filp, void *verify);
void tx_aincb_update(tx_aiocb *filp, int transfer);

void tx_outcb_active(tx_aiocb *filp, tx_task_t *task);
void tx_outcb_cancel(tx_aiocb *filp, void *verify);
void tx_outcb_update(tx_aiocb *filp, int transfer);
void tx_aiocb_fini(tx_aiocb *filp);

#endif

