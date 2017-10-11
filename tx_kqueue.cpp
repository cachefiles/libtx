#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>

#ifdef __APPLE__
#define __FreeBSD__
#endif

#ifdef __FreeBSD__
#include <errno.h>
#include <string.h>
#include <sys/event.h>
#endif

#include "txall.h"

#define MAX_EVENTS 10

#ifdef __FreeBSD__
typedef struct tx_kqueue_t {
	int kqueue_fd;
	int epoll_refcnt;
	tx_poll_t kqueue_poll;
} tx_kqueue_t;

static void tx_kqueue_pollout(tx_aiocb *filp);
static void tx_kqueue_attach(tx_aiocb *filp);
static void tx_kqueue_pollin(tx_aiocb *filp);
static void tx_kqueue_detach(tx_aiocb *filp);

static tx_poll_op _kqueue_ops = {
	tx_sendout: NULL,
	tx_connect: NULL,
	tx_accept: NULL,
	tx_pollout: tx_kqueue_pollout,
	tx_attach: tx_kqueue_attach,
	tx_pollin: tx_kqueue_pollin,
	tx_detach: tx_kqueue_detach
};

void tx_kqueue_pollout(tx_aiocb *filp)
{
	int error;
	int flags;
	tx_kqueue_t *epoll;
	struct kevent event0 = {0};
	epoll = container_of(filp->tx_poll, tx_kqueue_t, kqueue_poll);
	TX_ASSERT(filp->tx_poll->tx_ops == &_kqueue_ops);

	if ((filp->tx_flags & TX_POLLOUT) == 0x0) {
		flags = TX_ATTACHED | TX_DETACHED;
		TX_ASSERT((filp->tx_flags & flags) == TX_ATTACHED);

		event0.ident = filp->tx_fd;
		event0.udata = filp;
		event0.flags = EV_ADD| EV_ONESHOT;
		event0.fflags = 0;
		event0.data = 0;
		event0.filter = EVFILT_WRITE;

		error = kevent(epoll->kqueue_fd, &event0, 1, NULL, 0, NULL);
		epoll->epoll_refcnt += (error != -1);
		filp->tx_flags |= (error != -1? TX_POLLOUT: 0);
		TX_CHECK(error != -1, "kqueue kevent pollout failure");
	}

#ifndef DISABLE_MULTI_POLLER
	if (epoll->epoll_refcnt > 0) {
		tx_loop_t *loop = tx_loop_get(&epoll->kqueue_poll.tx_task);
		if (!loop->tx_holder) 
			loop->tx_holder = epoll;
	}
#endif

	return;
}

void tx_kqueue_attach(tx_aiocb *filp)
{
	int error;
	int flags, tflag;
	tx_kqueue_t *epoll;
	epoll = container_of(filp->tx_poll, tx_kqueue_t, kqueue_poll);
	TX_ASSERT(filp->tx_poll->tx_ops == &_kqueue_ops);

	flags = TX_ATTACHED| TX_DETACHED;
	tflag = (filp->tx_flags & flags);

	if (tflag == 0 || tflag == flags) {
		filp->tx_flags |= TX_ATTACHED;
		filp->tx_flags &= ~TX_DETACHED;
	}

	return;
}

void tx_kqueue_pollin(tx_aiocb *filp)
{
	int error;
	int flags;
	tx_kqueue_t *epoll;
	struct kevent event0;
	epoll = container_of(filp->tx_poll, tx_kqueue_t, kqueue_poll);
	TX_ASSERT(filp->tx_poll->tx_ops == &_kqueue_ops);

	if ((filp->tx_flags & TX_POLLIN) == 0x0) {
		flags = TX_ATTACHED | TX_DETACHED;
		TX_ASSERT((filp->tx_flags & flags) == TX_ATTACHED);

		event0.ident = filp->tx_fd;
		event0.udata = filp;
		event0.flags = EV_ADD| EV_ONESHOT;
		event0.fflags = 0;
		event0.data = 0;
		event0.filter = EVFILT_READ;

		error = kevent(epoll->kqueue_fd, &event0, 1, NULL, 0, NULL);
		epoll->epoll_refcnt += (error != -1);
		filp->tx_flags |= (error != -1? TX_POLLIN: 0);
		TX_CHECK(error != -1, "kevent pollin failure");
	}

#ifndef DISABLE_MULTI_POLLER
	if (epoll->epoll_refcnt > 0) {
		tx_loop_t *loop = tx_loop_get(&epoll->kqueue_poll.tx_task);
		if (!loop->tx_holder) 
			loop->tx_holder = epoll;
	}
#endif

	return;
}

