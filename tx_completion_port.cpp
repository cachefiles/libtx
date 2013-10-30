#include <time.h>
#include <stdio.h>

#ifdef WIN32
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#endif

#ifdef WIN32
#ifndef WSAID_TRANSMITFILE
#define WSAID_TRANSMITFILE \
{0xb5367df0,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
#endif

#ifndef WSAID_ACCEPTEX
#define WSAID_ACCEPTEX \
{0xb5367df1,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
typedef BOOL (PASCAL *LPFN_ACCEPTEX)(SOCKET, SOCKET, PVOID, DWORD,
        DWORD, DWORD, LPDWORD, LPOVERLAPPED);
#endif

#ifndef WSAID_CONNECTEX
#define WSAID_CONNECTEX \
{0x25a207b9,0xddf3,0x4660,{0x8e,0xe9,0x76,0xe5,0x8c,0x74,0x06,0x3e}}
typedef BOOL (PASCAL *LPFN_CONNECTEX)(SOCKET, const struct sockaddr *,
        int, PVOID, DWORD, LPDWORD, LPOVERLAPPED);
#endif

static LPFN_ACCEPTEX lpAcceptEx = NULL;
static LPFN_CONNECTEX lpConnectEx = NULL;
#endif

#include "txall.h"

#ifdef WIN32
typedef struct tx_completion_port_t {
	HANDLE port_handle;
	tx_poll_t port_poll;
} tx_completion_port_t;

#define ENTRIES_COUNT 10
#define OVERLAPPED_IDLE 0x1
#define OVERLAPPED_BIND 0x2
#define OVERLAPPED_TASK 0x4

struct tx_overlapped_t {
	int tx_flags;
	OVERLAPPED tx_internal;
	tx_task_t *tx_inout_task;
};

static void tx_completion_port_pollout(tx_file_t *filp)
{
	return;
}

static void tx_completion_port_attach(tx_file_t *filp)
{
	return;
}

static void tx_completion_port_pollin(tx_file_t *filp)
{
	return;
}

static void tx_completion_port_detach(tx_file_t *filp)
{
	return;
}

static tx_poll_op _completion_port_ops = {
	tx_pollout: tx_completion_port_pollout,
	tx_attach: tx_completion_port_attach,
	tx_pollin: tx_completion_port_pollin,
	tx_detach: tx_completion_port_detach
};

static void tx_completion_port_polling(void *up)
{
	int timeout;
	BOOL result;
	ULONG count;
	tx_overlapped_t *status;
	tx_completion_port_t *port;

	DWORD transfered_bytes;
	ULONG_PTR completion_key;
	LPOVERLAPPED overlapped = {0};

	port = (tx_completion_port_t *)up;
	timeout = 0; // get_from_loop

	for ( ; ; ) {
		result = GetQueuedCompletionStatus(port->port_handle,
				&transfered_bytes, &completion_key, &overlapped, 0);
		if (result == FALSE &&
			overlapped == NULL &&
			GetLastError() == WAIT_TIMEOUT) {
			TX_PRINT(TXL_MESSAGE, "completion port is clean");
			break;
		}

		TX_CHECK(overlapped == NULL, "could not get any event from port");
		status = (tx_overlapped_t *)overlapped;
		if (status->tx_flags & OVERLAPPED_TASK)
			tx_task_active(status->tx_inout_task);
	}

	result = GetQueuedCompletionStatus(port->port_handle,
			&transfered_bytes, &completion_key, &overlapped, timeout);
	if (result == FALSE &&
			overlapped == NULL &&
			GetLastError() == WAIT_TIMEOUT) {
		TX_PRINT(TXL_MESSAGE, "completion port is clean");
	} else {
		TX_CHECK(overlapped == NULL, "could not get any event from port");
		status = (tx_overlapped_t *)overlapped;
		if (status->tx_flags & OVERLAPPED_TASK)
			tx_task_active(status->tx_inout_task);
	}

	tx_poll_active(&port->port_poll);
	count = count;
	return;
}
#endif

tx_poll_t* tx_completion_port_init(tx_loop_t *loop)
{
	tx_task_t *np = 0;
	tx_task_q *taskq = &loop->tx_taskq;

#ifdef WIN32
	WSADATA wsadata;
	HANDLE handle = INVALID_HANDLE_VALUE;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	if (loop->tx_poller != NULL &&
		loop->tx_poller->tx_ops == &_completion_port_ops) {
		TX_PRINT(TXL_ERROR, "completion port aready created");
		return loop->tx_poller;
	}

	TAILQ_FOREACH(np, taskq, entries)
		if (np->tx_call == tx_completion_port_polling)
			return container_of(np, tx_poll_t, tx_task);

	tx_completion_port_t *poll = (tx_completion_port_t *)malloc(sizeof(tx_completion_port_t));
	TX_CHECK(poll == NULL, "create completion port failure");

	handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	TX_CHECK(handle == INVALID_HANDLE_VALUE, "create completion port failure");

	if (poll != NULL && handle != INVALID_HANDLE_VALUE) {
		poll->port_handle = handle;
		tx_poll_init(&poll->port_poll, loop, tx_completion_port_polling, poll);
		tx_poll_active(&poll->port_poll);
		poll->port_poll.tx_ops = &_completion_port_ops;
		loop->tx_poller = &poll->port_poll;
		return &poll->port_poll;
	}

	CloseHandle(handle);
	free(poll);
#endif

	taskq = taskq;
	np = np;
	return NULL;
}

