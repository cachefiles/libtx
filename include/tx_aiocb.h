#ifndef _TX_AIOCB_
#define _TX_AIOCB_

struct tx_aiocb;
struct tx_loop_t;

#define TX_LISTEN   0x01
#define TX_POLLIN   0x02
#define TX_POLLOUT  0x04
#define TX_READABLE 0x08
#define TX_WRITABLE 0x10
#define TX_ATTACHED 0x20
#define TX_DETACHED 0x40
#define TX_MEMLOCK  0x80

#define tx_readable(filp) ((filp)->tx_flags & TX_READABLE)
#define tx_writable(filp) ((filp)->tx_flags & TX_WRITABLE)
#define tx_acceptable(filp) ((filp)->tx_flags & TX_READABLE)

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

void tx_listen_init(tx_aiocb *filp, tx_loop_t *loop, int fd);
int  tx_listen_accept(tx_aiocb *filp, struct sockaddr *sa0, size_t *plen);
#define tx_listen_active(filp, task) tx_aincb_active(filp, task)
#define tx_listen_fini(filp)  tx_aiocb_fini(filp)

void tx_aiocb_init(tx_aiocb *filp, tx_loop_t *loop, int fd);
int  tx_aiocb_connect(tx_aiocb *filp, struct sockaddr *sa0, tx_task_t *t);
void tx_aiocb_fini(tx_aiocb *filp);

void tx_aincb_active(tx_aiocb *filp, tx_task_t *task);
void tx_aincb_update(tx_aiocb *filp, int transfer);
void tx_aincb_stop(tx_aiocb *filp, void *verify);

void tx_outcb_prepare(tx_aiocb *filp, tx_task_t *task, int flags);
void tx_outcb_cancel(tx_aiocb *filp, void *verify);

int tx_outcb_xsend(tx_aiocb *filp, struct tx_aiobuf *buf, size_t count);
int tx_outcb_write(tx_aiocb *filp, const void *data, size_t len);
int tx_outcb_sent(tx_aiocb *filp, int index); // get transfer
int tx_outcb_stat(tx_aiocb *filp, int index);  // get out stat

#endif