void tx_kqueue_detach(tx_aiocb *filp)
{
	int error;
	int flags;
	tx_kqueue_t *epoll;
	epoll = container_of(filp->tx_poll, tx_kqueue_t, kqueue_poll);
	TX_ASSERT(filp->tx_poll->tx_ops == &_kqueue_ops);

	flags = TX_ATTACHED| TX_DETACHED;
	if ((filp->tx_flags & flags) == TX_ATTACHED) {
		int nevent = 0;
		struct kevent events[2];

		if (filp->tx_flags & TX_POLLIN) {
			events[nevent].ident = filp->tx_fd;
			events[nevent].udata = filp;
			events[nevent].flags = EV_DELETE;
			events[nevent].fflags = 0;
			events[nevent].data = 0;
			events[nevent].filter = EVFILT_READ;
			nevent++;
		}

		if (filp->tx_flags & TX_POLLOUT) {
			events[nevent].ident = filp->tx_fd;
			events[nevent].udata = filp;
			events[nevent].flags = EV_DELETE;
			events[nevent].fflags = 0;
			events[nevent].data = 0;
			events[nevent].filter = EVFILT_WRITE;
			nevent++;
		}

		error = 0;
		if (nevent > 0) {
			error = kevent(epoll->kqueue_fd, events, nevent, NULL, 0, NULL);
			TX_CHECK(error != -1, "kevent pollin failure");
		}

		filp->tx_flags |= (error != -1? TX_DETACHED: 0);
	}

#ifndef DISABLE_MULTI_POLLER
	flags = TX_POLLIN| TX_POLLOUT;
	if (flags & filp->tx_flags) {
		tx_loop_t *loop = tx_loop_get(&epoll->kqueue_poll.tx_task);
		epoll->epoll_refcnt --;
		if (loop->tx_holder == epoll &&
				epoll->epoll_refcnt == 0)
			loop->tx_holder = NULL;
	}
#endif

	return;
}

static void tx_kqueue_polling(void *up)
{
	int i;
	int nfds;
	int timeout;
	tx_loop_t *loop;
	tx_kqueue_t *poll;
	struct timespec zerotime = {0, 0};
	struct timespec onetime  = {0, 10000000};
	struct kevent events[MAX_EVENTS];

	poll = (tx_kqueue_t *)up;
	loop = tx_loop_get(&poll->kqueue_poll.tx_task);
	timeout = tx_loop_timeout(loop, poll);

	nfds = kevent(poll->kqueue_fd, NULL, 0, events, MAX_EVENTS, timeout? &onetime: &zerotime);
	TX_PANIC(nfds != -1, "kevent");

	for (i = 0; i < nfds; ++i) {
		int flags = events[i].filter;
		tx_aiocb *filp = (tx_aiocb *)events[i].udata;

		poll->epoll_refcnt--; 

		if (flags == EVFILT_READ) {
			tx_task_active(filp->tx_filterin, filp);
			filp->tx_flags |= TX_READABLE;
			filp->tx_filterin = NULL;
			filp->tx_flags &= ~TX_POLLIN;
			continue;
		}

		if (flags == EVFILT_WRITE) {
			tx_task_active(filp->tx_filterout, filp);
			filp->tx_flags |= TX_WRITABLE;
			filp->tx_filterout = NULL;
			filp->tx_flags &= ~TX_POLLOUT;
			continue;
		}
	}

#ifndef DISABLE_MULTI_POLLER
	if (loop->tx_holder == poll &&
		poll->epoll_refcnt == 0)
		loop->tx_holder = NULL;
#endif

	tx_poll_active(&poll->kqueue_poll);
	return;
}
#endif

tx_poll_t *tx_kqueue_init(tx_loop_t *loop)
{
	int fd = -1;
	tx_task_t *np = 0;
	tx_task_q *taskq = &loop->tx_taskq;

#ifdef __FreeBSD__
	if (loop->tx_poller != NULL &&
		loop->tx_poller->tx_ops == &_kqueue_ops) {
		LOG_ERROR("completion port aready created");
		return loop->tx_poller;
	}

	 LIST_FOREACH(np, taskq, entries)
		if (np->tx_call == tx_kqueue_polling)
			return container_of(np, tx_poll_t, tx_task);

	tx_kqueue_t *poll = (tx_kqueue_t *)malloc(sizeof(tx_kqueue_t));
	TX_CHECK(poll != NULL, "create kqueue failure");

	fd = kqueue();
	TX_CHECK(fd != -1, "create epoll failure");

	if (poll != NULL && fd != -1) {
		tx_poll_init(&poll->kqueue_poll, loop, tx_kqueue_polling, poll);
		tx_poll_active(&poll->kqueue_poll);
		poll->kqueue_poll.tx_ops = &_kqueue_ops;
		loop->tx_poller = &poll->kqueue_poll;
#ifdef DISABLE_MULTI_POLLER
		loop->tx_holder = poll;
#endif
		poll->epoll_refcnt = 0;
		poll->kqueue_fd = fd;
		return &poll->kqueue_poll;
	}

	free(poll);
	close(fd);
#endif

    	TX_UNUSED(taskq);
    	TX_UNUSED(fd);
    	TX_UNUSED(np);
    	return NULL;
}

