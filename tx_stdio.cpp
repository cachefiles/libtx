#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#if defined(WIN32)
#include <winsock2.h>
#include <windows.h>
#endif

#include "txall.h"

#if defined(WIN32)

#define MAX_BUF_SIZE  8192
#define MAX_BUF_COUNT 4

struct std_buf_t {
	int len;
	char buf[MAX_BUF_SIZE];
};

static struct {
	HANDLE _h;
	void _data;
	void (*_callback)(void *data);
	MUTEX _mutex;
	std_buf_t _buf[MAX_BUF_COUNT];
} __input, __output;

int input_thread()
{
	char *buf;
	for (int i = 0; i < 4; i++);
}

static int stdio_read(tx_file_t *filp, void *buf, size_t len)
{
	BOOL readed;
	DWORD transfered;
	HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
	readed = ReadFile(handle, buf, len, &transfered, NULL);
	return readed? transfered: -1;
}

static int stdio_write(tx_file_t *filp, const void *buf, size_t len)
{
	BOOL wroted;
	DWORD transfered;
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	wroted = WriteFile(handle, buf, len, &transfered, NULL);
	return wroted? transfered: -1;
}

static int stdio_recv(tx_file_t *filp, void *buf, size_t len, int flags)
{
	if (flags == 0)
		return stdio_read(filp, buf, len);
	return -1;
}

static int stdio_send(tx_file_t *filp, const void *buf, size_t len, int flags)
{
	if (flags == 0)
		return stdio_write(filp, buf, len);
	return -1;
}

static void stdio_close(tx_file_t *filp)
{
	return;
}

static void stdio_active_out(tx_file_t *filp, tx_task_t *task)
{
    return;
}

static void stdio_cancel_out(tx_file_t *filp, void *verify)
{
    return;
}

static void stdio_active_in(tx_file_t *filp, tx_task_t *task)
{
    return;
}

static void stdio_cancel_in(tx_file_t *filp, void *verify)
{
    return;
}

static tx_file_op _stdio_fops = {
	op_read: stdio_read,
	op_write: stdio_write,
	op_send: stdio_send,
	op_recv: stdio_recv,
	op_close: stdio_close,
	op_active_out: stdio_active_out,
	op_cancel_out: stdio_cancel_out,
	op_active_in: stdio_active_in,
	op_cancel_in: stdio_cancel_in
};

void tx_stdio_init(tx_file_t *filp, tx_poll_t *poll, int fd)
{
	tx_poll_op *ops;
	filp->tx_fd = fd;
	filp->tx_flags = 0;
	filp->tx_poll  = poll;
    filp->tx_privp = NULL;
	filp->tx_filterin = NULL;
	filp->tx_filterout = NULL;
	filp->tx_fops = &_stdio_fops;
	
	ops = filp->tx_poll->tx_ops;
	ops->tx_attach(filp);
	return;
}

void tx_stdio_init(tx_file_t *filp, tx_loop_t *loop, int fd)
{
	tx_poll_t *poll = tx_poll_get(loop);
	TX_ASSERT(poll != NULL);
	tx_stdio_init(filp, poll, fd);
	return;
}

#endif
