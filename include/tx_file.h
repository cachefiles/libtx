#ifndef _TX_FILE_
#define _TX_FILE_

struct tx_loop_t;
struct tx_file_t;

struct tx_file_t {
	int tx_fd;
	int tx_flags;
	tx_loop_t *tx_loop;
};

void tx_file_init(tx_file_t *filp, tx_loop_t *loop, int fd);
int  tx_write(tx_file_t *filp, const void *buf, size_t len);
int  tx_read(tx_file_t *filp, void *buf, size_t len);
void tx_file_close(tx_file_t *filp);

void tx_file_active_out(tx_file_t *filp, tx_task_t *task);
void tx_file_cancel_out(tx_file_t *filp, void *verify);
void tx_file_active_in(tx_file_t *filp, tx_task_t *task);
void tx_file_cancel_in(tx_file_t *filp, void *verify);

#endif

