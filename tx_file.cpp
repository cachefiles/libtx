#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include "txall.h"

void tx_file_init(tx_file_t *filp, tx_poll_t *poll, int fd)
{
	tx_poll_op *ops;
	filp->tx_fd = fd;
	filp->tx_flags = 0;
	filp->tx_poll  = poll;
	filp->tx_filterin = NULL;
	filp->tx_filterout = NULL;
	
	ops = filp->tx_poll->tx_ops;
	ops->tx_attach(filp);
	return;
}

void tx_file_init(tx_file_t *filp, tx_loop_t *loop, int fd)
{
	tx_poll_t *poll = tx_poll_get(loop);
	TX_ASSERT(poll != NULL);
	tx_file_init(filp, poll, fd);
	return;
}

int tx_read(tx_file_t *filp, void *buf, size_t len)
{
	int l = read(filp->tx_fd, buf, len);
	if (l == -1 && EAGAIN == errno)
		filp->tx_flags &= ~TX_READABLE;
	return l;
}

int tx_write(tx_file_t *filp, const void *buf, size_t len)
{
	int l = write(filp->tx_fd, buf, len);
	if (l == -1 && EAGAIN == errno)
		filp->tx_flags &= ~TX_WRITABLE;
	return l;
}

void tx_file_close(tx_file_t *filp)
{
	tx_poll_op *ops = filp->tx_poll->tx_ops;
	ops->tx_detach(filp);

#ifdef __linux__
	close(filp->tx_fd);
#endif
	return;
}

void tx_file_active_out(tx_file_t *filp, tx_task_t *task)
{
	tx_poll_op *ops;

	if (tx_writable(filp)) {
		TX_CHECK(filp->tx_filterout, "tx_filterout not null");
		tx_task_active(task);
		return;
	}

	if (filp->tx_filterout != task) {
		TX_CHECK(filp->tx_filterout, "tx_filterout not null");
		filp->tx_filterout = task;
	}

	ops = filp->tx_poll->tx_ops;
	ops->tx_pollin(filp);
    return;
}

void tx_file_cancel_out(tx_file_t *filp, void *verify)
{
	if (verify == filp->tx_filterout)
		filp->tx_filterout = NULL;
    return;
}

void tx_file_active_in(tx_file_t *filp, tx_task_t *task)
{
	tx_poll_op *ops;

	if (tx_readable(filp)) {
		TX_CHECK(filp->tx_filterin == NULL, "tx_filterin not null");
		tx_task_active(task);
		return;
	}

	if (filp->tx_filterin != task) {
		TX_CHECK(filp->tx_filterin == NULL, "tx_filterout not null");
		filp->tx_filterin = task;
	}

	ops = filp->tx_poll->tx_ops;
	ops->tx_pollin(filp);
    return;
}

void tx_file_cancel_in(tx_file_t *filp, void *verify)
{
	if (verify == filp->tx_filterin)
		filp->tx_filterin = NULL;
    return;
}

