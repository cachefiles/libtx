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
	tx_poll_t epoll_poll;
	tx_task_t epoll_task;
	tx_loop_t *epoll_loop;
} tx_epoll_t;

static void tx_epoll_polling(void *up)
{
	int i;
	int nfds;
	int timeout;
	tx_loop_t *loop;
	tx_epoll_t *poll;
	struct epoll_event events[MAX_EVENTS];

	poll = (tx_epoll_t *)up;
	loop = poll->epoll_loop;
	timeout = 0; // get_from_loop

	nfds = epoll_wait(poll->epoll_fd, events, MAX_EVENTS, timeout);
	TX_PANIC(nfds == -1, "epoll_wait");

	for (i = 0; i < nfds; ++i) {
		if (events[i].events == 0) {
		}
	}

	tx_poll(loop, &poll->epoll_task);
	return;
}
#endif

tx_poll_t * tx_epoll_init(tx_loop_t *loop)
{
	int fd = -1;

#ifdef __linux__
	tx_epoll_t *poll = (tx_epoll_t *)malloc(sizeof(tx_epoll_t));
	TX_CHECK(poll == NULL, "create epoll failure");

	fd = epoll_create(10);
	TX_CHECK(fd == -1, "create epoll failure");

	if (poll != NULL && fd != -1) {
		poll->epoll_fd = fd;
		poll->epoll_loop = loop;
		tx_task_init(&poll->epoll_task, tx_epoll_polling, poll);
		tx_poll(loop, &poll->epoll_task);
		return &poll->epoll_poll;
	}

	free(poll);
	close(fd);
#endif

	fd = fd; //avoid warning
	return NULL;
}

