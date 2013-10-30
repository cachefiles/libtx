#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/epoll.h>
#endif

#include "txall.h"

#define MAX_EVENTS 10

#ifdef __linux__
typedef struct tx_epoll_t {
	int epoll_fd;
	tx_poll_t epoll_task;
} tx_epoll_t;

static void tx_epoll_pollout(tx_file_t *filp)
{
	return;
}

static void tx_epoll_attach(tx_file_t *filp)
{
	return;
}

static void tx_epoll_pollin(tx_file_t *filp)
{
	return;
}

static void tx_epoll_detach(tx_file_t *filp)
{
	return;
}

static tx_poll_op _epoll_ops = {
	.tx_pollout = tx_epoll_pollout,
	.tx_attach = tx_epoll_attach,
	.tx_pollin = tx_epoll_pollin,
	.tx_detach = tx_epoll_detach
};

static void tx_epoll_polling(void *up)
{
	int i;
	int nfds;
	int timeout;
	tx_loop_t *loop;
	tx_epoll_t *poll;
	struct epoll_event events[MAX_EVENTS];

	poll = (tx_epoll_t *)up;
	timeout = 0; // get_from_loop

	nfds = epoll_wait(poll->epoll_fd, events, MAX_EVENTS, timeout);
	TX_PANIC(nfds == -1, "epoll_wait");

	for (i = 0; i < nfds; ++i) {
		if (events[i].events == 0) {
		}
	}

	tx_poll_active(&poll->epoll_task);
	return;
}
#endif

tx_poll_t * tx_epoll_init(tx_loop_t *loop)
{
	int fd = -1;
	tx_task_t *np = 0;
	tx_task_q *taskq = &loop->tx_taskq;

#ifdef __linux__
	if (loop->tx_poller != NULL &&
		loop->tx_poller->tx_ops == &_epoll_ops) {
		TX_PRINT(TXL_ERROR, "completion port aready created");
		return loop->tx_poller;
	}

	 TAILQ_FOREACH(np, taskq, entries)
		if (np->tx_call == tx_epoll_polling)
			return container_of(np, tx_poll_t, tx_task);

	tx_epoll_t *poll = (tx_epoll_t *)malloc(sizeof(tx_epoll_t));
	TX_CHECK(poll == NULL, "create epoll failure");

	fd = epoll_create(10);
	TX_CHECK(fd == -1, "create epoll failure");

	if (poll != NULL && fd != -1) {
		tx_poll_init(&poll->epoll_task, loop, tx_epoll_polling, poll);
		tx_poll_active(&poll->epoll_task);
		loop->tx_poller = &poll->epoll_task;
		poll->epoll_fd = fd;
		return &poll->epoll_task;
	}

	free(poll);
	close(fd);
#endif

	taskq = taskq;
	fd = fd; //avoid warning
	np = np;
	return NULL;
}

