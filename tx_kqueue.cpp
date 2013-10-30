#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>

#ifdef __FreeBSD__
#include <sys/event.h>
#endif

#include "txall.h"

#define MAX_EVENTS 10

#ifdef __FreeBSD__
typedef struct tx_kqueue_t {
	int kqueue_fd;
	tx_poll_t kqueue_poll;
} tx_epoll_t;

static void tx_kqueue_polling(void *up)
{
	int i;
	int nfds;
	int timeout;
	tx_kqueue_t *poll;
	struct kevent events[MAX_EVENTS];

	poll = (tx_kqueue_t *)up;
	timeout = 0; // get_from_loop

	nfds = kevent(poll->kqueue_fd, NULL, 0, events, MAX_EVENTS, NULL);
	TX_PANIC(nfds == -1, "kevent");

	for (i = 0; i < nfds; ++i) {
		if (events[i].flags == 0) {
		}
	}

	tx_poll_active(&poll->kqueue_poll);
	return;
}
#endif

tx_poll_t *tx_kqueue_init(tx_loop_t *loop)
{
	int fd = -1;

#ifdef __FreeBSD__
	tx_kqueue_t *poll = (tx_kqueue_t *)malloc(sizeof(tx_kqueue_t));
	TX_CHECK(poll == NULL, "create kqueue failure");

	fd = kqueue();
	TX_CHECK(fd == -1, "create epoll failure");

	if (poll != NULL && fd != -1) {
		poll->kqueue_fd = fd;
		tx_poll_init(&poll->kqueue_poll, loop, tx_kqueue_polling, poll);
		tx_poll_active(&poll->kqueue_poll);
		return &poll->kqueue_poll;
	}

	free(poll);
	close(fd);
#endif

	fd = fd; //avoid warning
	return NULL;
}

