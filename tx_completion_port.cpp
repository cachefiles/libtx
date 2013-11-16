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
#define OVERLAPPED_IDLE 0x1
#define OVERLAPPED_BIND 0x2
#define OVERLAPPED_TASK 0x4

struct wsa_overlapped_t {
	OVERLAPPED tx_lapped; /* MUST KEEP THIS FIRST */
	void *tx_ulptr;
};

struct tx_overlapped_t {
	int tx_flags;
	int tx_refcnt;
	tx_file_t *tx_filp;
	LIST_ENTRY(tx_overlapped_t) entries;
	wsa_overlapped_t tx_send, tx_recv;
};

LIST_HEAD(tx_overlapped_l, tx_overlapped_t);

typedef struct tx_completion_port_t {
	HANDLE port_handle;
	tx_poll_t port_poll;
    tx_overlapped_l port_list;
#if ENABLE_ASNYIO
	tx_page_t *tx_page; /* for high speed async transfer */
	tx_page_t *rx_page; /* for high speed async transfer */
	tx_page_t *tx_cached; /* getther one byte to avoid WSAENOBUFS */
#endif
	char txl_books[8192 * 1024];
	char txl_pages[8192 * 1024];
	wsa_overlapped_t txl_overlappeds[2 * 1024];
} tx_completion_port_t;

static WSABUF _tx_wsa_buf = {0, 0};
static void tx_completion_port_pollout(tx_file_t *filp);
static void tx_completion_port_attach(tx_file_t *filp);
static void tx_completion_port_pollin(tx_file_t *filp);
static void tx_completion_port_detach(tx_file_t *filp);

static tx_poll_op _completion_port_ops = {
	tx_pollout: tx_completion_port_pollout,
	tx_attach: tx_completion_port_attach,
	tx_pollin: tx_completion_port_pollin,
	tx_detach: tx_completion_port_detach
};

void tx_completion_port_pollout(tx_file_t *filp)
{
    int error, flags;
	WSABUF wbuf = {0};
	tx_overlapped_t *olaped;
    TX_ASSERT(filp->tx_poll->tx_ops == &_completion_port_ops);

    if ((filp->tx_flags & TX_POLLOUT) == 0x0) {
		DWORD _wsa_transfer = 0;
        flags = TX_ATTACHED | TX_DETACHED;
        TX_ASSERT((filp->tx_flags & flags) == TX_ATTACHED);

		olaped = (tx_overlapped_t *)filp->tx_privp;
		memset(&olaped->tx_send.tx_lapped, 0, sizeof(olaped->tx_send.tx_lapped));
		olaped->tx_send.tx_ulptr = olaped;
		wbuf.buf = &filp->tx_sndnxt;
		wbuf.len = (filp->tx_flags & TX_ONE_BYTE)? 1: 0;
		error = WSASend(filp->tx_fd, &wbuf, 1,
				&_wsa_transfer, 0, &olaped->tx_send.tx_lapped, NULL);
        TX_CHECK(error != SOCKET_ERROR || WSAGetLastError() == WSA_IO_PENDING, "WSASend failure");
		if (error != SOCKET_ERROR ||
			WSAGetLastError() == WSA_IO_PENDING) {
			filp->tx_flags |= TX_POLLOUT;
			olaped->tx_refcnt++;
		}

		TX_ASSERT(olaped->tx_refcnt < 4);
    }

    return;
}

void tx_completion_port_attach(tx_file_t *filp)
{
    int error;
    int flags, tflag;
    HANDLE handle = NULL;
	tx_overlapped_t *olapped;
    tx_completion_port_t *port;
    port = container_of(filp->tx_poll, tx_completion_port_t, port_poll);
    TX_ASSERT(filp->tx_poll->tx_ops == &_completion_port_ops);

	flags = TX_ATTACHED| TX_DETACHED;
	tflag = (filp->tx_flags & flags);

	if (tflag == 0) {
        unsigned long nblock = 1;
        error = ioctlsocket(filp->tx_fd, FIONBIO, (unsigned long *)&nblock);
        TX_CHECK(error == 0, "set file to nonblock failure");
        handle = CreateIoCompletionPort((HANDLE)filp->tx_fd, port->port_handle, (ULONG_PTR)filp, 0);
        TX_PANIC(handle != NULL, "AssociateDeviceWithCompletionPort falure");
        filp->tx_flags |= (TX_ATTACHED| TX_ONE_BYTE);
		filp->tx_privp = olapped = new tx_overlapped_t;
        TX_PANIC(olapped != NULL, "allocate memoryallocate memory  falure");
		memset(olapped, 0, sizeof(*olapped));
		LIST_INSERT_HEAD(&port->port_list, olapped, entries);
		olapped->tx_filp = filp;
		olapped->tx_refcnt = 1;
    }   

	return;
}

