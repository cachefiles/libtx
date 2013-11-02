#ifndef _TX_FILE_
#define _TX_FILE_

struct tx_loop_t;
struct tx_file_t;

#define TX_POLLIN   0x01
#define TX_POLLOUT  0x02
#define TX_READABLE 0x04
#define TX_WRITABLE 0x08
#define TX_ATTACHED 0x10
#define TX_DETACHED 0x20

#define tx_readable(filp) ((filp)->tx_flags & TX_READABLE)
#define tx_writable(filp) ((filp)->tx_flags & TX_WRITABLE)

struct tx_file_op {
	int (*op_read)(tx_file_t *f, void *b, size_t l);
	int (*op_write)(tx_file_t *f, const void *b, size_t l);
	int (*op_send)(tx_file_t *f, const void *b, size_t l, int g);
	int (*op_recv)(tx_file_t *f, void *p, size_t l, int g);
	void (*op_close)(tx_file_t *f);
	void (*op_active_out)(tx_file_t *f, tx_task_t *t);
	void (*op_cancel_out)(tx_file_t *f, void *v);
	void (*op_active_in)(tx_file_t *f, tx_task_t *t);
	void (*op_cancel_in)(tx_file_t *f, void *v);
};

struct tx_file_t {
	int tx_fd;
	int tx_flags;
	void *tx_privp;
	tx_poll_t *tx_poll;
	tx_file_op *tx_fops;

	tx_task_t *tx_filterin;
	tx_task_t *tx_filterout;
};

void tx_file_init(tx_file_t *filp, tx_loop_t *loop, int fd);
int  tx_write(tx_file_t *filp, const void *buf, size_t len);
int  tx_read(tx_file_t *filp, void *buf, size_t len);
int  tx_send(tx_file_t *filp, const void *buf, size_t len, int flags);
int  tx_recv(tx_file_t *filp, void *buf, size_t len, int flags);
void tx_close(tx_file_t *filp);

void tx_active_out(tx_file_t *filp, tx_task_t *task);
void tx_cancel_out(tx_file_t *filp, void *verify);
void tx_active_in(tx_file_t *filp, tx_task_t *task);
void tx_cancel_in(tx_file_t *filp, void *verify);

#endif

