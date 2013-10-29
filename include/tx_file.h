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
int  rx_write(tx_file_t *filp, const void *buf, size_t len);
int  rx_read(tx_file_t *filp, void *buf, size_t len);
void tx_file_close(tx_file_t *filp);

#endif