void tx_completion_port_pollin(tx_file_t *filp)
{
    int error, flags;
	tx_overlapped_t *olaped;
    TX_ASSERT(filp->tx_poll->tx_ops == &_completion_port_ops);

    if ((filp->tx_flags & TX_POLLIN) == 0x0) {
		DWORD _wsa_flags = 0;
		DWORD _wsa_transfer = 0;
        flags = TX_ATTACHED | TX_DETACHED;
        TX_ASSERT((filp->tx_flags & flags) == TX_ATTACHED);

		olaped = (tx_overlapped_t *)filp->tx_privp;
		memset(&olaped->tx_recv.tx_lapped, 0, sizeof(olaped->tx_recv.tx_lapped));
		olaped->tx_recv.tx_ulptr = olaped;
		error = WSARecv(filp->tx_fd, &_tx_wsa_buf, 1,
				&_wsa_transfer, &_wsa_flags, &olaped->tx_recv.tx_lapped, NULL);
        TX_CHECK(error != SOCKET_ERROR || WSAGetLastError() == WSA_IO_PENDING, "WSARecv failure");
		if (error != SOCKET_ERROR ||
			WSAGetLastError() == WSA_IO_PENDING) {
			filp->tx_flags |= TX_POLLIN;
			olaped->tx_refcnt++;
		}

		TX_ASSERT(olaped->tx_refcnt < 4);
    }

	return;
}

void tx_completion_port_detach(tx_file_t *filp)
{
    int flags, tflag;
    TX_ASSERT(filp->tx_poll->tx_ops == &_completion_port_ops);

	flags = TX_ATTACHED| TX_DETACHED;
	tflag = (filp->tx_flags & flags);

	if (tflag == TX_ATTACHED) {
		tx_overlapped_t *olaped;
		olaped = (tx_overlapped_t *)filp->tx_privp;
        filp->tx_flags |= TX_DETACHED;
        filp->tx_privp = NULL;
		olaped->tx_filp = NULL;
		if (--olaped->tx_refcnt == 0) {
			LIST_REMOVE(olaped, entries);
			delete olaped;
		}
    }   

	return;
}

static void handle_overlapped(wsa_overlapped_t *ulptr, DWORD transfered)
{
	tx_file_t *filp;
	tx_overlapped_t *olaped;
	olaped = (tx_overlapped_t *)ulptr->tx_ulptr;

	if (transfered != 0 && transfered != 1) {
		TX_CHECK(transfered == 0, "transfer byte none zero");
		return;
	}

	if (--olaped->tx_refcnt == 0) {
		LIST_REMOVE(olaped, entries);
		delete olaped;
		return;
	}

	filp = olaped->tx_filp;
	if (filp != NULL) {
		if (ulptr == &olaped->tx_send) {
			TX_CHECK(transfered <= 1, "transfer byte none zero");
			TX_ASSERT(transfered == 0 || (filp->tx_flags & TX_ONE_BYTE));
			filp->tx_flags &= ~TX_ONE_BYTE;
			filp->tx_flags &= ~TX_POLLOUT;
			filp->tx_flags |= TX_WRITABLE;
			tx_task_active(filp->tx_filterout);
			filp->tx_filterout = NULL;
		} else if (ulptr == &olaped->tx_recv) {
			TX_CHECK(transfered == 0, "transfer byte none zero");
			filp->tx_flags &= ~TX_POLLIN;
			filp->tx_flags |= TX_READABLE;
			tx_task_active(filp->tx_filterin);
			filp->tx_filterin = NULL;
		}
	}

	return;
}

static void tx_completion_port_polling(void *up)
{
	int timeout;
	BOOL result;
	ULONG count;
    tx_loop_t *loop;
	wsa_overlapped_t *status;
	tx_completion_port_t *port;

	DWORD transfered_bytes;
	ULONG_PTR completion_key;
	LPOVERLAPPED overlapped = 0;

	port = (tx_completion_port_t *)up;
    loop = tx_loop_get(&port->port_poll.tx_task);

	for ( ; ; ) {
		timeout = tx_loop_timeout(loop, up)? 15: 0;
		result = GetQueuedCompletionStatus(port->port_handle,
				&transfered_bytes, &completion_key, &overlapped, timeout);
		if (overlapped == NULL &&
            result == FALSE && GetLastError() == WAIT_TIMEOUT) {
			/* TX_PRINT(TXL_MESSAGE, "completion port is clean"); */
			break;
		}

		TX_CHECK(overlapped != NULL, "could not get any event from port");
		status = (wsa_overlapped_t *)overlapped;
		handle_overlapped(status, transfered_bytes);
	}

	tx_poll_active(&port->port_poll);
	TX_UNUSED(count);
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

	LIST_FOREACH(np, taskq, entries)
		if (np->tx_call == tx_completion_port_polling)
			return container_of(np, tx_poll_t, tx_task);

	tx_completion_port_t *poll = (tx_completion_port_t *)malloc(sizeof(tx_completion_port_t));
	TX_CHECK(poll != NULL, "create completion port failure");

	handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	TX_CHECK(handle != INVALID_HANDLE_VALUE, "create completion port failure");

	if (poll != NULL && handle != INVALID_HANDLE_VALUE) {
		poll->port_handle = handle;
		tx_poll_init(&poll->port_poll, loop, tx_completion_port_polling, poll);
		tx_poll_active(&poll->port_poll);
		poll->port_poll.tx_ops = &_completion_port_ops;
		loop->tx_poller = &poll->port_poll;
		LIST_INIT(&poll->port_list);
#ifdef DISABLE_MULTI_POLLER
		loop->tx_holder = poll;
#endif
		return &poll->port_poll;
	}

	CloseHandle(handle);
	free(poll);
#endif

	TX_UNUSED(taskq);
	TX_UNUSED(np);
	return NULL;
}

