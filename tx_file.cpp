#include <stdio.h>
#include <unistd.h>

#include "txall.h"

void tx_file_init(tx_file_t *filp, tx_loop_t *loop, int fd)
{
	filp->tx_fd = fd;
	filp->tx_flags = 0;
	filp->tx_loop  = loop;
	return;
}

int tx_read(tx_file_t *filp, void *buf, size_t len)
{
	return read(filp->tx_fd, buf, len);
}

int tx_write(tx_file_t *filp, const void *buf, size_t len)
{
	return write(filp->tx_fd, buf, len);
}

void tx_file_close(tx_file_t *filp)
{
#ifdef __linux__
	close(filp->tx_fd);
#endif
	return;
}

void tx_file_active_out(tx_file_t *filp, tx_task_t *task)
{
    return;
}

void tx_file_cancel_out(tx_file_t *filp, void *verify)
{
    return;
}

void tx_file_active_in(tx_file_t *filp, tx_task_t *task)
{
    return;
}

void tx_file_cancel_in(tx_file_t *filp, void *verify)
{
    return;
}

