#ifndef _TX_FILE_
#define _TX_FILE_

struct tx_loop_t;
struct tx_file_t;

struct tx_file_t {
	int tx_fd;
	int tx_flags;
	tx_loop_t *tx_loop;
};

struct tx_wait_t {
};

void tx_file_init(tx_file_t *filp, tx_loop_t *loop, int fd);
int  rx_write(tx_file_t *filp, const void *buf, size_t len);
int  rx_read(tx_file_t *filp, void *buf, size_t len);
void tx_file_close(tx_file_t *filp);
int  tx_wait_out(tx_wait_t *outcbp, tx_file_t *filp, tx_task_t *task);
int  tx_wait_in(tx_wait_t *incbp, tx_file_t *filp, tx_task_t *task);
int  tx_wait_active(tx_wait_t *iocbp);
int  tx_wait_cancel(tx_wait_t *iocbp);

#endif

