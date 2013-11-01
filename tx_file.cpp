#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#if defined(WIN32)
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#endif

#include "txall.h"

void tx_file_init(tx_file_t *filp, tx_poll_t *poll, int fd)
{
	tx_poll_op *ops;
	filp->tx_fd = fd;
	filp->tx_flags = 0;
	filp->tx_poll  = poll;
    filp->tx_privp = NULL;
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

int tx_recv(tx_file_t *filp, void *buf, size_t len, int flags)
{
#if defined(WIN32)
	int l = recv(filp->tx_fd, (char *)buf, len, flags);
	if (l == -1 && WSAEWOULDBLOCK == WSAGetLastError())
		filp->tx_flags &= ~TX_READABLE;
	return l;
#else
	int l = recv(filp->tx_fd, buf, len, flags);
	if (l == -1 && EAGAIN == errno)
		filp->tx_flags &= ~TX_READABLE;
	return l;
#endif
}

int tx_send(tx_file_t *filp, const void *buf, size_t len, int flags)
{
#if defined(WIN32)
	int l = send(filp->tx_fd, (const char *)buf, len, flags);
	if (l == -1 && WSAEWOULDBLOCK == WSAGetLastError())
		filp->tx_flags &= ~TX_WRITABLE;
	return l;
#else
	int l = send(filp->tx_fd, buf, len, flags);
	if (l == -1 && EAGAIN == errno)
		filp->tx_flags &= ~TX_WRITABLE;
	return l;
#endif
}

void tx_file_close(tx_file_t *filp)
{
	tx_poll_op *ops = filp->tx_poll->tx_ops;
	ops->tx_detach(filp);

#ifdef WIN32
	shutdown(filp->tx_fd, SD_BOTH);
	closesocket(filp->tx_fd);
#endif

#ifdef __linux__
	close(filp->tx_fd);
#endif
	return;
}

void tx_file_active_out(tx_file_t *filp, tx_task_t *task)
{
	tx_poll_op *ops;

	if (tx_writable(filp)) {
		TX_CHECK(filp->tx_filterout == NULL, "tx_filterout not null");
		tx_task_active(task);
		return;
	}

	if (filp->tx_filterout != task) {
		TX_CHECK(filp->tx_filterout == NULL, "tx_filterout not null");
		filp->tx_filterout = task;
	}

	ops = filp->tx_poll->tx_ops;
	ops->tx_pollout(filp);
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

